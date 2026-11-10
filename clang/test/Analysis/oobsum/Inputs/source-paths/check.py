"""SPEC-0136：所有源码在临时目录自有生成，不读取业务代码。

背景：全局basename回译会把Mid调用note指向Leaf所在同名头文件。
用法：source-paths.test传clang和新输出根；验证同TU头文件、跨TU同名静态函数、旧FIR和真实歧义。
每次原始命令/输出、FIR、solved和判定均留在临时输出根；Dump仅观察，不能改变标签和指纹。
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

clang, output = sys.argv[1:]
root = Path(output).resolve()
root.mkdir(parents=True)
checker = 'rb.enhance.core.OobSummary'
serial = 0


def source(name, text):
    p = root / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('/* Self-generated source fixture, SPEC-0136; no business code. */\n' + text)
    return p


def run(wd, phase, src, *configs):
    global serial
    serial += 1
    cmd = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
           '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none']
    for value in ['Phase=' + phase, 'WorkDir=' + str(wd), *configs]:
        cmd += ['-analyzer-config', checker + ':' + value]
    p = subprocess.run(cmd + [str(src)], capture_output=True, text=True)
    (root / ('%02d-%s.log' % (serial, phase))).write_text(p.stdout + p.stderr)
    assert p.returncode == 0, (cmd, p.returncode, p.stderr)
    return p.stderr


def rows(wd):
    return [json.loads(line) for p in (wd / 'decisions').glob('*.jsonl') for line in p.read_text().splitlines()
            if json.loads(line).get('schema') == 'oobsum-decision/1']


def strip_debug(text):
    return ''.join(l for l in text.splitlines(keepends=True) if not l.startswith('[oobsum:diagnostics]'))


def trace_file(wd, solve=False):
    files = list((wd / 'dump').glob('solve/source-locations.jsonl' if solve else '*/source-locations.jsonl'))
    assert len(files) == 1, files
    return [json.loads(l) for l in files[0].read_text().splitlines()]


# Leaf先出现，不能把b/common.h中的Mid误归a/common.h。
a = source('headers/a/common.h', 'static void Leaf(char *p, int i) { p[i]=0; }\n')
b = source('headers/b/common.h', 'static void Mid(char *p, int i) { Leaf(p,i); }\n')
main = source('headers/root.c', '#include "a/common.h"\n#include "b/common.h"\nvoid Entry(void) { char a[8]; Mid(a,8); }\n')
wd = root / 'header-work'
plain = run(wd, 'inline', main)
before = rows(wd)
d = next(r for r in before if r.get('emitted'))
assert d['label'] == 'definite'
assert str(b) in d['chain'][1] and str(a) in d['chain'][-1], d['chain']
assert any(line.startswith(str(b) + ':') and 'Mid calls Leaf here' in line for line in plain.splitlines())
for _ in range(3):
    assert run(wd, 'inline', main) == plain
observed = run(wd, 'inline', main, 'Dump=diagnostics', 'DumpFilter=Entry')
assert strip_debug(observed) == plain and rows(wd) == before
traces = trace_file(wd)
assert traces and all(t['schema'] == 'oobsum-source-location/1' for t in traces)
assert any(t['fn'] == 'Mid' and t['resolved'].startswith(str(b) + ':') for t in traces)
filtered = run(wd, 'inline', main, 'Dump=diagnostics', 'DumpFilter=NOT_SELECTED')
assert strip_debug(filtered) == plain and rows(wd) == before
assert trace_file(wd) == []

# 同名源文件、同名static Init：必须沿已知caller/callee_key区分，不能按显示名首选。
left = source('cross/a/common.c', 'void Leaf(char*,int);\nstatic void Init(char *p,int i) { Leaf(p,i); }\nvoid EntryA(void) { char a[8]; Init(a,8); }\n')
right = source('cross/b/common.c', 'void Leaf(char*,int);\nstatic void Init(char *p,int i) { Leaf(p,i); }\nvoid EntryB(void) { char b[8]; Init(b,8); }\n')
leaf = source('cross/leaf.c', 'void Leaf(char *p,int i) { p[i]=0; }\n')
wd = root / 'cross-work'
for src in (left, right, leaf):
    run(wd, 'export', src)
run(wd, 'solve', leaf, 'Dump=diagnostics')
for src in (left, right, leaf):
    run(wd, 'report', src)
for key, file in [('F:EntryA', left), ('F:EntryB', right)]:
    ds = [r for r in rows(wd) if r['fn'] == key and r['label'] == 'definite']
    assert ds and all(str(file) in r['chain'][0] and str(file) in r['chain'][1] for r in ds), ds
assert any(t['fn'] == 'Init' and t['resolved'].startswith(str(left) + ':') for t in trace_file(wd, True))
assert any(t['fn'] == 'Init' and t['resolved'].startswith(str(right) + ':') for t in trace_file(wd, True))

# 模拟无diag/accesses的旧FIR；短位置和推理字段保留，不靠新AST重新猜来源。
legacy = root / 'legacy'
shutil.copytree(wd / 'fir', legacy / 'fir')
for file in (legacy / 'fir').glob('*.json'):
    module = json.loads(file.read_text())
    for fn in module['funcs']:
        for block in fn['blocks']:
            for inst in block['insts']:
                inst.pop('diag', None)
                inst.pop('accesses', None)
    file.write_text(json.dumps(module))
run(legacy, 'solve', leaf)
run(legacy, 'report', leaf)
assert {(r['fn'], r['fp'], r['label']) for r in rows(legacy)} == {(r['fn'], r['fp'], r['label']) for r in rows(wd)}
for r in rows(legacy):
    if r['fn'] in ('F:EntryA', 'F:EntryB'):
        assert str(left if r['fn'] == 'F:EntryA' else right) in r['chain'][1]

# 同一个函数里两个同名片段，同一短行又调用同一Leaf；信息已丢，必须不猜。
source('fragments/a/body.inc', 'Leaf(a,8);\n')
source('fragments/b/body.inc', 'Leaf(a,8);\n')
frag = source('fragments/root.c', 'static void Leaf(char *p,int i) { p[i]=0; }\nvoid Entry(void) { char a[8];\n#include "a/body.inc"\n#include "b/body.inc"\n}\n')
wd = root / 'ambiguous'
text = run(wd, 'inline', frag, 'Dump=diagnostics', 'DumpFilter=Entry')
ds = [r for r in rows(wd) if r.get('emitted')]
assert ds and all(r['label'] == 'definite' for r in ds)
assert any('<oobsum-unresolved>/body.inc' in hop for r in ds for hop in r['chain'])
assert 'some context or call-chain source locations are unresolved' in text
assert not any('Entry calls Leaf here' in line for line in text.splitlines())
assert any(t['status'] == 'ambiguous' and t['candidates'] == 2 for t in trace_file(wd))

# 无diag时即使仍有accesses元数据，也不能从两个同名片段的访问中首选一个sink。
source('old-sinks/a/body.inc', 'a[8]=0;\n')
source('old-sinks/b/body.inc', 'a[8]=0;\n')
old = source('old-sinks/root.c', 'void Entry(void) { char a[8];\n#include "a/body.inc"\n#include "b/body.inc"\n}\n')
wd = root / 'old-sinks-work'
run(wd, 'export', old)
for file in (wd / 'fir').glob('*.json'):
    module = json.loads(file.read_text())
    for fn in module['funcs']:
        for block in fn['blocks']:
            for inst in block['insts']:
                inst.pop('diag', None)
    file.write_text(json.dumps(module))
run(wd, 'solve', old, 'Dump=diagnostics')
text = run(wd, 'report', old)
ds = [r for r in rows(wd) if r.get('emitted')]
assert ds and all(r['label'] == 'definite' for r in ds)
assert all(r.get('sink', '').startswith('<oobsum-unresolved>/body.inc:') for r in ds), ds
assert 'some context or call-chain source locations are unresolved' in text
assert not any('Entry accesses the object here' in line for line in text.splitlines())
print('source paths: owner / call keys / headers / cross TU / legacy / ambiguity / diagnostics: OK')
