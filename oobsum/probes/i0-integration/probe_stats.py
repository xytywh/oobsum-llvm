#!/usr/bin/env python3
"""汇总 OobSummaryProbe 在 WorkDir 下写出的 tu-*.json，给出编码计划要的几组数字。

用法：
    python3 probe_stats.py <WorkDir> [--wall-seconds N] [--md out.md] [--json out.json]

回答的问题（对应 07-dev-plan.md 的输入 I3、I6 与 06-spec.md 的待定项）：
    1. 目标 ABI：每个 TU 的 triple、指针 / long 字节数、大小端（ILP32 是否真的生效）
    2. 规模：TU 数、函数数、CFG 块数分布（P50/P90/P99/最大）、带循环的函数占比
    3. 断链风险：直接调用 / 函数指针调用的比例；跨 TU 被调者"全工程唯一定义 / 多个定义 / 没有定义"的比例
    4. 多轮扫描代价：按函数级调用图（跨 TU 才 +1）算出需要几轮、总共要重复分析多少个 TU
    5. 解析 + 建 CFG 的耗时（checker 内部计时；全量墙钟时间请用 --wall-seconds 传入 time 的结果）

只依赖 Python 3 标准库。
"""
import argparse
import glob
import json
import os
import sys
from collections import Counter, defaultdict


def pct(vals, p):
    if not vals:
        return 0
    s = sorted(vals)
    k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[k]


