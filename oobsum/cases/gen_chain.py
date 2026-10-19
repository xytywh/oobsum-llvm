#!/usr/bin/env python3
# 链长用例生成与检查（06- T-04、G1；SPEC-0113）。
#
# 四类模板 × N（入口到访问点经过的调用边数）：
#   1 形参透传     F0(p, i) { p[i] = 0; }、F_k(p, i) { F_{k-1}(p, i); }；入口 a[16]，TP 传 16、TN 传 15
#   2 偏移累加     F_k(p, i) { F_{k-1}(p + 1, i); }；入口 a[32]，TP / TN 的下标按累加的偏移算到恰好越 1 字节 / 恰好不越
#   3 封装分配器链 A0(n) { return malloc(n); }、A_k(n) { return A_{k-1}(n); }；入口 p = A_{N-1}(16)，TP 写 p[16]、TN 写 p[15]
#   4 跨 TU 链     同模板 1，每个函数一个文件（三阶段 export → solve → report）
# G1 的口径：模板 1、2、4 在 N >= 3 时 TP 报 DEFINITE、N = 2 时不报（层数 ≤ InlineDepth，日志里 inline_depth）；所有 TN 不报 DEFINITE / POSSIBLE。
# 模板 3（尽力）：访问点就在入口里，层数看 eff_depth（经过几层 ret_new）。
#
#   python3 oobsum/cases/gen_chain.py --out /tmp/chains                 # 只生成
#   python3 oobsum/cases/gen_chain.py --out /tmp/chains --check         # 生成并逐个跑、按 G1 口径判（退出码 1 = 有不符合的）
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CHECKER = 'rb.enhance.ipa.OobSummary'
HEAD = 'typedef unsigned char UINT8;\ntypedef unsigned int UINT32;\n'
NS = [2, 3, 5, 10, 17, 20]


def chain(n, step, buf, tp_idx, tn_idx):
    """模板 1 / 2：F0 访问，F1…F_{n-1} 往下传（step = 每层加的偏移），入口 Top_TP / Top_TN 调 F_{n-1}"""
    fns = ['static void F0(UINT8 *p, UINT32 i) { p[i] = 0; }']
    for k in range(1, n):
        fns.append('static void F%d(UINT8 *p, UINT32 i) { F%d(p%s, i); }' % (k, k - 1, ' + %d' % step if step else ''))
    fns.append('void Top_TP(void) { UINT8 a[%d]; F%d(a, %d); }' % (buf, n - 1, tp_idx))
    fns.append('void Top_TN(void) { UINT8 a[%d]; F%d(a, %d); }' % (buf, n - 1, tn_idx))
    return HEAD + '\n'.join(fns) + '\n'


def gen(out):
    """返回 [(模板, N, [源文件], 跨 TU)]"""
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    items = []
    for n in NS:
        # 模板 1：入口经 n 条调用边到 F0（入口调 F_{n-1}）
        p = os.path.join(out, 't1_n%d.c' % n)
        open(p, 'w').write(chain(n, 0, 16, 16, 15))
        items.append((1, n, [p], False))
        # 模板 2：每层 +1，F0 收到的偏移是 n − 1；末端 = (n − 1) + i + 1
        p = os.path.join(out, 't2_n%d.c' % n)
        open(p, 'w').write(chain(n, 1, 32, 32 - (n - 1), 31 - (n - 1)))
        items.append((2, n, [p], False))
        # 模板 3：A0 调 malloc，A_k 透传；入口拿到 n 层 ret_new 的对象
        fns = ['void *malloc(unsigned long n);', 'static UINT8 *A0(UINT32 n) { return (UINT8 *)malloc(n); }']
        for k in range(1, n):
            fns.append('static UINT8 *A%d(UINT32 n) { return A%d(n); }' % (k, k - 1))
        fns.append('void Top_TP(void) { UINT8 *p = A%d(16); if (p) { p[16] = 0; } }' % (n - 1))
        fns.append('void Top_TN(void) { UINT8 *p = A%d(16); if (p) { p[15] = 0; } }' % (n - 1))
        p = os.path.join(out, 't3_n%d.c' % n)
        open(p, 'w').write(HEAD + '\n'.join(fns) + '\n')
        items.append((3, n, [p], False))
        # 模板 4：模板 1 拆成每个函数一个文件（函数是外部链接）
        d = os.path.join(out, 't4_n%d' % n)
        os.makedirs(d)
        srcs = []
        for k in range(n):
            body = 'p[i] = 0;' if k == 0 else 'F%d(p, i);' % (k - 1)
            decl = '' if k == 0 else 'void F%d(UINT8 *p, UINT32 i);\n' % (k - 1)
            s = os.path.join(d, 'f%d.c' % k)
            open(s, 'w').write(HEAD + decl + 'void F%d(UINT8 *p, UINT32 i) { %s }\n' % (k, body))
            srcs.append(s)
        s = os.path.join(d, 'top.c')
        open(s, 'w').write(HEAD + 'void F%d(UINT8 *p, UINT32 i);\n' % (n - 1) +
                           'void Top_TP(void) { UINT8 a[16]; F%d(a, 16); }\n' % (n - 1) +
                           'void Top_TN(void) { UINT8 a[16]; F%d(a, 15); }\n' % (n - 1))
        srcs.append(s)
        items.append((4, n, srcs, True))
    return items


