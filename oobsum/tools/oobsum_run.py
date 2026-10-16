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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CHECKER = 'rb.enhance.ipa.OobSummary'
STUB = '/* oobsum_run.py 生成的桩 TU（06- X-04）：只当 Phase=solve 的宿主。 */\nvoid oobsum_solve_stub(void) {}\n'


def run(cmd, workdir, phase):
    """打印、执行，输出追加到 logs/<阶段>.log；返回退出码。"""
    text = ' '.join(shlex.quote(c) for c in cmd)
    print('$ ' + text, flush=True)
    os.makedirs(os.path.join(workdir, 'logs'), exist_ok=True)
    with open(os.path.join(workdir, 'logs', phase + '.log'), 'a', encoding='utf-8') as log:
        log.write('$ %s\n' % text)
        r = subprocess.run(cmd, capture_output=True, text=True)
        log.write(r.stdout + r.stderr + '# exit %d\n' % r.returncode)
    return r.returncode


def stop(phase, cmd):
    sys.exit('✗ %s 阶段失败（输出见 logs/%s.log）；修好后单独重跑这一段：\n  %s' % (
        phase, phase, ' '.join(shlex.quote(c) for c in cmd)))


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
        cmd.append('-analysis_folder=' + folder)  # COMPANY.md §4 的写法（多个目录用逗号）
    return cmd + ['-checker=' + CHECKER, '-extra_args=' + extra]


def cmd_bxen(a):
    workdir = os.path.abspath(a.workdir)
    os.makedirs(workdir, exist_ok=True)
    stub_dir, stub = write_stub(workdir)
    json.dump([{'directory': stub_dir, 'file': stub, 'arguments': ['cc', '-c', stub]}],
              open(os.path.join(stub_dir, 'compile_commands.json'), 'w', encoding='utf-8'), indent=1)
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
            print('$ ' + ' '.join(shlex.quote(c) for c in cmd))
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
    p = sub.add_parser('status')
    p.add_argument('--workdir', required=True)
    a = ap.parse_args()
    sys.exit({'local': cmd_local, 'bxen': cmd_bxen, 'status': cmd_status}[a.cmd](a))


if __name__ == '__main__':
    main()
