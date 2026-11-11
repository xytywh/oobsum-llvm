"""SPEC-0132/0139：固定FIR重复report，验证note唯一定位、消歧失败和调试可观测。

输入：clang路径、仅含自有C/头文件的夹具目录、临时WorkDir。
执行：export/solve一次，反复report；AMBIGUOUS只控制是否载入无语义的同名头文件。
预期：新solved的source_files固定note范围，空header不改输出；legacy仍按当前可见文件消歧。
唯一候选有note，歧义/缺失不猜，完整路径也受固定范围约束；主warning与decisions不变。
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


def run(phase, ambiguous=0, configs=(), *, source_file=None, workdir=None, defines=()):
    global invocations
    invocations += 1
    cmd = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
           '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-DAMBIGUOUS=' + str(ambiguous)]
    for define in defines:
        cmd += ['-D' + define]
    for config in ['Phase=' + phase, 'WorkDir=' + str(workdir or work), 'InlineDepth=0'] + list(configs):
        cmd += ['-analyzer-config', checker + ':' + config]
    process = subprocess.run(cmd + [str(source_file or source)], capture_output=True, text=True)
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
assert str(fixture / 'a/common.h') in data['source_files'], data
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
assert len(set(ambiguous)) == 1 and ambiguous[0] == unique[0]
correct_allocation_note(ambiguous[0])
assert judgments() == baseline_decisions
assert [l for l in unique[0].splitlines() if ': warning:' in l] == [l for l in ambiguous[0].splitlines() if ': warning:' in l]
debug = run('report', 1, ['Dump=diagnostics', 'DumpFilter=AllocAndWrite'])
assert warning_text(debug) == ambiguous[0] and judgments() == baseline_decisions
trace_is('unique', 1, True)
filtered = run('report', 1, ['Dump=diagnostics', 'DumpFilter=NO_SUCH_FUNCTION'])
assert warning_text(filtered) == ambiguous[0] and judgments() == baseline_decisions
assert traces() == [] and '[oobsum:diagnostics]' not in filtered

# 固定文件表的真实歧义不会因当前SM少载入一个文件而变成唯一。
both_files = copy.deepcopy(data)
both_files['source_files'].append(str(fixture / 'b/common.h'))
solved_path.write_text(json.dumps(both_files))
bounded = [run('report', flag, ['Dump=diagnostics', 'DumpFilter=Write']) for flag in (0, 1)]
assert warning_text(bounded[0]) == warning_text(bounded[1])
assert 'note: the object is allocated here' not in bounded[0]
trace_is('ambiguous', 2)

# 完整路径仍必须属于所选文件表，不能碰巧在当前AST出现就补出note。
for kind in ('full', 'missing', 'outside'):
    changed = copy.deepcopy(data)
    row = next(d for d in changed['decisions'] if d['obj'].startswith('heap@'))
    path = fixture / {'full': 'a/common.h', 'missing': 'absent.h', 'outside': 'b/common.h'}[kind]
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

# 旧手工solved没有source_files：保留SPEC-0132对实际可见文件集合的原歧义/完整路径/缺失断言。
legacy_data = copy.deepcopy(data)
legacy_data.pop('source_files')
solved_path.write_text(json.dumps(legacy_data))
legacy_unique = run('report')
correct_allocation_note(legacy_unique)
legacy_ambiguous = [run('report', 1) for _ in range(10)]
assert len(set(legacy_ambiguous)) == 1
assert 'note: the object is allocated here' not in legacy_ambiguous[0]
assert judgments() == baseline_decisions
assert [l for l in legacy_unique.splitlines() if ': warning:' in l] == [l for l in legacy_ambiguous[0].splitlines() if ': warning:' in l]
legacy_debug = run('report', 1, ['Dump=diagnostics', 'DumpFilter=AllocAndWrite'])
assert warning_text(legacy_debug) == legacy_ambiguous[0] and judgments() == baseline_decisions
trace_is('ambiguous', 2)
for kind in ('full', 'missing'):
    changed = copy.deepcopy(legacy_data)
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

# 同FIR的额外include还可能影响跨TU chain note，固定文件表后两种report应一致。
chain_source = root / 'chain-source'
chain_source.mkdir()
caller = chain_source / 'caller.c'
leaf = chain_source / 'leaf.c'
caller.write_text('#if BUILD_CALLER\nvoid Leaf(char*);\nvoid Entry(void){char a[8];Leaf(a);}\n#endif\n')
leaf.write_text('void Leaf(char*p){p[8]=0;}\n#if INCLUDE_CALLER\n#include "caller.c"\n#endif\n')
chain_work = root / 'chain-work'
run('export', source_file=caller, workdir=chain_work, defines=['BUILD_CALLER=1'])
run('export', source_file=leaf, workdir=chain_work, defines=['INCLUDE_CALLER=0'])
before = {p.name: p.read_bytes() for p in (chain_work / 'fir').glob('*.json')}
run('export', source_file=leaf, workdir=chain_work, defines=['INCLUDE_CALLER=1'])
assert before == {p.name: p.read_bytes() for p in (chain_work / 'fir').glob('*.json')}
run('solve', source_file=leaf, workdir=chain_work)
chain_reports = [run('report', source_file=leaf, workdir=chain_work,
                     defines=['INCLUDE_CALLER=' + str(flag)]) for flag in (0, 1)]
assert chain_reports[0] == chain_reports[1], chain_reports
assert '[OOB-DEFINITE]' in chain_reports[0]
assert not any('note: Entry calls Leaf here' in line for line in chain_reports[0].splitlines())
assert any('note: Leaf accesses the object here' in line for line in chain_reports[0].splitlines())
print('allocation/chain notes: fixed files / same FIR / legacy / ambiguous / missing / full path / filter: OK')
