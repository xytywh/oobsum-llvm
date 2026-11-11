#!/usr/bin/env python3
"""跨 TU 的驱动（oobsum/specs/SPEC-0031；06- §7 X-04、X-05、X-10）：export → solve → report 三阶段。

  python3 oobsum/tools/oobsum_run.py local  --workdir W [--clang C] [--triple T] [--config K=V]… [--cc1-arg A]… 源文件…
  python3 oobsum/tools/oobsum_run.py bxen   --workdir W --bxen B --cc-json 原cdb目录 [--analysis-folder 目录,…] [--extra "…"] [--run]
  python3 oobsum/tools/oobsum_run.py status --workdir W
  python3 oobsum/tools/oobsum_run.py summary --workdir W --list [--contains 名字片段] [--counts-only]
  python3 oobsum/tools/oobsum_run.py solve --workdir W --bxen B --summary-function 函数名 [--summary-function 另一函数名] --run
  python3 oobsum/tools/oobsum_run.py solve --workdir W --bxen B --summary-all --run
  python3 oobsum/tools/oobsum_run.py summary --workdir W --function 函数名 --view local

local：本仓库的 clang 逐文件跑 cc1（参数同 run.py csa）；bxen：打印（--run 时执行）三段 bxen 命令，生成桩 TU 的 cdb 与 report 的 cdb。
每条命令先打印再执行，输出追加到 W/logs/<阶段>.log；非零退出即停，并提示单独重跑那一段。只依赖 Python 3 标准库。
summary（SPEC-0129）：FIR 中按目标与函数键盘点；查看 local（局部事实）/ derived（传播结果）视图。
空 index.json 表示没有保存摘要，不能据此判定没有分析函数。复用完整 FIR，精确选择函数重跑 solve 即可补出，
无需再次 export/report；保留原 solve 的模型与策略选项。函数名和摘要只在公司本地查看，带回用 --list --counts-only。
SPEC-0130：--summary-all 在本次 solve 保存全部摘要，之后换函数只需读取；需要新版 checker 的 Dump=summary-files。
全量会产生每函数两份JSON，增加磁盘/inode/写盘时间。--summary-function 仅保存少量，默认预读FIR核对名字；
已从对应FIR的funcs[].name确认名字时，可加 --skip-summary-precheck 跳过盘点，代价是错名只能在solve后发现。
--list 可选、--limit 仅限制展示行数，盘点仍读全体FIR；已有摘要可直接打开 index 与对应 JSON，无需任何脚本。
"""
import sys
sys.dont_write_bytecode = True
import argparse, glob, json, os, shlex, subprocess
if os.environ.pop('OOBSUM_CONFIG', None) is not None:  # 本工具自己传选项；用户为 clang_check 设的环境变量不能混进来（SPEC-0120）
    print('注意：忽略环境变量 OOBSUM_CONFIG', file=sys.stderr)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CHECKER = 'rb.enhance.core.OobSummary'
STUB = '/* oobsum_run.py 生成的桩 TU（06- X-04）：只当 Phase=solve 的宿主。 */\nvoid oobsum_solve_stub(void) {}\n'


def show(cmd):
    """打印用的命令行：-extra_args="…" 写成与 COMPANY.md 相同的样子（bash 与 IDE 的运行配置都能直接粘贴，SPEC-0115），其余按 shell 引用"""
    out = []
    for c in cmd:
        key, _, val = c.partition('=')
        if key == '-extra_args' and not any(ch in val for ch in '"$`\\!'):
            out.append('-extra_args="%s"' % val)
        else:
            out.append(shlex.quote(c))
    return ' '.join(out)


def run(cmd, workdir, phase):
    """打印、执行，输出追加到 logs/<阶段>.log；返回退出码。"""
    text = show(cmd)
    print('$ ' + text, flush=True)
    os.makedirs(os.path.join(workdir, 'logs'), exist_ok=True)
    with open(os.path.join(workdir, 'logs', phase + '.log'), 'a', encoding='utf-8') as log:
        log.write('$ %s\n' % text)
        log.flush()
        # SPEC-0123：合并管道避免死锁；每行立即展示、落盘，不能等 20 分钟的 solve 结束。
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, errors='replace', bufsize=1) as proc:
            for line in proc.stdout:
                print(line, end='', flush=True)
                log.write(line)
                log.flush()
            code = proc.wait()
        log.write('# exit %d\n' % code)
    return code


def stop(phase, cmd):
    sys.exit('✗ %s 阶段失败（输出见 logs/%s.log）；修好后单独重跑这一段：\n  %s' % (
        phase, phase, show(cmd)))


def solved(workdir):
    """W/solved/*.json → [(主文件, 判定条数)]"""
    out = []
    for p in sorted(glob.glob(os.path.join(workdir, 'solved', '*.json'))):
        try:
            d = json.load(open(p, encoding='utf-8'))
            out.append((d['file'], len(d['decisions'])))
        except (ValueError, KeyError):
            print('注意：读不了 %s' % p)
    return out


def write_stub(workdir):
    d = os.path.join(workdir, 'solve_stub')
    os.makedirs(d, exist_ok=True)
    stub = os.path.join(d, 'stub.c')
    open(stub, 'w', encoding='utf-8').write(STUB)
    return d, stub


