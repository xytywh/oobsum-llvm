#!/usr/bin/env python3
"""统计OobSummary可观察范围并运行自有深度/CFG实验（SPEC-0135）。

背景/谁用：规则维护者和评审者需要区分调用层数、单函数CFG限制、精度预算和报告策略。
做什么：stats逐TU读FIR，统计块数/跳过标记/显式调用图SCC，以及可选判断标签/原因/depth。
只输出计数，不列业务名字、路径或正文；audit_complete仅指统计文件读取成功，不代表100%覆盖。
被cfg_too_large/no_cfg替换的函数仅有占位块，真实块数未知，不能用于最大CFG或直方图。
缺FIR体的调用可能有模型，间接调用可能已被引擎解析；图深度只指已观察直接图，不是运行时栈上限。
用法：
  python3 scope_audit.py stats --workdir W --fir-only --out scope.json
  python3 scope_audit.py stats --workdir W --out scope-with-decisions.json
  python3 scope_audit.py bench --clang cmake-build-release/bin/clang --out /tmp/oobsum-bench
  python3 scope_audit.py bench --clang CLANG --out NEW_DIR --wide-branches 2600 --wide-low 5000 --wide-high 6000
bench仅生成自有代码，inline模式同一摘要引擎；每case独立WorkDir，保存源/FIR/判断/选项和stderr，
results.json记录标签、实际depth/CFG、墙钟时间（含解析/IO）。--timeout是实验进程限时，不是checker预算。
退出码：0统计完成/实验符合预期；2缺失/读入失败或实验未符合预期。仅标准库，不修改规则/输入。
"""
import argparse
from collections import Counter, deque
import json
import os
from pathlib import Path
import subprocess
import sys
import time

LABELS = {'definite', 'possible', 'suspect', 'safe'}
REASONS = {'entry_unproven', 'type_range_only', 'typed_field', 'untracked_pointer', 'unknown_callee',
           'not_in_tu', 'not_exported', 'fptr', 'cxx_call', 'recursion', 'subobject_overflow',
           'global_value', 'pointer_stepping', 'alloc_size_unknown', 'unsupported_construct', 'alias_unverified',
           'unanalyzed:cfg_too_large', 'unanalyzed:no_cfg', 'unanalyzed:iter_limit'}
FILTERS = {'safe', 'inline_depth', 'duplicate_access', 'duplicate_fp', 'flexible_array', 'policy',
           'suspect_reason', 'suspect_reasons', 'balanced', 'disabled', 'policy_balanced', 'reason_filtered'}


def graph_counts(graph):
    """迭代Kosaraju，避免统计脚本自己被Python递归上限截断。"""
    reverse = {key: set() for key in graph}
    edges = 0
    for key, outgoing in graph.items():
        for target in outgoing:
            reverse[target].add(key)
            edges += 1
    seen, order = set(), []
    for root in graph:
        if root in seen:
            continue
        seen.add(root)
        stack = [(root, iter(graph[root]))]
        while stack:
            node, outgoing = stack[-1]
            nxt = next(outgoing, None)
            if nxt is None:
                order.append(node)
                stack.pop()
            elif nxt not in seen:
                seen.add(nxt)
                stack.append((nxt, iter(graph[nxt])))
    component, sizes = {}, []
    for root in reversed(order):
        if root in component:
            continue
        number = len(sizes)
        component[root] = number
        stack, size = [root], 0
        while stack:
            key = stack.pop()
            size += 1
            for nxt in reverse[key]:
                if nxt not in component:
                    component[nxt] = number
                    stack.append(nxt)
        sizes.append(size)
    dag = {i: set() for i in range(len(sizes))}
    recursive = {i for i, size in enumerate(sizes) if size > 1}
    for key, outgoing in graph.items():
        for nxt in outgoing:
            if key == nxt:
                recursive.add(component[key])
            if component[key] != component[nxt]:
                dag[component[key]].add(component[nxt])
    incoming, depth = Counter(), {i: 0 for i in dag}
    for outgoing in dag.values():
        incoming.update(outgoing)
    todo = deque(i for i in dag if not incoming[i])
    while todo:
        key = todo.popleft()
        for nxt in dag[key]:
            depth[nxt] = max(depth[nxt], depth[key] + 1)
            incoming[nxt] -= 1
            if not incoming[nxt]:
                todo.append(nxt)
    longest = max(depth.values(), default=0)
    return dict(resolved_direct_edges=edges, scc_count=len(sizes), recursive_sccs=len(recursive),
                recursive_functions=sum(sizes[i] for i in recursive), condensed_longest_edges=longest,
                direct_dag_longest_edges=None if recursive else longest)


