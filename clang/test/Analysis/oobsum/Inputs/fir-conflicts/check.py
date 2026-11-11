"""SPEC-0133：用自有同源CAP变体复现并阻断共享WorkDir覆盖。

输入：clang、variant.c、临时目录。独立配置应分别DEFINITE/SAFE；同一目录多配置保存并固定选择，
保留两份有效FIR，按SPEC-0139固定最小哈希选择，不能由最后完成者决定。也验证重复相同导出、pending、
空冲突目录、遗留临时文件以及实际并发写者。每次原始输出留在临时目录，失败可直接查看。
由fir-conflicts.test驱动，不读取业务代码。
"""
import json
from pathlib import Path
import subprocess
import sys

clang, source, directory = sys.argv[1:]
root = Path(directory).resolve()
root.mkdir(parents=True)
checker = 'rb.enhance.core.OobSummary'
serial = 0


def command(wd, phase, cap, output=None):
    args = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
            '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-DCAP=' + str(cap),
            '-analyzer-config', checker + ':Phase=' + phase,
            '-analyzer-config', checker + ':WorkDir=' + str(wd),
            '-analyzer-config', checker + ':Dump=inputs+progress', source]
    return args + (['-o', str(root / output)] if output else [])


def run(wd, phase, cap=8, ok=True, output=None):
    global serial
    serial += 1
    result = subprocess.run(command(wd, phase, cap, output), text=True, capture_output=True)
    (root / ('%02d-%s.log' % (serial, phase))).write_text(result.stdout + result.stderr)
    assert (result.returncode == 0) == ok, (phase, cap, result.returncode, result.stderr)
    return result.stderr


def rows(wd):
    return [json.loads(line) for p in (wd / 'decisions').glob('*.jsonl') for line in p.read_text().splitlines()
            if line.strip() and json.loads(line).get('schema') == 'oobsum-decision/1']


def variants(wd):
    dirs = list((wd / 'fir-variants').iterdir())
    assert len(dirs) == 1 and dirs[0].is_dir()
    variants = list(dirs[0].glob('*.json'))
    assert len(variants) == 2
    assert len({json.loads(p.read_text())['tu'] for p in variants}) == 1
    return {p.name: p.read_bytes() for p in variants}


# 正常单配置及相同内容重复写，不改默认文件数/路径；重新导出后必须重新solve。
wd = root / 'same'
run(wd, 'export')
fir = next((wd / 'fir').glob('*.json'))
original = fir.read_bytes()
run(wd, 'export')
assert fir.read_bytes() == original and len(list((wd / 'fir').iterdir())) == 1
assert (wd / 'inputs-pending').exists() and not (wd / 'fir-conflicts').exists()
assert '[OOB-' not in run(wd, 'report', ok=False)
run(wd, 'solve')
assert not (wd / 'inputs-pending').exists()
run(wd, 'report')
assert any(d['label'] == 'definite' and d['emitted'] for d in rows(wd))
run(wd, 'export')
assert '[OOB-' not in run(wd, 'report', ok=False)
run(wd, 'solve')
run(wd, 'report')
assert fir.read_bytes() == original
safe = root / 'safe'
run(safe, 'export', 32)
run(safe, 'solve', 32)
run(safe, 'report', 32)
assert rows(safe) and all(not d['emitted'] for d in rows(safe))