def emitted_count(workdir):
    n = 0
    for p in glob.glob(os.path.join(workdir, 'decisions', '*.jsonl')):
        for line in open(p, encoding='utf-8'):
            if '"emitted":true' in line and json.loads(line).get('diagnostic_primary', True):
                n += 1
    return n


def cmd_local(a):
    workdir = os.path.abspath(a.workdir)
    os.makedirs(workdir, exist_ok=True)
    clang = a.clang or os.path.join(REPO, 'cmake-build-release', 'bin', 'clang')
    res = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()

    def cc1(phase, src):
        args = [clang, '-cc1', '-triple', a.triple, '-analyze', '-resource-dir', res, '-internal-isystem',
                os.path.join(res, 'include'), '-analyzer-checker=' + CHECKER, '-analyzer-config', 'ipa=none',
                '-analyzer-config', '%s:Phase=%s' % (CHECKER, phase), '-analyzer-config', '%s:WorkDir=%s' % (CHECKER, workdir)]
        if a.triple.startswith(('arm', 'thumb', 'aarch64')):
            args.append('-fno-signed-char')  # 同 run.py csa（SPEC-0007 §2.1）
        for kv in a.config:
            args += ['-analyzer-config', '%s:%s' % (CHECKER, kv)]
        return args + a.cc1_arg + [src]

    srcs = [os.path.abspath(s) for s in a.sources]
    for s in srcs:  # ① export
        cmd = cc1('export', s)
        if run(cmd, workdir, 'export') != 0:
            stop('export', cmd)
    _, stub = write_stub(workdir)  # ② solve
    cmd = cc1('solve', stub)
    if run(cmd, workdir, 'solve') != 0:
        stop('solve', cmd)
    table = solved(workdir)
    todo = [f for f, n in table if n > 0]  # ③ report：判定不为空的 TU
    for f in todo:
        cmd = cc1('report', f)
        if run(cmd, workdir, 'report') != 0:
            stop('report', cmd)
    print('export %d 个 TU；solve 判定 %d 条；report %d 个 TU，告警 %d 条' % (
        len(srcs), sum(n for _, n in table), len(todo), emitted_count(workdir)))
    return 0


def bxen_cmd(a, cc_json, phase, workdir, analysis_file=None, folder=None):
    extra = '-analyzer-config ipa=none -analyzer-config %s:Phase=%s -analyzer-config %s:WorkDir=%s' % (CHECKER, phase, CHECKER, workdir)
    if a.extra:
        extra += ' ' + a.extra
    if phase == 'solve' and getattr(a, '_summary_filter', None) is not None:
        extra += ' -analyzer-config %s:Dump=progress+%s -analyzer-config %s:DumpFilter=%s' % (
            CHECKER, getattr(a, '_summary_kind', 'summary'), CHECKER, a._summary_filter)
    cmd = [a.bxen, '-exec_mode=onlySa', '-cc_json=' + cc_json]
    if analysis_file:
        cmd.append('-analysis_file=' + analysis_file)
    elif folder:
        cmd.append('-analysis_folder=' + folder)  # 一条 bxen 命令跑几个目录（多个目录用逗号；试跑用，全量用 clang_check）
    return cmd + ['-checker=' + CHECKER, '-extra_args=' + extra]


def write_stub_cdb(workdir):
    """桩 TU 与只含它的 compile_commands.json（X-04）→ (目录, 桩文件)"""
    stub_dir, stub = write_stub(workdir)
    # command 写法：与公司 TestCase 的 compile_commands.json 相同（bxen 自己解析 -cc_json，SPEC-0120）
    json.dump([{'directory': stub_dir, 'file': stub, 'command': 'cc -c ' + stub}],
              open(os.path.join(stub_dir, 'compile_commands.json'), 'w', encoding='utf-8'), indent=1)
    return stub_dir, stub


