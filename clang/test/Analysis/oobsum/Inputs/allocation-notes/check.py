"""SPEC-0132：固定FIR重复report，验证分配note唯一定位、消歧失败和调试可观测。

输入：clang路径、仅含自有C/头文件的夹具目录、临时WorkDir。
执行：export/solve一次，反复report；AMBIGUOUS只控制是否载入无语义的同名头文件。
预期：唯一候选有note，歧义/缺失不猜，完整路径可精确定位；主warning与decisions不变。
Dump=diagnostics只增加单独调试输出，按DumpFilter选函数，并落allocation-notes.jsonl。
由allocation-notes.test驱动，只用Python标准库；不读取公司源码。
"""
import copy
import json
from pathlib import Path
import re
import subprocess
import sys

clang, fixture, output = sys.argv[1:]
fixture, root = Path(fixture).resolve(), Path(output).resolve()
root.mkdir(parents=True)
work = root / 'work'
checker = 'rb.enhance.core.OobSummary'
source = fixture / 'root.c'
invocations = 0


def run(phase, ambiguous=0, configs=()):
    global invocations
    invocations += 1
    cmd = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
           '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-DAMBIGUOUS=' + str(ambiguous)]
    for config in ['Phase=' + phase, 'WorkDir=' + str(work), 'InlineDepth=0'] + list(configs):
        cmd += ['-analyzer-config', checker + ':' + config]
    process = subprocess.run(cmd + [str(source)], capture_output=True, text=True)
    (root / ('%02d-%s.log' % (invocations, phase))).write_text(process.stdout + process.stderr)
    assert process.returncode == 0, (cmd, process.stdout, process.stderr)
    return process.stderr


def judgments():
    return {p.name: p.read_bytes() for p in (work / 'decisions').glob('*.jsonl')}


def warning_text(text):
    return ''.join(line for line in text.splitlines(keepends=True) if not line.startswith('[oobsum:diagnostics]'))


def allocation_notes(text):
    return [line.split(': note:')[0].rsplit(':', 2) for line in text.splitlines()
            if 'note: the object is allocated here' in line]


def correct_allocation_note(text):
    notes = allocation_notes(text)
    assert len(notes) == 1, notes
    path, line, column = notes[0]
    assert Path(path).resolve() == fixture / 'a/common.h', notes
    assert int(line) == allocation_line and column == '1', notes


def traces():
    files = list((work / 'dump').glob('*/allocation-notes.jsonl'))
    assert len(files) == 1, files
    return [json.loads(line) for line in files[0].read_text().splitlines() if line.strip()]


def trace_is(status, count, resolved=False):
    entries = traces()
    assert len(entries) == 1, entries
    entry = entries[0]
    assert entry['schema'] == 'oobsum-allocation-note/1'
    assert entry['status'] == status and entry['candidates'] == count, entry
    assert re.fullmatch('[0-9a-f]{16}', entry['fp'])
    if resolved:
        path, line, column = entry['resolved'].rsplit(':', 2)
        assert Path(path).resolve() == fixture / 'a/common.h', entry
        assert (line, column) == (str(allocation_line), '1'), entry
    else:
        assert entry['resolved'] == '', entry


run('export')
run('solve')
solved_path = next((work / 'solved').glob('*.json'))
original_solved = solved_path.read_bytes()
data = json.loads(original_solved)
heap_rows = [d for d in data['decisions'] if d['obj'].startswith('heap@')]
assert len(heap_rows) == 1 and heap_rows[0]['label'] == 'definite', heap_rows
allocation_line = int(heap_rows[0]['obj'].rsplit(':', 1)[1])

# 同名空头文件不改变分析输入；差异只在report可见文件集合。
original_fir = {p.name: p.read_bytes() for p in (work / 'fir').glob('*.json')}
run('export', 1)
assert original_fir == {p.name: p.read_bytes() for p in (work / 'fir').glob('*.json')}
run('solve')  # SPEC-0133：重新export后走完整阶段屏障，即使FIR内容相同。
assert solved_path.read_bytes() == original_solved

unique = [run('report') for _ in range(5)]
assert len(set(unique)) == 1
correct_allocation_note(unique[0])
assert '[oobsum:diagnostics]' not in unique[0]
assert not list((work / 'dump').glob('*/allocation-notes.jsonl'))
baseline_decisions = judgments()
debug = run('report', configs=['Dump=diagnostics', 'DumpFilter=AllocAndWrite'])
assert warning_text(debug) == unique[0] and judgments() == baseline_decisions
trace_is('unique', 1, True)

ambiguous = [run('report', 1) for _ in range(10)]
assert len(set(ambiguous)) == 1
assert 'note: the object is allocated here' not in ambiguous[0]
assert judgments() == baseline_decisions
assert [l for l in unique[0].splitlines() if ': warning:' in l] == [l for l in ambiguous[0].splitlines() if ': warning:' in l]
debug = run('report', 1, ['Dump=diagnostics', 'DumpFilter=AllocAndWrite'])
assert warning_text(debug) == ambiguous[0] and judgments() == baseline_decisions
trace_is('ambiguous', 2)
filtered = run('report', 1, ['Dump=diagnostics', 'DumpFilter=NO_SUCH_FUNCTION'])
assert warning_text(filtered) == ambiguous[0] and judgments() == baseline_decisions
assert traces() == [] and '[oobsum:diagnostics]' not in filtered

# Reporter可读取的机器记录：直接构造完整路径/缺失路径来源，主判断仍需报告。
for kind in ('full', 'missing'):
    changed = copy.deepcopy(data)
    row = next(d for d in changed['decisions'] if d['obj'].startswith('heap@'))
    path = fixture / 'a/common.h' if kind == 'full' else fixture / 'absent.h'
    row['obj'] = 'heap@' + str(path) + ':' + str(allocation_line)
    solved_path.write_text(json.dumps(changed))
    output = run('report', 1, ['Dump=diagnostics', 'DumpFilter=Write'])
    assert '[OOB-DEFINITE]' in output
    if kind == 'full':
        correct_allocation_note(output)
        trace_is('unique', 1, True)
    else:
        assert 'note: the object is allocated here' not in output
        trace_is('missing', 0)
solved_path.write_bytes(original_solved)
print('allocation notes: unique / ambiguous / missing / full path / filter / stable reports: OK')
