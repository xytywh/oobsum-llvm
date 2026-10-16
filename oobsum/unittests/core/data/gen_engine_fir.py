#!/usr/bin/env python3
"""EngineTest 的输入（oobsum/specs/SPEC-0021 §5）：用本仓库的 clang 把 engine.c 降级（Dump=fir），
把本机路径换成 /x/engine.c 后写成 engine-fir.json。改了 engine.c 或降级规则后重跑。

用法：python3 -I gen_engine_fir.py [clang 路径]
只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True
import glob, json, os, shutil, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
clang = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'cmake-build-release', 'bin', 'clang')
src = os.path.join(HERE, 'engine.c')
tmp = tempfile.mkdtemp(prefix='oobsum-engine-fir-')
try:
    subprocess.run([clang, '-cc1', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char', '-analyze',
                    '-analyzer-checker=rb.enhance.ipa.OobSummary', '-analyzer-config', 'rb.enhance.ipa.OobSummary:Dump=fir',
                    '-analyzer-config', 'rb.enhance.ipa.OobSummary:WorkDir=' + tmp, src],
                   check=True, capture_output=True)
    path = glob.glob(os.path.join(tmp, 'dump', '*', 'fir', '*.json'))[0]
    text = open(path, encoding='utf-8').read().replace(src, '/x/engine.c')
    json.loads(text)
    with open(os.path.join(HERE, 'engine-fir.json'), 'w', encoding='utf-8') as f:
        f.write(text)
    print('engine-fir.json：%d 字节' % len(text))
finally:
    shutil.rmtree(tmp)
