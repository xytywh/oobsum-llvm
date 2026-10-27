#!/usr/bin/env python3
"""跨 TU 的驱动（oobsum/specs/SPEC-0031；06- §7 X-04、X-05、X-10）：export → solve → report 三阶段。

  python3 oobsum/tools/oobsum_run.py local  --workdir W [--clang C] [--triple T] [--config K=V]… [--cc1-arg A]… 源文件…
  python3 oobsum/tools/oobsum_run.py bxen   --workdir W --bxen B --cc-json 原cdb目录 [--analysis-folder 目录,…] [--extra "…"] [--run]
  python3 oobsum/tools/oobsum_run.py status --workdir W

local：本仓库的 clang 逐文件跑 cc1（参数同 run.py csa）；bxen：打印（--run 时执行）三段 bxen 命令，生成桩 TU 的 cdb 与 report 的 cdb。
每条命令先打印再执行，输出追加到 W/logs/<阶段>.log；非零退出即停，并提示单独重跑那一段。只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True
import argparse, glob, json, os, shlex, subprocess
if os.environ.pop('OOBSUM_CONFIG', None) is not None:  # 本工具自己传选项；用户为 clang_check 设的环境变量不能混进来（SPEC-0120）
    print('注意：忽略环境变量 OOBSUM_CONFIG', file=sys.stderr)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CHECKER = 'rb.enhance.ipa.OobSummary'
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
        r = subprocess.run(cmd, capture_output=True, text=True)
        log.write(r.stdout + r.stderr + '# exit %d\n' % r.returncode)
    return r.returncode


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
            if '"emitted":true' in line:
                n += 1
    return n


def cmd_local(a):
    workdir = os.path.abspath(a.workdir)
    os.makedirs(workdir, exist_ok=True)
    clang = a.clang or os.path.join(REPO, 'cmake-build-release', 'bin', 'clang')
    res = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()

    def cc1(phase, src):
        args = [clang, '-cc1', '-triple', a.triple, '-analyze', '-resource-dir', res, '-internal-isystem',
                os.path.join(res, 'include'), '-analyzer-checker=' + CHECKER,
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
    extra = '-analyzer-config %s:Phase=%s -analyzer-config %s:WorkDir=%s' % (CHECKER, phase, CHECKER, workdir)
    if a.extra:
        extra += ' ' + a.extra
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
    stub_dir, stub = write_stub_cdb(workdir)
    cmd = bxen_cmd(a, stub_dir, 'solve', workdir, stub)
    print('# fir/ 里 %d 个 TU；solve 单进程读全部 FIR，写 solved/<tu>.json' % nfir)
    if not a.run:
        print('$ ' + show(cmd))
        return 0
    if run(cmd, workdir, 'solve') != 0:
        stop('solve', cmd)
    table = solved(workdir)
    print('solve 完成：solved/ %d 个 TU，判定 %d 条（%d 个 TU 有判定）；下一步 clang_check 跑 Phase=report' % (
        len(table), sum(n for _, n in table), sum(1 for _, n in table if n > 0)))
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
    table = solved(workdir)
    dec = glob.glob(os.path.join(workdir, 'decisions', '*.jsonl'))
    print('fir/ %d 个；solved/ %d 个（判定不为空 %d 个）；decisions/ %d 个' % (
        len(fir), len(table), sum(1 for _, n in table if n > 0), len(dec)))
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
    print('== %d 条判定，报出 %d 条（%d 个 TU）' % (len(ds), len(emitted), len({d['_tu'] for d in ds})))
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
    data = json.load(open(a.path, encoding='utf-8'))
    probs = [p for p in (data.get('problems', []) if isinstance(data, dict) else []) if isinstance(p, dict) and p]
    mine = [p for p in probs if p.get('description') == CHECKER or '[%s]' % CHECKER in str(p.get('reason', ''))]
    labels, reasons = {}, {}
    for p in mine:
        m = re.search(r'\[OOB-([A-Z]+)(?::([A-Za-z0-9_]+))?\]', str(p.get('reason', '')))
        label = m.group(1) if m else '?'
        labels[label] = labels.get(label, 0) + 1
        if label == 'SUSPECT':
            reasons[m.group(2) or '-'] = reasons.get(m.group(2) or '-', 0) + 1
    order = ['DEFINITE', 'POSSIBLE', 'SUSPECT', 'SAFE', '?']
    print('%s：problems %d 条，其中 %s %d 条：%s' % (os.path.basename(a.path), len(probs), CHECKER, len(mine),
                                              '、'.join('%s %d' % (k, labels[k]) for k in order if k in labels) or '无'))
    if reasons:
        print('  SUSPECT 按原因：' + '、'.join('%s %d' % kv for kv in sorted(reasons.items(), key=lambda x: (-x[1], x[0]))))
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
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        out = os.path.join(os.path.abspath(a.out), 'compile_commands.json')
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(keep, f, indent=1, ensure_ascii=False)
        print('已写 %s：%d 条' % (out, len(keep)))
    return 0


def _summary_of(workdir, tuid, fn):
    """dump/<tu>/summary/<函数名>.json（Dump=summary 写的）；没有就 None"""
    p = os.path.join(workdir, 'dump', tuid, 'summary', fn + '.json')
    if not os.path.isfile(p):
        hits = glob.glob(os.path.join(workdir, 'dump', '*', 'summary', fn + '.json'))
        p = hits[0] if len(hits) == 1 else None
    return json.load(open(p, encoding='utf-8')) if p else None


def cmd_explain(a):
    """explain <指纹 | 函数名>（06- F-42，SPEC-0112）：判定的来龙去脉——标签、原因、文字、末端与大小、调用链；
    带 Dump=summary 跑过时，链上每个函数的摘要（Req 与调用点的实参）"""
    ds = _decisions(a.workdir)
    hits = [d for d in ds if d.get('fp') == a.what or _name(d.get('fn', '')) == a.what]
    if not a.all:
        hits = [d for d in hits if d['label'] != 'safe' or d.get('fp') == a.what]
    if not hits:
        print('没有指纹或函数名是 %s 的判定（加 --all 连 SAFE 一起找）' % a.what)
        return 1
    for d in hits:
        print('== [%s%s] %s  指纹 %s' % (d['label'].upper(), ':' + d['reason'] if d.get('reason') else '',
                                       d.get('site', '?'), d.get('fp', '?')))
        print('   %s' % d.get('msg', ''))
        print('   函数 %s；%s；末端 %s；大小 %s；来源 %s；层数 %s / %s%s' % (
            _name(d.get('fn', '?')), '报出' if d.get('emitted') else '没报（%s）' % d.get('not_emitted_because', '-'),
            d.get('end', '-'), d.get('have', '-'), d.get('bound', '-'), d.get('depth', 0), d.get('eff_depth', 0),
            '；同一次访问已报的：%s' % d['dup_of'] if d.get('dup_of') else ''))
        chain = d.get('chain', [])
        for i, hop in enumerate(chain):
            print('   %s%s' % ('  ' * i + ('└ ' if i else ''), hop))
        for hop in chain:
            fn = hop.split('@', 1)[0]
            sm = _summary_of(a.workdir, d.get('_tuid', ''), fn)
            if sm is None:
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
    if not glob.glob(os.path.join(a.workdir, 'dump', '*', 'summary')):
        print('（WorkDir 里没有 dump/*/summary：跑的时候加 Dump=summary 才能看到链上各函数的摘要）')
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
              'explain': cmd_explain}[a.cmd](a))


if __name__ == '__main__':
    main()
