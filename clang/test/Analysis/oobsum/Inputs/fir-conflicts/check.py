"""SPEC-0133：用自有同源CAP变体复现并阻断共享WorkDir覆盖。

输入：clang、variant.c、临时目录。独立配置应分别DEFINITE/SAFE；同一目录混合配置必须报错，
保留两份有效FIR，不能根据最后完成者继续生成SAFE或告警。也验证重复相同导出、pending、
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


def command(wd, phase, cap):
    return [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
            '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-DCAP=' + str(cap),
            '-analyzer-config', checker + ':Phase=' + phase,
            '-analyzer-config', checker + ':WorkDir=' + str(wd),
            '-analyzer-config', checker + ':Dump=inputs+progress', source]


def run(wd, phase, cap=8, ok=True):
    global serial
    serial += 1
    result = subprocess.run(command(wd, phase, cap), text=True, capture_output=True)
    (root / ('%02d-%s.log' % (serial, phase))).write_text(result.stdout + result.stderr)
    assert (result.returncode == 0) == ok, (phase, cap, result.returncode, result.stderr)
    return result.stderr


def rows(wd):
    return [json.loads(line) for p in (wd / 'decisions').glob('*.jsonl') for line in p.read_text().splitlines()
            if line.strip() and json.loads(line).get('schema') == 'oobsum-decision/1']


def conflict(wd):
    dirs = list((wd / 'fir-conflicts').iterdir())
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

# 无论先后次序都拒绝混合域，保留相同两份冲突证据，原canonical不被覆盖。
evidence = []
for name, caps in [('bad-first', (8, 32)), ('safe-first', (32, 8))]:
    mixed = root / name
    run(mixed, 'export', caps[0])
    canonical = next((mixed / 'fir').glob('*.json'))
    before = canonical.read_bytes()
    assert 'FIR export failed' in run(mixed, 'export', caps[1], ok=False)
    assert canonical.read_bytes() == before
    evidence.append(conflict(mixed))
    run(mixed, 'solve', ok=False)
    progress = json.loads((mixed / 'dump/solve/progress.json').read_text())
    assert progress['stage'] == 'failed' and not list((mixed / 'solved').glob('*.json'))
    assert '[OOB-' not in run(mixed, 'report', ok=False)
assert evidence[0] == evidence[1]

# 确实同时启动两个写者，已有目标也不能被任一进程替换。
parallel = root / 'parallel'
processes = [subprocess.Popen(command(parallel, 'export', cap), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for cap in (8, 32)]
codes = []
for i, process in enumerate(processes):
    output, _ = process.communicate(timeout=30)
    (root / ('parallel-%d.log' % i)).write_text(output)
    codes.append(process.returncode)
assert any(code != 0 for code in codes)
assert conflict(parallel) == evidence[0]
run(parallel, 'solve', ok=False)

# 候选遗留和只有目录而未写完证据的冲突都不能被当成完整输入。
for kind in ('temporary', 'empty-conflict'):
    stale = root / kind
    run(stale, 'export')
    if kind == 'temporary':
        (stale / 'fir/orphan.export-x.tmp').write_text('partial')
    else:
        (stale / 'fir-conflicts').mkdir()
    run(stale, 'solve', ok=False)
    assert not list((stale / 'solved').glob('*.json'))
    state = json.loads((stale / 'dump/inputs.json').read_text())
    assert state['status'] == ('incomplete_export' if kind == 'temporary' else 'conflict')
print('FIR publication: single / duplicate / pending / conflicting / concurrent / incomplete: OK')