def stats(workdir, fir_only=False):
    root = Path(workdir)
    modules = sorted((root / 'fir').glob('*.json'))
    raw, duplicates, errors, missing, indirect, callsites = 0, 0, 0, 0, 0, 0
    raw_flags, unique_flags, histogram = Counter(), Counter(), Counter()
    functions, external_duplicates, maximum, total_blocks = {}, set(), 0, 0
    for i, path in enumerate(modules):
        if i % 100 == 0:
            print('[oobsum:scope] fir completed=%d total=%d' % (i, len(modules)), file=sys.stderr, flush=True)
        try:
            with path.open(encoding='utf-8') as stream:
                module = json.load(stream)
            t = module['target']
            target = (t['triple'], t['ptr'], t['long'], t['big_endian'])
            for fn in module['funcs']:
                key = (target, fn['key'])
                flags = fn.get('flags', [])
                raw += 1
                raw_flags.update(x if x in ('cfg_too_large', 'no_cfg') else 'other' for x in flags)
                if not any(f in flags for f in ('cfg_too_large', 'no_cfg')):
                    size = len(fn['blocks'])
                    maximum = max(maximum, size)
                    total_blocks += size
                    bucket = next((str(bound) for bound in (16, 64, 256, 1024, 5000) if size <= bound), 'above5000')
                    histogram[bucket] += 1
                if key in functions:
                    duplicates += 1
                    if fn['key'].startswith('F:'):
                        external_duplicates.add(key)
                    continue
                unique_flags.update(x if x in ('cfg_too_large', 'no_cfg') else 'other' for x in flags)
                calls = []
                for block in fn['blocks']:
                    for inst in block['insts']:
                        if inst['op'] == 'call':
                            calls.append(inst['callee'])
                functions[key] = calls
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            errors += 1
    graph = {key: set() for key in functions}
    for key, outgoing in functions.items():
        for callee in outgoing:
            callsites += 1
            if not isinstance(callee, str):
                indirect += 1
            elif (key[0], callee) in functions:
                graph[key].add((key[0], callee))
            else:
                missing += 1
    labels, emitted, reasons, filtered, depths = Counter(), Counter(), Counter(), Counter(), Counter()
    max_depth, max_eff = 0, 0
    logs = [] if fir_only else sorted((root / 'decisions').glob('*.jsonl'))
    for path in logs:
        try:
            with path.open(encoding='utf-8') as stream:
                for line in stream:
                    if not line.strip():
                        continue
                    row = json.loads(line)
                    if row.get('schema') != 'oobsum-decision/1':
                        continue
                    if row['label'] not in LABELS:
                        raise ValueError('unknown decision label')
                    label = row['label']
                    labels[label] += 1
                    if row['label'] == 'suspect':
                        reason = row.get('reason', '')
                        reasons[reason if reason in REASONS else 'other'] += 1
                    if row.get('emitted'):
                        emitted[label] += 1
                    else:
                        why = row.get('not_emitted_because', '')
                        filtered[why if why in FILTERS else 'other'] += 1
                    d, e = int(row.get('depth', 0)), int(row.get('eff_depth', 0))
                    depths[str(max(d, e))] += 1
                    max_depth, max_eff = max(max_depth, d), max(max_eff, e)
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            errors += 1
    conflict = (root / 'fir-conflicts').exists() or (root / 'fir-conflicts').is_symlink()
    temporary = sum(1 for _ in (root / 'fir').glob('*.tmp'))
    return dict(schema='oobsum-scope/1', audit_complete=bool(modules) and errors == 0 and not conflict and not temporary and (fir_only or bool(logs)),
                fir_files=len(modules), read_errors=errors, raw_function_definitions=raw, unique_function_keys=len(functions),
                duplicate_definitions=duplicates, duplicate_external_keys=len(external_duplicates),
                flags_raw=dict(raw_flags), flags_unique=dict(unique_flags),
                max_lowered_cfg_blocks=maximum, total_lowered_cfg_blocks_raw=total_blocks,
                cfg_blocks_histogram_raw_upper_bound=dict(histogram),
                graph=dict(graph_counts(graph), direct_call_sites=callsites - indirect,
                           indirect_call_sites=indirect, calls_missing_fir_body=missing),
                decision_log_files=len(logs), decision_logs_requested=not fir_only, labels=dict(labels), emitted_labels=dict(emitted),
                suspect_reasons=dict(reasons), filtered=dict(filtered), observed_depth_max=max_depth,
                observed_eff_depth_max=max_eff, judgments_by_effective_depth=dict(depths),
                input_guard=dict(conflict=conflict, temporary=temporary, pending=(root / 'inputs-pending').exists() or (root / 'inputs-pending').is_symlink()))