def cmd_solve(a):
    """只做 solve（SPEC-0118）：export / report 两遍由 clang_check 按 cdb 每个文件一个进程并行跑，中间这一步单进程"""
    workdir = os.path.abspath(a.workdir)
    nfir = len(glob.glob(os.path.join(workdir, 'fir', '*.json')))
    if nfir == 0:
        print('✗ %s/fir/ 下没有文件：先用 clang_check 跑一遍 Phase=export（或带 WorkDir 的默认模式）' % workdir)
        return 1
    selected = getattr(a, 'summary_function', [])
    save_all = getattr(a, 'summary_all', False)
    skip_precheck = getattr(a, 'skip_summary_precheck', False)
    if (save_all and selected) or (skip_precheck and not selected):
        print('✗ --summary-all 与 --summary-function 互斥；--skip-summary-precheck 只能配 --summary-function', file=sys.stderr)
        return 1
    expected_summaries = None
    if selected:
        if skip_precheck:
            print('# 已跳过Python摘要预检；bxen仍读全部FIR，错名将在solve后发现')
        else:
            print('# 正在预读全部FIR核对函数名；已从对应文件确认名字时，可用--skip-summary-precheck省去这一遍', flush=True)
            entries, bad = _function_inventory(workdir)
            available = {e['fn'] for e in entries}
            missing = set(selected) - available
            if missing:
                print('✗ 有 %d 个函数名不在 FIR 中；尚未启动 bxen。可直接看对应FIR的funcs[].name，或用 summary --list；函数名不要加占位用的尖括号/反引号' %
                      len(missing), file=sys.stderr)
                return 1
            expected_summaries = {(e['target'], e['key']) for e in entries if e['fn'] in selected}
            print('# 摘要预检：匹配 %d 个函数（按目标与函数键计）；无法盘点的 FIR %d 个' % (
                sum(e['fn'] in selected for e in entries), bad))
        a._summary_filter = _literal_filter(selected)
    if save_all:
        a._summary_filter = '.*'  # SPEC-0130 / I-22：覆盖旧筛选；Clang不接受空的-analyzer-config值。
        a._summary_kind = 'summary-files'
        print('# 本次保存全部函数的local/derived摘要；增加磁盘空间、inode和写盘时间，不逐函数打印JSON', flush=True)
    stub_dir, stub = write_stub_cdb(workdir)
    cmd = bxen_cmd(a, stub_dir, 'solve', workdir, stub)
    print('# fir/ 里 %d 个 TU；solve 单进程读全部 FIR，写 solved/<tu>.json' % nfir)
    if not a.run:
        print('$ ' + show(cmd))
        return 0
    index_path = os.path.join(workdir, 'dump', 'solve', 'summary', 'index.json')
    previous_index = os.stat(index_path) if (selected or save_all) and os.path.isfile(index_path) else None
    status_path = os.path.join(os.path.dirname(index_path), 'status.json')
    previous_status = os.stat(status_path) if save_all and os.path.isfile(status_path) else None
    if run(cmd, workdir, 'solve') != 0:
        stop('solve', cmd)
    progress_path = os.path.join(workdir, 'dump', 'solve', 'progress.json')
    if os.path.isfile(progress_path):
        with open(progress_path, encoding='utf-8') as f:
            progress = json.load(f)
        if progress.get('stage') == 'failed':
            print('✗ solve 写结果失败；查看 logs/solve.log，修复后重新 solve', file=sys.stderr)
            return 1
    table = solved(workdir)
    if len(table) < nfir:
        print('注意：solved 比 FIR 少 %d 个 TU；查看 logs/solve.log 中跳过的坏 FIR 或写入错误' % (nfir - len(table)))
    print('solve 完成：solved/ %d 个 TU，判定 %d 条（%d 个 TU 有判定）；下一步 clang_check 跑 Phase=report' % (
        len(table), sum(n for _, n in table), sum(1 for _, n in table if n > 0)))
    if selected or save_all:
        try:
            current = os.stat(index_path)
            if previous_index and (current.st_ino, current.st_mtime_ns) == (previous_index.st_ino, previous_index.st_mtime_ns):
                raise ValueError('本次 solve 未更新摘要索引')
            entries = _summary_index(workdir)
            if expected_summaries is not None and not expected_summaries <= {(e.get('target'), e.get('key')) for e in entries}:
                raise ValueError('所选函数的部分目标或函数键没有保存到摘要索引')
            for fn in set(selected):
                hits = [e for e in entries if e.get('fn') == fn]
                if not hits:
                    raise ValueError('选中的函数没有保存到摘要索引')
                for e in hits:
                    for view in ('local', 'derived'):
                        _read_summary(workdir, e, view)
            if save_all:
                current_status = os.stat(status_path)
                if previous_status and (current_status.st_ino, current_status.st_mtime_ns) == (previous_status.st_ino, previous_status.st_mtime_ns):
                    raise ValueError('本次 solve 未更新摘要保存状态')
                with open(status_path, encoding='utf-8') as f:
                    status = json.load(f)
                if not (isinstance(status, dict) and status.get('index_written') is True and status.get('write_failures') == 0 and
                        status.get('available') == status.get('matched') == status.get('saved') == len(entries)):
                    raise ValueError('全量摘要的可用/匹配/保存计数不一致或有写盘失败')
                print('全部摘要已索引：%d 个函数；checker确认两种视图均写入，未再次遍历所有摘要正文。之后直接打开JSON或用 summary --function 查看' % len(entries))
            else:
                print('摘要已保存：%d 个函数，两种视图均可读；用 summary --function NAME --view local 或 derived 查看' % len(entries))
        except (OSError, ValueError, KeyError) as error:
            print('✗ solve 已结束，但请求的摘要不完整：%s；查看 logs/solve.log 的 summary 计数与写盘提示' % error, file=sys.stderr)
            return 1
    return 0


