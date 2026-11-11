"""SPEC-0139：只读选中FIR的报告边界，使用自有生成的测试代码。

由selected-report.test传clang、CAP夹具及新输出根。核对同FIR字符串宏不会在report追加
当前AST调试标签、inline/legacy钩子仍可用；多变体未选进程不覆盖Dump/日志，
未导出降级参数和坏solved元数据明确失败。所有原始命令/输出/判断保留在临时根。
"""
import json
import os
from pathlib import Path
import subprocess
import sys

clang, variant_source, directory = sys.argv[1:]
root = Path(directory).resolve()
root.mkdir(parents=True)
checker = 'rb.enhance.core.OobSummary'
serial = 0
env = dict(os.environ)
env.pop('OOBSUM_CONFIG', None)


def run(wd, phase, source, define, *configs, ok=True):
    global serial
    serial += 1
    cmd = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
           '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-D' + define]
    for value in ['WorkDir=' + str(wd), 'Phase=' + phase, *configs]:
        cmd += ['-analyzer-config', checker + ':' + value]
    p = subprocess.run(cmd + [str(source)], text=True, capture_output=True, env=env)
    (root / ('%02d-%s.log' % (serial, phase))).write_text(json.dumps(cmd) + '\n' + p.stdout + p.stderr)
    assert (p.returncode == 0) == ok, (cmd, p.stderr)
    return p.stderr


def body(wd, directory, suffix):
    return next((wd / directory).glob('*' + suffix)).read_bytes()


source = root / 'debug.c'
source.write_text('''/* Self-owned diagnostic hook fixture, SPEC-0139. */
#if MODE
#define LABEL "definite"
#else
#define LABEL "possible"
#endif
void oobsum_debug_emit(const char*, const char*, int);
void DebugOnly(void) { oobsum_debug_emit(LABEL, "", 3); }
''')
wd = root / 'same-fir'
run(wd, 'export', source, 'MODE=0')
original = body(wd, 'fir', '.json')
run(wd, 'export', source, 'MODE=1')
assert body(wd, 'fir', '.json') == original and not (wd / 'fir-variants').exists()
run(wd, 'solve', source, 'MODE=0')
a = run(wd, 'report', source, 'MODE=0')
judgments = body(wd, 'decisions', '.jsonl')
b = run(wd, 'report', source, 'MODE=1')
assert a == b and judgments == body(wd, 'decisions', '.jsonl')
assert 'debug decision from' not in a
for mode, label in [(0, 'POSSIBLE'), (1, 'DEFINITE')]:
    text = run(root / ('inline-%d' % mode), 'inline', source, 'MODE=%d' % mode)
    assert '[OOB-' + label + '] debug decision from' in text
legacy = root / 'legacy'
legacy.mkdir()
assert '[OOB-POSSIBLE] debug decision from' in run(legacy, 'report', source, 'MODE=0')

wd = root / 'variants'
run(wd, 'export', variant_source, 'CAP=8')
small = json.loads(body(wd, 'fir', '.json'))
run(wd, 'export', variant_source, 'CAP=32')
chosen = 8 if json.loads(body(wd, 'fir', '.json')) == small else 32
other = 32 if chosen == 8 else 8
run(wd, 'solve', variant_source, 'CAP=8')
solved = next((wd / 'solved').glob('*.json'))
original_solved = solved.read_bytes()
assert 'source_files' in json.loads(original_solved)
dump = 'Dump=options+keys+fir+summary+state'
observed = run(wd, 'report', variant_source, 'CAP=%d' % chosen, dump)
judgments = body(wd, 'decisions', '.jsonl')
plain = run(wd, 'report', variant_source, 'CAP=%d' % chosen)
assert body(wd, 'decisions', '.jsonl') == judgments
assert [l for l in plain.splitlines() if ': warning:' in l or ': note:' in l] == [
    l for l in observed.splitlines() if ': warning:' in l or ': note:' in l]


def traces():
    return {str(p.relative_to(wd)): p.read_bytes() for p in (wd / 'dump').rglob('*')
            if p.is_file() and 'report-selection-' not in p.name}


before = traces()
text = run(wd, 'report', variant_source, 'CAP=%d' % other, dump)
assert '[OOB-' not in text and '[oobsum:options]' not in text
assert traces() == before and body(wd, 'decisions', '.jsonl') == judgments
text = run(wd, 'report', variant_source, 'CAP=%d' % chosen, dump, 'MaxBlocks=1', ok=False)
assert 'was not exported' in text and body(wd, 'decisions', '.jsonl') == judgments
bad = json.loads(original_solved)
bad['source_files'] = 'invalid'
solved.write_text(json.dumps(bad))
assert 'invalid solved source_files' in run(wd, 'report', variant_source, 'CAP=%d' % chosen, ok=False)
bad = json.loads(original_solved)
bad['selected_fir'] = 'ffffffffffffffff'
solved.write_text(json.dumps(bad))
assert 'minimum-hash selection' in run(wd, 'report', variant_source, 'CAP=%d' % chosen, ok=False)
assert body(wd, 'decisions', '.jsonl') == judgments
solved.write_bytes(original_solved)
print('selected report: fixed judgments / legacy hook / gate before dumps / input drift / metadata: OK')