def tarjan(nodes, succ):
    """迭代式 Tarjan，返回 SCC 列表（逆拓扑序：被调者所在分量先出）。"""
    index, low, on, stack, out = {}, {}, set(), [], []
    counter = [0]
    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(succ.get(root, ())))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(succ.get(w, ()))))
                    advanced = True
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                out.append(comp)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('workdir')
    ap.add_argument('--wall-seconds', type=float, default=None, help='全量 bxen 运行的墙钟秒数（time 的 real）')
    ap.add_argument('--md')
    ap.add_argument('--json')
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.workdir, 'tu-*.json')))
    if not files:
        sys.exit('没有找到 tu-*.json：WorkDir 是否传对、checker 是否真的跑了')
    tus = []
    for f in files:
        try:
            tus.append(json.load(open(f, encoding='utf-8')))
        except ValueError as e:
            print('跳过损坏的文件 %s：%s' % (f, e), file=sys.stderr)

    abi = Counter((t['triple'], t['ptr_bytes'], t['long_bytes'], 'BE' if t['big_endian'] else 'LE') for t in tus)

    # 函数：只统计主文件里定义的（头文件 static inline 每个 TU 一份，单列）
    defs = defaultdict(list)          # key -> [tu]
    func_tu = {}
    blocks, loops_fn, direct, indirect, header_inline = [], 0, 0, 0, 0
    calls = []                        # (caller_key, callee_key, caller_tu)
    for t in tus:
        for fn in t['funcs']:
            if not fn['main']:
                header_inline += 1
                continue
            defs[fn['key']].append(t['tu'])
            func_tu[fn['key']] = t['tu']
            blocks.append(fn['blocks'])
            loops_fn += 1 if fn['loops'] else 0
            direct += fn['direct']
            indirect += fn['indirect']
            for c in fn['callees']:
                calls.append((fn['key'], c, t['tu']))
    nfunc = len(blocks)

    # 被调者解析：同 TU 定义 / 全工程唯一定义 / 多个定义（同名外部函数分属不同产品）/ 没有定义（库、未进 cdb）
    tu_defs = defaultdict(set)
    for k, ts in defs.items():
        for tu in ts:
            tu_defs[tu].add(k)
    res = Counter()
    succ = defaultdict(set)
    for caller, callee, tu in calls:
        if callee in tu_defs[tu]:
            res['same_tu'] += 1
            succ[caller].add(callee)
        elif callee.startswith('F@'):
            res['same_tu_header_inline'] += 1
        elif len(defs.get(callee, ())) == 1:
            res['cross_tu_unique'] += 1
            succ[caller].add(callee)
        elif len(defs.get(callee, ())) > 1:
            res['cross_tu_ambiguous'] += 1
        else:
            res['undefined'] += 1
    dup_keys = sum(1 for k, v in defs.items() if len(v) > 1)

    # 多轮扫描：函数级图，跨 TU 的边 +1；SCC 内取同一个轮次
    nodes = list(func_tu.keys())
    comps = tarjan(nodes, succ)
    comp_of = {}
    for i, c in enumerate(comps):
        for v in c:
            comp_of[v] = i
    wave_comp = [0] * len(comps)
    for i, c in enumerate(comps):           # 逆拓扑序：被调者分量已算完
        w = 0
        for v in c:
            for s in succ.get(v, ()):
                j = comp_of.get(s)
                if j is None or j == i:
                    continue
                w = max(w, wave_comp[j] + (1 if func_tu.get(s) != func_tu.get(v) else 0))
        wave_comp[i] = w
    waves_by_tu = defaultdict(set)
    for v in nodes:
        waves_by_tu[func_tu[v]].add(wave_comp[comp_of[v]])
    rounds = (max(wave_comp) + 1) if wave_comp else 0
    tu_analyses = sum(len(s) for s in waves_by_tu.values())
    per_round = Counter()
    for tu, s in waves_by_tu.items():
        for w in s:
            per_round[w] += 1
    cyc = sum(1 for c in comps if len(c) > 1) + sum(1 for c in comps if len(c) == 1 and c[0] in succ.get(c[0], ()))

    tu_ms = [t.get('tu_ms', 0.0) for t in tus]
    out = {
        'tus': len(tus), 'functions_main': nfunc, 'header_inline_copies': header_inline,
        'abi': [{'triple': k[0], 'ptr': k[1], 'long': k[2], 'endian': k[3], 'tus': v} for k, v in abi.most_common()],
        'cfg_blocks': {'p50': pct(blocks, 50), 'p90': pct(blocks, 90), 'p99': pct(blocks, 99), 'max': max(blocks or [0])},
        'functions_with_loops': loops_fn,
        'calls': {'direct': direct, 'indirect_fptr': indirect,
                  'indirect_ratio': round(indirect / float(direct + indirect), 4) if direct + indirect else 0},
        'callee_resolution': dict(res), 'duplicate_external_definitions': dup_keys,
        'recursive_sccs': cyc,
        'multi_round': {'rounds': rounds, 'tu_analyses': tu_analyses,
                        'tu_analyses_over_tus': round(tu_analyses / float(len(tus)), 3) if tus else 0,
                        'tus_per_round': [per_round[w] for w in range(rounds)]},
        'cfg_ms_sum': round(sum(tu_ms), 1), 'cfg_ms_p99_tu': round(pct(tu_ms, 99), 1),
        'wall_seconds_one_pass': a.wall_seconds,
    }
    if a.wall_seconds:
        out['multi_round']['estimated_wall_seconds'] = round(a.wall_seconds * out['multi_round']['tu_analyses_over_tus'], 1)

    lines = ['# OobSummaryProbe 全量统计', '',
             '| 项 | 值 |', '|---|---|',
             '| TU 数 | %d |' % out['tus'],
             '| 主文件里定义的函数 | %d（另有头文件 static inline 副本 %d） |' % (nfunc, header_inline),
             '| 目标 ABI | %s |' % '；'.join('%s ptr=%s long=%s %s ×%d' % (x['triple'], x['ptr'], x['long'], x['endian'], x['tus']) for x in out['abi']),
             '| CFG 块数 P50 / P90 / P99 / 最大 | %(p50)s / %(p90)s / %(p99)s / %(max)s |' % out['cfg_blocks'],
             '| 含循环的函数 | %d（%.1f%%） |' % (loops_fn, 100.0 * loops_fn / max(1, nfunc)),
             '| 直接调用 / 函数指针调用 | %d / %d（函数指针 %.1f%%） |' % (direct, indirect, 100.0 * out['calls']['indirect_ratio']),
             '| 被调者解析 | %s |' % '，'.join('%s %d' % kv for kv in sorted(res.items())),
             '| 同名外部函数有多个定义 | %d 个名字 |' % dup_keys,
             '| 递归分量 | %d |' % cyc,
             '| 多轮扫描：轮数 | %d |' % rounds,
             '| 多轮扫描：TU 分析总次数 / TU 数 | %d / %d = %.2f |' % (tu_analyses, len(tus), out['multi_round']['tu_analyses_over_tus']),
             '| 每轮 TU 数 | %s |' % ' '.join(str(x) for x in out['multi_round']['tus_per_round'][:30]),
             '| checker 内建 CFG 累计耗时 | %.1f 秒（最慢 1%% 的 TU ≥ %.0f ms） |' % (out['cfg_ms_sum'] / 1000.0, out['cfg_ms_p99_tu'])]
    if a.wall_seconds:
        lines.append('| 一遍全量墙钟 / 多轮估算 | %.0f 秒 / %.0f 秒 |' % (a.wall_seconds, out['multi_round']['estimated_wall_seconds']))
    md = '\n'.join(lines) + '\n'
    print(md)
    if a.md:
        open(a.md, 'w', encoding='utf-8').write(md)
    if a.json:
        json.dump(out, open(a.json, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)


if __name__ == '__main__':
    main()