def cmd_bxen(a):
    workdir = os.path.abspath(a.workdir)
    os.makedirs(workdir, exist_ok=True)
    stub_dir, stub = write_stub_cdb(workdir)
    cmds = [('export', bxen_cmd(a, os.path.abspath(a.cc_json), 'export', workdir, folder=a.analysis_folder)),
            ('solve', bxen_cmd(a, stub_dir, 'solve', workdir, stub))]
    # report 的 cdb：从原 cdb 原样复制 solved 里判定不为空的 TU 的条目（X-05）；solve 之后才有
    table = solved(workdir)
    if table:
        want = {os.path.normpath(f) for f, n in table if n > 0}
        cdb = json.load(open(os.path.join(a.cc_json, 'compile_commands.json'), encoding='utf-8'))
        keep = [e for e in cdb if os.path.normpath(os.path.join(e.get('directory', ''), e['file'])) in want]
        rdir = os.path.join(workdir, 'report_cdb')
        os.makedirs(rdir, exist_ok=True)
        json.dump(keep, open(os.path.join(rdir, 'compile_commands.json'), 'w', encoding='utf-8'), indent=1)
        cmds.append(('report', bxen_cmd(a, rdir, 'report', workdir, folder=a.analysis_folder)))
        print('# report 的 cdb：%d 个 TU（原 cdb %d 个）→ %s' % (len(keep), len(cdb), rdir))
    else:
        print('# 还没有 solved/：先跑 export、solve，再用同一命令重跑一次生成 report 那段')
    print('# 全量模式（不带 -analysis_file）的写法待确认：见 oobsum/specs/SPEC-0031 §8')
    for phase, cmd in cmds:
        if a.run:
            if run(cmd, workdir, phase) != 0:
                stop(phase, cmd)
        else:
            print('$ ' + show(cmd))
    return 0


def cmd_status(a):
    workdir = os.path.abspath(a.workdir)
    fir = glob.glob(os.path.join(workdir, 'fir', '*.json'))
    dec = glob.glob(os.path.join(workdir, 'decisions', '*.jsonl'))
    progress = None
    progress_path = os.path.join(workdir, 'dump', 'solve', 'progress.json')
    if os.path.isfile(progress_path):
        try:
            with open(progress_path, encoding='utf-8') as f:
                progress = json.load(f)
        except (OSError, ValueError):
            print('注意：暂时读不了 solve 进度文件')
    # 求解未结束时不能为看进度而反复解析数 GB 的 solved 判定（SPEC-0123）。
    unfinished = progress is not None and progress.get('stage') not in ('done', 'failed')
    if unfinished:
        print('fir/ %d 个；solved/ %d 个（判定不为空：快照尚未完成，未统计）；decisions/ %d 个' % (
            len(fir), len(glob.glob(os.path.join(workdir, 'solved', '*.json'))), len(dec)))
    else:
        table = solved(workdir)
        print('fir/ %d 个；solved/ %d 个（判定不为空 %d 个）；decisions/ %d 个' % (
            len(fir), len(table), sum(1 for _, n in table if n > 0), len(dec)))
    if progress is not None:
        print('solve 进度：%s %s/%s，已用 %s 秒，pid=%s（阶段内计数，不是剩余时间）' % (
            progress.get('stage'), progress.get('completed'), progress.get('total'),
            progress.get('elapsed_s'), progress.get('pid')))
    last = None
    for phase in ['export', 'solve', 'report']:
        p = os.path.join(workdir, 'logs', phase + '.log')
        if os.path.exists(p):
            codes = [l for l in open(p, encoding='utf-8') if l.startswith('# exit ')]
            bad = [c for c in codes if c.strip() != '# exit 0']
            print('%s：%d 条命令%s' % (phase, len(codes), '，失败 %d 条' % len(bad) if bad else ''))
            last = phase
    nxt = {None: '跑 export', 'export': '跑 solve', 'solve': '跑 report', 'report': '完成'}[last]
    if not fir:
        nxt = '跑 export'
    print('下一步：' + nxt)
    return 0


def _decisions(workdir):
    """WorkDir/decisions/*.jsonl 的全部判定（每条加 _tu：所在 TU 的源文件）"""
    out = []
    for f in sorted(glob.glob(os.path.join(workdir, 'decisions', '*.jsonl'))):
        lines = [json.loads(l) for l in open(f, encoding='utf-8') if l.strip()]
        if not lines or lines[0].get('schema') != 'oobsum-decisions/1':
            continue
        for d in lines[1:]:
            d['_tu'] = lines[0].get('file', '')
            d['_tuid'] = lines[0].get('tu', '')
            out.append(d)
    return out


def _name(fn):
    return fn.split(':')[-1]


def _targets(workdir):
    """W/fir/*.json 的 target（分析时用的目标：三元组、指针 / long 字节数、大小端）→ {显示文本: TU 数}（SPEC-0116）。
    target 在缩进写的文件末尾附近，只解析那一段，不读整个 FIR"""
    counts = {}
    dec = json.JSONDecoder()
    for p in glob.glob(os.path.join(workdir, 'fir', '*.json')):
        text = open(p, encoding='utf-8').read()
        i = text.rfind('"target"')
        try:
            t = dec.raw_decode(text, text.index('{', i))[0] if i >= 0 else None
        except ValueError:
            t = None
        key = ('%s ptr=%s long=%s %s' % (t.get('triple'), t.get('ptr'), t.get('long'),
                                         'big-endian' if t.get('big_endian') else 'little-endian')) if t else '（读不出 target）'
        counts[key] = counts.get(key, 0) + 1
    return counts


