#!/usr/bin/env python3
"""EngineTest 的输入（oobsum/specs/SPEC-0021 §5、SPEC-0029 §5）：用本仓库的 clang 把 engine.c、solve_a.c、solve_b.c 降级（Dump=fir），
把本机路径换成 /x/<文件名> 后写成 engine-fir.json、solve-a-fir.json、solve-b-fir.json。改了源文件或降级规则后重跑。

用法：python3 -I gen_engine_fir.py [clang 路径]
只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True
import glob, json, os, shutil, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
clang = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'cmake-build-release', 'bin', 'clang')
# 源文件 → 输出（路径换成 /x/<文件名>）；solve_a / solve_b 给 SolveAcrossModules（SPEC-0029）
for name in ['engine', 'solve_a', 'solve_b']:
    src = os.path.join(HERE, name + '.c')
    tmp = tempfile.mkdtemp(prefix='oobsum-engine-fir-')
    try:
        subprocess.run([clang, '-cc1', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char', '-analyze',
                        '-analyzer-checker=rb.enhance.ipa.OobSummary', '-analyzer-config', 'rb.enhance.ipa.OobSummary:Dump=fir',
                        '-analyzer-config', 'rb.enhance.ipa.OobSummary:WorkDir=' + tmp, src],
                       check=True, capture_output=True)
        path = glob.glob(os.path.join(tmp, 'dump', '*', 'fir', '*.json'))[0]
        text = open(path, encoding='utf-8').read().replace(src, '/x/' + name + '.c')
        json.loads(text)
        out = name.replace('_', '-') + '-fir.json'
        with open(os.path.join(HERE, out), 'w', encoding='utf-8') as f:
            f.write(text)
        print('%s：%d 字节' % (out, len(text)))
    finally:
        shutil.rmtree(tmp)
