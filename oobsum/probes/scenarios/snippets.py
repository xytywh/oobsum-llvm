#!/usr/bin/env python3
"""从 04-scenario-tiers.md 抽出全部 C 片段，逐段做语法检查，可选用 Infer 当标尺跑一遍。

用法：
  python3 snippets.py extract [--out DIR]                 # 只抽取：DIR/S1_1.c ...（默认 ./out）
  python3 snippets.py check   --clang CLANG [--out DIR]   # 抽取 + clang -fsyntax-only -Wall -Wextra
  python3 snippets.py infer   --infer INFER [--out DIR] [--shim]
                                                          # 抽取 + infer --bufferoverrun-only，打印对照表
      --shim：Infer 没有 memcpy_s / VOS_MemAlloc 的模型，用宏换成它认识的函数再跑：
              VOS_MemAlloc(pid, pt, sz) -> malloc(sz)
              memcpy_s / memset_s(d, destMax, ...) -> memset(d, 0, destMax)
              （后者对应安全函数出错路径按 destMax 清零目的缓冲区，只检验 destMax <= extent(dst) 这一半）

文档是唯一出处：片段改了，重新跑一遍即可。第一个 ```c 代码块是公共声明，会拼到每个片段前面。
只依赖 Python 3 标准库。
"""
import argparse, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DOC = os.path.normpath(os.path.join(HERE, '..', '..', '04-scenario-tiers.md'))
# 期望（与文档 §3 的标注一致）：None 表示期望不报
EXPECT = {
    'S1_1': 'L1', 'S1_2': 'L2', 'S1_3': 'L1', 'S1_4': 'L1', 'S1_5': 'L1', 'S1_6': 'L1', 'S1_7': 'L1',
    'S2_1': 'L1', 'S2_2': 'L1', 'S2_3': 'L1', 'S2_4': 'L1', 'S2_5': 'L1', 'S2_6': 'L1',
    'S2_7_TN': None, 'S2_7_TP': 'L2',
    'S3_1_TN': None, 'S3_1_TP': 'L2', 'S3_2_TP': 'L2', 'S3_2_TN': None, 'S3_3': 'L2',
    'S3_4': 'L2', 'S3_5': 'L1', 'S3_6': 'L2', 'S3_7': None, 'S3_8': 'L2', 'S3_9': 'L2',
    'S4_1': 'L1', 'S4_2': 'L1', 'S4_3': 'L1', 'S4_4': 'L1', 'S4_6': 'L2',
    'S5_1': 'L1', 'S5_2': 'L1', 'S5_3': 'L2',
    'S6_1': 'L1', 'S6_2': 'L2', 'S6_3': 'L2', 'S9_1': 'L1', 'S9_2': 'L1', 'S10_1': 'L2',
}
# Infer 没有模型、跑了也不说明问题的：安全函数与自定义分配器
NO_MODEL = re.compile(r'\b(memcpy_s|memset_s|VOS_MemAlloc)\s*\(')
SHIM = """#include <stdlib.h>
#include <string.h>
#define VOS_MemAlloc(pid, pt, sz) malloc(sz)
#define memcpy_s(d, dm, s, c) (memset((d), 0, (dm)), 0)
#define memset_s(d, dm, v, c) (memset((d), 0, (dm)), 0)
"""


def extract(out, shim=False):
    text = open(DOC, encoding='utf-8').read()
    blocks = re.findall(r'```c\n(.*?)```', text, re.S)
    if len(blocks) < 2:
        sys.exit('没有找到 C 片段：%s' % DOC)
    prelude, snippets = blocks[0], blocks[1:]
    if shim:   # 去掉三个原型（否则会被宏展开成冲突的声明），换成宏
        prelude = SHIM + '\n'.join(l for l in prelude.splitlines()
                                    if not re.match(r'^(void \*VOS_MemAlloc|errno_t mem(cpy|set)_s)\(', l)) + '\n'
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    files = []
    for b in snippets:
        tops = re.findall(r'^void (S\w+)\(', b, re.M)
        name = tops[0] if tops else 'snippet%02d' % (len(files) + 1)
        path = os.path.join(out, name + '.c')
        with open(path, 'w', encoding='utf-8') as fp:
            fp.write(prelude + '\n' + b)
        files.append((name, path, tops, bool(NO_MODEL.search(b)) and not shim))
    return files


def check(files, clang):
    bad = 0
    for name, path, _, _ in files:
        r = subprocess.run([clang, '-std=c11', '-fsyntax-only', '-Wall', '-Wextra',
                            '-Wno-unused-function', '-Wno-unused-parameter', path],
                           capture_output=True, text=True)
        if r.returncode != 0 or r.stderr.strip():
            bad += 1
            print('== %s\n%s' % (name, r.stderr.strip()))
    print('语法检查：%d 段，失败 %d' % (len(files), bad))
    return bad


def run_infer(files, infer, out, shim=False):
    rows = []
    for name, path, tops, no_model in files:
        rd = os.path.join(out, 'infer-' + name)
        subprocess.run([infer, '--bufferoverrun-only', '--results-dir', rd, '--no-progress-bar',
                        '--', 'clang', '-c', path, '-o', os.devnull],
                       capture_output=True, text=True, timeout=300)
        try:
            rep = json.load(open(os.path.join(rd, 'report.json')))
        except (OSError, ValueError):
            rep = []
        got = {}
        for r in rep:
            if r['bug_type'].startswith('BUFFER_OVERRUN_'):
                proc = r['procedure'].split('(')[0]
                got.setdefault(proc, set()).add(r['bug_type'].replace('BUFFER_OVERRUN_', ''))
        for t in tops:
            rows.append((t, EXPECT.get(t, '?'), ','.join(sorted(got.get(t, []))) or '—', no_model))
    print('%-9s %-6s %-8s %s' % ('场景', '期望', 'Infer', '备注'))
    for t, exp, got, no_model in rows:
        note = 'Infer 无安全函数 / 分配器模型（加 --shim 再看）' if no_model else ''
        print('%-9s %-6s %-8s %s' % (t, exp or '不报', got, note))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['extract', 'check', 'infer'])
    ap.add_argument('--out', default=os.path.join(os.getcwd(), 'out'))
    ap.add_argument('--clang')
    ap.add_argument('--infer')
    ap.add_argument('--shim', action='store_true')
    a = ap.parse_args()
    files = extract(a.out, a.shim and a.mode == 'infer')
    print('抽取 %d 段到 %s' % (len(files), a.out))
    if a.mode == 'check':
        if not a.clang:
            sys.exit('check 需要 --clang')
        sys.exit(1 if check(files, a.clang) else 0)
    if a.mode == 'infer':
        if not a.infer:
            sys.exit('infer 需要 --infer')
        run_infer(files, a.infer, a.out, a.shim)


if __name__ == '__main__':
    main()