def cmd_stats(a):
    """stats（06- I-30，SPEC-0112）：报出的按标签 × 层数、SUSPECT 按原因、没报的按原因、报出最多的函数与 TU"""
    targets = _targets(a.workdir)
    for t, n in sorted(targets.items(), key=lambda x: (-x[1], x[0])):
        print('目标：%s（%d 个 TU）' % (t, n))
    if not targets:
        print('目标：（%s 下没有 fir/*.json）' % a.workdir)
    ds = _decisions(a.workdir)
    if not ds:
        print('%s 下没有判定日志' % a.workdir)
        return 1
    emitted = [d for d in ds if d.get('emitted')]
    physical = sum(d.get('diagnostic_primary', True) for d in emitted)
    print('== %d 条判定，报出 %d 条（%d 个 TU）' % (len(ds), len(emitted), len({d['_tu'] for d in ds})))
    print('物理告警 %d 条（同一 sink 行合并；上面的报出数按原始判定计）' % physical)
    buckets = ['0', '1', '2', '3', '4+']
    def bucket(d):
        n = max(d.get('depth', 0), d.get('eff_depth', 0))
        return buckets[min(n, 4)]
    print('\n报出：标签 × 层数（max(depth, eff_depth)）')
    print('%-10s' % '' + ''.join('%7s' % b for b in buckets) + '%8s' % '合计')
    for label in ['definite', 'possible', 'suspect']:
        row = [d for d in emitted if d['label'] == label]
        print('%-10s' % label + ''.join('%7d' % sum(1 for d in row if bucket(d) == b) for b in buckets) + '%8d' % len(row))
    def table(title, rows, key):
        counts = {}
        for d in rows:
            counts[key(d)] = counts.get(key(d), 0) + 1
        print('\n' + title)
        for k, n in sorted(counts.items(), key=lambda x: (-x[1], x[0]))[:a.top]:
            print('  %6d  %s' % (n, k))
    table('报出的 SUSPECT：按原因', [d for d in emitted if d['label'] == 'suspect'], lambda d: d.get('reason', '-'))
    table('没报的：按原因（not_emitted_because）', [d for d in ds if not d.get('emitted')],
          lambda d: '%s / %s' % (d['label'], d.get('not_emitted_because', '-')))
    if a.counts_only:
        print('\n（--counts-only：不列函数名 / 文件名，可以直接带回，SPEC-0119）')
        return 0
    table('报出最多的函数（前 %d）' % a.top, emitted, lambda d: _name(d.get('fn', '?')))
    table('报出最多的 TU（前 %d）' % a.top, emitted, lambda d: os.path.basename(d['_tu']))
    return 0


def cmd_report(a):
    """clang_check 的 report.json（SPEC-0120）：本规则的告警按标签、SUSPECT 按原因计数——只有数字，可以带回"""
    import re
    path = a.path
    if os.path.isdir(path):
        candidates = sorted(glob.glob(os.path.join(path, '*report.json')))
        if len(candidates) != 1:
            print('✗ 报告目录有 %d 个 *report.json；请直接指定 JSON 文件路径' % len(candidates), file=sys.stderr)
            return 1
        path = candidates[0]
    try:
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
    except (OSError, ValueError) as error:
        print('✗ 无法读取报告 JSON：%s' % error, file=sys.stderr)
        return 1
    probs = [p for p in (data.get('problems', []) if isinstance(data, dict) else []) if isinstance(p, dict) and p]
    names = (CHECKER, 'rb.enhance.ipa.OobSummary')  # SPEC-0123：公司已有旧包报告也能统计
    mine = [p for p in probs if any(p.get('description') == name or '[%s]' % name in str(p.get('reason', ''))
                                  for name in names)]
    labels, reasons = {}, {}
    for p in mine:
        m = re.search(r'\[OOB-([A-Z]+)(?::([A-Za-z0-9_]+))?\]', str(p.get('reason', '')))
        label = m.group(1) if m else '?'
        labels[label] = labels.get(label, 0) + 1
        if label == 'SUSPECT':
            reasons[m.group(2) or '-'] = reasons.get(m.group(2) or '-', 0) + 1
    glued = sum(len(re.findall(r'warning:\s*\[OOB-(?:DEFINITE|POSSIBLE|SUSPECT|SAFE)', str(p.get('reason', '')))) > 1 for p in mine)
    order = ['DEFINITE', 'POSSIBLE', 'SUSPECT', 'SAFE', '?']
    print('%s：problems %d 条，其中 %s %d 条：%s' % (os.path.basename(path), len(probs), CHECKER, len(mine),
                                              '、'.join('%s %d' % (k, labels[k]) for k in order if k in labels) or '无'))
    if reasons:
        print('  SUSPECT 按原因：' + '、'.join('%s %d' % kv for kv in sorted(reasons.items(), key=lambda x: (-x[1], x[0]))))
    if glued:
        print('注意：%d 条报告记录含多个 warning，旧收集结果可能粘连；需用新 sink 合并格式重跑核对' % glued)
    return 0