def decisions(wd):
    out = []
    for f in sorted(glob.glob(os.path.join(wd, 'decisions', '*.jsonl'))):
        lines = [json.loads(l) for l in open(f) if l.strip()]
        out += lines[1:]
    return out


def run_one(clang, triple, wd, srcs, xtu):
    if os.path.isdir(wd):
        shutil.rmtree(wd)
    if xtu:
        r = subprocess.run([sys.executable, os.path.join(REPO, 'oobsum', 'tools', 'oobsum_run.py'), 'local', '--workdir', wd,
                            '--clang', clang, '--triple', triple] + srcs, capture_output=True, text=True)
        return r.returncode == 0
    res = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    ok = True
    for s in srcs:
        args = [clang, '-cc1', '-triple', triple, '-analyze', '-resource-dir', res, '-internal-isystem',
                os.path.join(res, 'include'), '-analyzer-checker=' + CHECKER, '-analyzer-config', '%s:WorkDir=%s' % (CHECKER, wd)]
        if triple.startswith(('arm', 'thumb', 'aarch64')):
            args.append('-fno-signed-char')
        ok = subprocess.run(args + [s], capture_output=True, text=True).returncode == 0 and ok
    return ok


def verdict(t, n, ds):
    """按 G1 的口径：TP（模板 1、2、4：N >= 3 报 DEFINITE、N = 2 不报但日志里有 DEFINITE）；TN 不报 DEFINITE / POSSIBLE"""
    tp = [d for d in ds if d['fn'].endswith(':Top_TP') and d['label'] == 'definite']
    tn = [d for d in ds if d['fn'].endswith(':Top_TN') and d['label'] in ('definite', 'possible') and d.get('emitted')]
    tp_emitted = any(d.get('emitted') for d in tp)
    if tn:
        return False, 'TN 报了 %s' % tn[0]['label'].upper()
    if not tp:
        return False, 'TP 没有 DEFINITE 判定'
    if n >= 3 and not tp_emitted:
        return False, 'TP 没报（%s）' % tp[0].get('not_emitted_because', '?')
    if n == 2 and tp_emitted and t != 3:
        return False, 'N = 2 的 TP 报了（应交现有规则）'
    return True, 'TP %s、TN 不报' % ('报 DEFINITE' if tp_emitted else '不报（%s）' % tp[0].get('not_emitted_because', '?'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--clang', default=os.path.join(REPO, 'cmake-build-release', 'bin', 'clang'))
    ap.add_argument('--triple', default='armebv7-linux-gnueabi')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    items = gen(out)
    print('已生成 %d 个链（模板 1–4 × N ∈ %s）：%s' % (len(items), NS, out))
    if not a.check:
        return 0
    bad = 0
    for t, n, srcs, xtu in items:
        wd = os.path.join(out, 'wd', 't%d_n%d' % (t, n))
        ran = run_one(a.clang, a.triple, wd, srcs, xtu)
        ok, why = verdict(t, n, decisions(wd)) if ran else (False, '运行失败')
        bad += not ok
        print('模板 %d  N=%-3d %s  %s' % (t, n, '通过' if ok else '✗', why))
    print('合计 %d 个，不符合 %d 个' % (len(items), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