# SPEC-0139：正反次序/重复-o/实际并发均留存同一集合、选择同一代表，solve仍只算一个。
evidence, products = [], []
for name, caps in [('bad-first', (8, 32)), ('safe-first', (32, 8))]:
    mixed = root / name
    for cap in caps:
        run(mixed, 'export', cap)
    canonical = next((mixed / 'fir').glob('*.json'))
    snapshots = variants(mixed)
    chosen_name = min(snapshots)
    assert canonical.read_bytes() == snapshots[chosen_name]
    assert not (mixed / 'fir-conflicts').exists()
    chosen = 8 if json.loads(canonical.read_text()) == json.loads(original) else 32
    ignored = 32 if chosen == 8 else 8
    run(mixed, 'export', chosen, output='different-output.o')
    assert variants(mixed) == snapshots
    evidence.append(snapshots)
    run(mixed, 'solve')
    selection = json.loads((mixed / 'dump/solve/fir-selection.json').read_text())
    assert selection['policy'] == 'min-fir-hash/1'
    assert (selection['selected_tus'], selection['variant_tus'], selection['ignored_variants']) == (1, 1, 1)
    assert selection['entries'][0]['selected'] == chosen_name[:-5]
    solved_path = next((mixed / 'solved').glob('*.json'))
    solved_body = solved_path.read_bytes()
    assert json.loads(solved_body)['selected_fir'] == chosen_name[:-5]
    expected = run(mixed, 'report', chosen)
    assert ('[OOB-DEFINITE]' in expected) == (chosen == 8)
    log = next((mixed / 'decisions').glob('*.jsonl'))
    judgments = log.read_bytes()
    assert '[OOB-' not in run(mixed, 'report', ignored)
    assert log.read_bytes() == judgments
    assert 'was not exported' in run(mixed, 'report', 16, ok=False)
    assert log.read_bytes() == judgments
    # 后来的未选进程不写空日志；相同输入再次solve/report不改变产品。
    run(mixed, 'solve')
    assert solved_path.read_bytes() == solved_body
    assert run(mixed, 'report', chosen) == expected
    assert log.read_bytes() == judgments
    products.append((canonical.read_bytes(), solved_body, judgments, expected))
assert evidence[0] == evidence[1] and products[0] == products[1]

for iteration in range(3):
    parallel = root / ('parallel-%d' % iteration)
    processes = [subprocess.Popen(command(parallel, 'export', cap), stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True) for cap in (8, 32, 8, 32, 32, 8)]
    for i, process in enumerate(processes):
        output, _ = process.communicate(timeout=30)
        (root / ('parallel-%d-%d.log' % (iteration, i))).write_text(output)
        assert process.returncode == 0, output
    assert variants(parallel) == evidence[0]
    canonical = next((parallel / 'fir').glob('*.json'))
    assert canonical.read_bytes() == products[0][0]
    chosen = 8 if json.loads(canonical.read_text()) == json.loads(original) else 32
    run(parallel, 'solve')
    assert next((parallel / 'solved').glob('*.json')).read_bytes() == products[0][1]
    processes = [subprocess.Popen(command(parallel, 'report', cap), stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True) for cap in (32, 8, 32, 8)]
    for i, process in enumerate(processes):
        output, _ = process.communicate(timeout=30)
        (root / ('report-%d-%d.log' % (iteration, i))).write_text(output)
        assert process.returncode == 0, output
    assert next((parallel / 'decisions').glob('*.jsonl')).read_bytes() == products[0][2]

# inline没有选择阶段，维持原拒绝行为；老冲突、真正遗留仍不能求解。
inline = root / 'inline-reject'
run(inline, 'inline', 8)
assert 'conflicting FIR variants' in run(inline, 'inline', 32, ok=False)

# 候选遗留和只有目录而未写完证据的冲突都不能被当成完整输入。
for kind in ('temporary', 'empty-conflict', 'empty-variants'):
    stale = root / kind
    run(stale, 'export')
    if kind == 'temporary':
        (stale / 'fir/orphan.export-x.tmp').write_text('partial')
    else:
        (stale / ('fir-variants' if kind == 'empty-variants' else 'fir-conflicts')).mkdir()
    run(stale, 'solve', ok=False)
    assert not list((stale / 'solved').glob('*.json'))
    state = json.loads((stale / 'dump/inputs.json').read_text())
    assert state['status'] == ('incomplete_export' if kind == 'temporary' else 'io_error' if kind == 'empty-variants' else 'conflict')
print('FIR publication: single / duplicate / pending / selected variants / concurrent / report gate / incomplete: OK')