def cmd_cdb(a):
    """看 / 过滤编译数据库（SPEC-0119）：条目数、文件数、出现多次的文件数；--under 只留这些目录下的文件，--out 写过滤后的 compile_commands.json"""
    src = os.path.join(a.cc_json, 'compile_commands.json') if os.path.isdir(a.cc_json) else a.cc_json
    cdb = json.load(open(src, encoding='utf-8'))

    def path_of(e):
        return os.path.normpath(os.path.join(e.get('directory', ''), e['file']))
    unders = [os.path.normpath(os.path.abspath(u)) + os.sep for u in a.under]
    keep = [e for e in cdb if not unders or any(path_of(e).startswith(u) for u in unders)]
    counts = {}
    for e in keep:
        counts[path_of(e)] = counts.get(path_of(e), 0) + 1
    print('%s：%d 条%s，文件 %d 个，出现多次的文件 %d 个' % (
        src, len(cdb), '（过滤后 %d 条）' % len(keep) if unders else '', len(counts), sum(1 for n in counts.values() if n > 1)))
    # 按扩展名数语言（C++ 的 TU 照常分析，C++ 特有的写法保守处理，SPEC-0121）
    lang = {'C（.c）': 0, 'C++（.cpp / .cc / .cxx）': 0, '其它': 0}
    for f in counts:
        ext = os.path.splitext(f)[1]
        key = 'C（.c）' if ext == '.c' else 'C++（.cpp / .cc / .cxx）' if ext in ('.cpp', '.cc', '.cxx', '.c++', '.C') else '其它'
        lang[key] += 1
    print('  文件按语言：' + '、'.join('%s %d 个' % kv for kv in lang.items()))
    # SPEC-0124：仅靠主文件路径的 TuId 无法保存多个编译变体，先给可带回的计数。
    variants = {}
    for e in keep:
        try:
            args = e.get('arguments') or shlex.split(e.get('command', ''))
        except ValueError:
            args = [e.get('command', '')]
        norm = []
        i = 0
        while i < len(args):
            if args[i] in ('-o', '-MF', '-MT', '-MQ'):
                i += 2
                continue
            norm.append(args[i])
            i += 1
        variants.setdefault(path_of(e), set()).add((e.get('directory', ''), tuple(norm)))
    print('  去掉输出文件选项后编译命令仍不同的文件：%d 个（可能有不同预处理 / ABI 配置；不同有效FIR留存，固定选择一份，SPEC-0139）' %
          sum(1 for choices in variants.values() if len(choices) > 1))
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        out = os.path.join(os.path.abspath(a.out), 'compile_commands.json')
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(keep, f, indent=1, ensure_ascii=False)
        print('已写 %s：%d 条' % (out, len(keep)))
    return 0


def _function_inventory(workdir):
    """SPEC-0129 / F-42：逐 TU 盘点，不同时保存整板 FIR；口径与 solve 的目标分组、函数键去重一致。"""
    entries, bad = {}, 0
    for path in sorted(glob.glob(os.path.join(workdir, 'fir', '*.json'))):
        try:
            with open(path, encoding='utf-8') as f:
                module = json.load(f)
            t = module['target']
            target = '%s ptr=%s long=%s %s-endian' % (
                t['triple'], t['ptr'], t['long'], 'big' if t['big_endian'] else 'little')
            for fn in module['funcs']:
                entry = {'fn': fn['name'], 'key': fn['key'], 'target': target, 'tu': module['tu']}
                entries.setdefault((target, fn['key']), entry)
        except (OSError, ValueError, KeyError, TypeError):
            bad += 1
    return sorted(entries.values(), key=lambda e: (e['fn'], e['target'], e['key'])), bad


def _literal_filter(names):
    """LLVM Regex 用 POSIX ERE；不可用 Python re.escape 的空白转义交给 bxen 分词。"""
    def escape(name):
        return ''.join('[[:space:]]' if c.isspace() else '\\' + c if c in r'\.^$|?*+()[]{}' else c for c in name)
    return '^(' + '|'.join(escape(name) for name in sorted(set(names))) + ')$'


def _summary_index(workdir):
    with open(os.path.join(workdir, 'dump', 'solve', 'summary', 'index.json'), encoding='utf-8') as f:
        entries = json.load(f)
    if not isinstance(entries, list) or any(not isinstance(e, dict) for e in entries):
        raise ValueError('摘要索引格式不正确')
    return entries


def _read_summary(workdir, entry, view):
    stem = entry['stem']
    if not stem or os.path.basename(stem) != stem or stem in ('.', '..'):
        raise ValueError('摘要索引包含无效 stem')
    with open(os.path.join(workdir, 'dump', 'solve', 'summary', stem + '.' + view + '.json'), encoding='utf-8') as f:
        return json.load(f)