def bench(args):
    root = Path(args.out).resolve()
    if root.exists():
        raise ValueError('benchmark output directory must be new')
    root.mkdir(parents=True)
    sources = root / 'sources'
    sources.mkdir()
    depths = [int(d) for d in args.depths.split(',')]
    if any(d < 1 for d in depths) or args.wide_branches < 1:
        raise ValueError('depths and branch count must be positive')
    cases = []
    for depth in depths:
        source = sources / ('chain-%d.c' % depth)
        code = ['static void Leaf(char *p) { p[8] = 0; }']
        for i in range(depth - 1):
            code.append('static void F%d(char *p) { %s(p); }' % (i, 'Leaf' if i == 0 else 'F%d' % (i - 1)))
        code.append('void Entry(void) { char a[8]; %s(a); }' % ('Leaf' if depth == 1 else 'F%d' % (depth - 2)))
        source.write_text('\n'.join(code) + '\n')
        cases.append(('chain-%d' % depth, source, 3, depth, False))
    source = sources / 'wide.c'
    source.write_text('static void Big(char *p, unsigned k) { int d=0;\n' +
                      ''.join('if(k==%d) d=1;\n' % i for i in range(1, args.wide_branches + 1)) +
                      '(void)d; p[8]=0; }\nvoid Entry(void) { char a[8]; Big(a,0); }\n')
    cases += [('wide-low', source, args.wide_low, 1, True), ('wide-high', source, args.wide_high, 1, False)]
    results = []
    for name, source, limit, requested, skipped in cases:
        wd = root / name
        wd.mkdir()
        command = [args.clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
                   '-analyzer-checker=rb.enhance.core.OobSummary', '-analyzer-config', 'ipa=none']
        for option in ['Phase=inline', 'WorkDir=' + str(wd), 'MaxBlocks=' + str(limit), 'Dump=options+stats']:
            command += ['-analyzer-config', 'rb.enhance.core.OobSummary:' + option]
        command += [str(source)]
        (wd / 'command.json').write_text(json.dumps(command, indent=1) + '\n')
        print('[oobsum:scope] bench=' + name, file=sys.stderr, flush=True)
        child_env = os.environ.copy()
        child_env.pop('OOBSUM_CONFIG', None)  # 自有实验不能被业务阶段/WorkDir环境变量重定向。
        start = time.monotonic()
        with (wd / 'stderr.log').open('w') as log:
            try:
                process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, env=child_env)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                exit_code = 'timeout'
        summary = stats(wd)
        hit = summary['flags_unique'].get('cfg_too_large', 0)
        passed = (exit_code == 0 and summary['audit_complete'] and
                  (hit > 0 and summary['labels'].get('suspect', 0) > 0 if skipped else
                   hit == 0 and summary['emitted_labels'].get('definite', 0) > 0 and summary['observed_depth_max'] >= requested))
        result = dict(case=name, max_blocks=limit, requested_call_edges=requested,
                      elapsed_s=round(time.monotonic() - start, 4), exit=exit_code, passed=bool(passed), stats=summary)
        results.append(result)
        (wd / 'result.json').write_text(json.dumps(result, indent=1) + '\n')
    output = dict(schema='oobsum-scope-bench/1', mode='inline', all_passed=all(r['passed'] for r in results), cases=results)
    (root / 'results.json').write_text(json.dumps(output, indent=1) + '\n')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('stats')
    p.add_argument('--workdir', required=True)
    p.add_argument('--fir-only', action='store_true')
    p.add_argument('--out')
    p = sub.add_parser('bench')
    p.add_argument('--clang', required=True)
    p.add_argument('--out', required=True, help='新目录，保存合成源/各次日志/结果')
    p.add_argument('--depths', default='1,16,64,256')
    p.add_argument('--wide-branches', type=int, default=32)
    p.add_argument('--wide-low', type=int, default=16)
    p.add_argument('--wide-high', type=int, default=512)
    p.add_argument('--timeout', type=float, default=30)
    args = parser.parse_args()
    try:
        result = bench(args) if args.cmd == 'bench' else stats(args.workdir, args.fir_only)
        if args.cmd == 'stats' and args.out:
            Path(args.out).write_text(json.dumps(result, indent=1) + '\n')
        print(json.dumps(result, indent=1))
        return 0 if result.get('all_passed', result.get('audit_complete', False)) else 2
    except (OSError, ValueError) as error:
        print(json.dumps({'audit_complete': False, 'error': type(error).__name__}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