def cmd_summary(a):
    """先盘点实际函数，再查本次 solve 的索引；不从旧的残留文件猜摘要。"""
    if a.list:
        if not a.counts_only:
            print('正在盘点全部FIR；--contains/--limit只限制展示。也可直接打开对应FIR的funcs[].name/key，无需此脚本', flush=True)
        entries, bad = _function_inventory(a.workdir)
        hits = [e for e in entries if not a.contains or a.contains in e['fn']]
        try:
            saved = len(_summary_index(a.workdir))
        except (OSError, ValueError):
            saved = 0
        print('FIR 函数 %d；筛选匹配 %d；已索引摘要 %d；无法盘点的 FIR %d' % (len(entries), len(hits), saved, bad))
        if not a.counts_only:
            for e in hits[:max(0, a.limit)]:
                print('%s  key=%s  target=%s' % (e['fn'], e['key'], e['target']))
            if len(hits) > max(0, a.limit):
                print('只列前 %d 个；用 --contains 缩小范围或 --limit 增加显示数量' % max(0, a.limit))
        return 0 if hits else 1
    try:
        entries = _summary_index(a.workdir)
        hits = [e for e in entries if a.function in (e.get('fn'), e.get('key')) and
                (not a.target or a.target == e.get('target'))]
        if not hits:
            raise ValueError('索引没有该函数；空索引可能是 DumpFilter 零匹配。先 summary --list，再 solve --summary-function NAME --run；可复用 FIR，无需 export/report')
        if len(hits) > 1:
            raise ValueError('有 %d 个同名函数；用 --function 指定函数键，并用 --target 指定目标（从 --list 查询）' % len(hits))
        print(json.dumps(_read_summary(a.workdir, hits[0], a.view), indent=1, ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print('✗ 无法查看摘要：%s' % error, file=sys.stderr)
        return 1


def _summary_of(workdir, tuid, fn):
    """优先读全局索引；无索引时兼容 dump/<tu>/summary，缺失或歧义返回 None。"""
    index_path = os.path.join(workdir, 'dump', 'solve', 'summary', 'index.json')
    if os.path.isfile(index_path):
        entries = _summary_index(workdir)
        hits = [e for e in entries if e.get('key') == fn or e.get('fn') == fn]
        # SPEC-0124：同名跨目标函数必须按当前判定所在目标选摘要，不能任取第一个。
        targets = {e.get('target') for e in entries if e.get('tu') == tuid and e.get('target')}
        if len(targets) == 1:
            hits = [e for e in hits if e.get('target') in targets]
        if len(hits) == 1:
            return _read_summary(workdir, hits[0], 'local')
        return None  # SPEC-0129：有全局索引时，不能回退到其它轮次留下的 TU 局部事实。
    p = os.path.join(workdir, 'dump', tuid, 'summary', fn + '.json')
    if not os.path.isfile(p):
        hits = glob.glob(os.path.join(workdir, 'dump', '*', 'summary', fn + '.json'))
        p = hits[0] if len(hits) == 1 else None
    return json.load(open(p, encoding='utf-8')) if p else None


def cmd_explain(a):
    """explain <指纹 | 函数名>（06- F-42，SPEC-0112）：判定的来龙去脉——标签、原因、文字、末端与大小、调用链；
    带 Dump=summary 跑过时，链上每个函数的摘要（Req 与调用点的实参）"""
    ds = _decisions(a.workdir)
    hits = [d for d in ds if d.get('fp') == a.what or d.get('report_group') == a.what or
            _name(d.get('fn', '')) == a.what or any(h.split('@', 1)[0] == a.what for h in d.get('chain', []))]
    if not a.all:
        hits = [d for d in hits if d['label'] != 'safe' or d.get('fp') == a.what]
    if not hits:
        print('没有指纹或函数名是 %s 的判定（加 --all 连 SAFE 一起找）' % a.what)
        return 1
    missing_summaries = set()
    for d in hits:
        print('== [%s%s] %s  指纹 %s' % (d['label'].upper(), ':' + d['reason'] if d.get('reason') else '',
                                       d.get('site', '?'), d.get('fp', '?')))
        print('   %s' % d.get('msg', ''))
        if d.get('sink'):
            print('   sink %s；证明上下文 %s；告警分组 %s' % (d['sink'], d.get('site'), d.get('report_group', '-')))
        print('   函数 %s；%s；末端 %s；大小 %s；来源 %s；层数 %s / %s%s' % (
            _name(d.get('fn', '?')), '报出' if d.get('emitted') else '没报（%s）' % d.get('not_emitted_because', '-'),
            d.get('end', '-'), d.get('have', '-'), d.get('bound', '-'), d.get('depth', 0), d.get('eff_depth', 0),
            '；同一次访问已报的：%s' % d['dup_of'] if d.get('dup_of') else ''))
        chain = d.get('chain', [])
        for i, hop in enumerate(chain):
            print('   %s%s' % ('  ' * i + ('└ ' if i else ''), hop))
        for hop in chain:
            fn = hop.split('@', 1)[0]
            try:
                sm = _summary_of(a.workdir, d.get('_tuid', ''), fn)
            except (OSError, ValueError, KeyError):
                sm = None
            if sm is None:
                missing_summaries.add(fn)
                continue
            params = ', '.join('p%d=%s' % (i, p.get('name', '?')) for i, p in enumerate(sm.get('params', [])))
            print('   -- 摘要 %s（%s）' % (fn, params))
            for r in sm.get('reqs', []):
                print('      %s %s%s 需 [%s, %s] <= %s%s  @%s' % (
                    r.get('kind'), r.get('obj'), ' (' + r['dir'] + ')' if r.get('dir') else '', r['end'][0], r['end'][1],
                    r.get('have'), '  守卫 ' + ' ∧ '.join(r['guard']) if r.get('guard') else '', r.get('access', {}).get('at', '?')))
            for c in sm.get('calls', []):
                locs = ', '.join('%s∈[%s, %s]' % (k, v['range'][0], v['range'][1]) for k, v in sorted((c.get('locals') or {}).items())
                                 if isinstance(v, dict) and len(v.get('range', [])) == 2)
                print('      调用 %s @%s 实参 %s%s' % (c.get('callee'), c.get('site'), json.dumps(c.get('args', []), ensure_ascii=False),
                                               '；局部 ' + locs if locs else ''))
    if missing_summaries:
        print('（链上 %d 个函数没有可唯一读取的摘要；先 summary --list 查函数名，再 solve --summary-function NAME --run 补出；可复用 FIR）' % len(missing_summaries))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('local')
    p.add_argument('--workdir', required=True)
    p.add_argument('--clang')
    p.add_argument('--triple', default='armebv7-linux-gnueabi')
    p.add_argument('--config', action='append', default=[], metavar='K=V')
    p.add_argument('--cc1-arg', action='append', default=[])
    p.add_argument('sources', nargs='+')
    p = sub.add_parser('bxen')
    p.add_argument('--workdir', required=True)
    p.add_argument('--bxen', required=True)
    p.add_argument('--cc-json', required=True)
    p.add_argument('--extra', default='')
    p.add_argument('--analysis-folder', help='export / report 两段带 -analysis_folder=（逗号分隔的目录）')
    p.add_argument('--run', action='store_true')
    p = sub.add_parser('solve', help='只做 solve：export / report 由 clang_check 并行跑（SPEC-0118）')
    p.add_argument('--workdir', required=True)
    p.add_argument('--bxen', required=True)
    p.add_argument('--extra', default='')
    p.add_argument('--run', action='store_true')
    summary_mode = p.add_mutually_exclusive_group()
    summary_mode.add_argument('--summary-function', action='append', default=[], metavar='NAME', help='精确选择显示函数名；默认先查FIR，可重复（SPEC-0129）')
    summary_mode.add_argument('--summary-all', action='store_true', help='本次保存全部摘要，日后只读查询；需要新版checker（SPEC-0130）')
    p.add_argument('--skip-summary-precheck', action='store_true', help='只配--summary-function：跳过Python全量FIR盘点，错名在solve后发现')
    p = sub.add_parser('summary', help='盘点 FIR 中的函数，或读取本次 solve 保存的函数摘要（SPEC-0129）')
    p.add_argument('--workdir', required=True)
    choice = p.add_mutually_exclusive_group(required=True)
    choice.add_argument('--list', action='store_true', help='从 FIR 列出实际函数，不依赖摘要索引')
    choice.add_argument('--function', metavar='NAME', help='显示函数名或函数键')
    p.add_argument('--contains', help='--list 的函数名子串筛选')
    p.add_argument('--limit', type=int, default=20, help='--list 最多显示的函数数，默认20')
    p.add_argument('--counts-only', action='store_true', help='--list 只给计数，可带回')
    p.add_argument('--target', help='指定完整目标字符串，消除同名歧义')
    p.add_argument('--view', choices=('local', 'derived'), default='local')
    p = sub.add_parser('status')
    p.add_argument('--workdir', required=True)
    p = sub.add_parser('stats', help='报出的按标签 × 层数、SUSPECT 按原因、没报的按原因、报出最多的函数 / TU')
    p.add_argument('--workdir', required=True)
    p.add_argument('--top', type=int, default=10)
    p.add_argument('--counts-only', action='store_true', help='只打印计数，不列函数名 / 文件名（带回用）')
    p = sub.add_parser('report', help='clang_check 的 report.json：本规则的告警按标签 / 原因计数（SPEC-0120）')
    p.add_argument('path', help='report.json 的路径')
    p = sub.add_parser('cdb', help='看 / 过滤 compile_commands.json：条目数、文件数、重复；--under 过滤、--out 写出（SPEC-0119）')
    p.add_argument('--cc-json', required=True, help='compile_commands.json 所在的目录（或文件本身）')
    p.add_argument('--under', action='append', default=[], metavar='DIR', help='只留这个目录下的文件（可给多个）')
    p.add_argument('--out', help='把过滤后的写到 <OUT>/compile_commands.json')
    p = sub.add_parser('explain', help='一条判定（指纹）或一个函数的判定的来龙去脉')
    p.add_argument('--workdir', required=True)
    p.add_argument('--all', action='store_true', help='连 SAFE 一起列')
    p.add_argument('what', help='判定指纹，或函数名')
    a = ap.parse_args()
    sys.exit({'local': cmd_local, 'bxen': cmd_bxen, 'solve': cmd_solve, 'cdb': cmd_cdb, 'report': cmd_report, 'status': cmd_status, 'stats': cmd_stats,
              'explain': cmd_explain, 'summary': cmd_summary}[a.cmd](a))


if __name__ == '__main__':
    main()
