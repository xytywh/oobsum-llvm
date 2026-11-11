"""SPEC-0137：基于fir-conflicts夹具验证发布观察与静态阶段检查的区别。

背景：发布成功/冲突后完整扫描共享fir/，会把活跃候选计作遗留，并可能stat已被unlink的文件。
输入：export-inputs.test传入clang、自有CAP源码、新临时根；不读取业务数据。
方法：持有一个候选，实际并发重复发布并同时增删其它候选；核验浅观察/本次结果/按进程落盘。
之后停止全部写者，验证solve仍拒绝真正遗留与冲突。正常判断与Dump开关不变。
每次命令和原始输出保存到临时根，失败可复查；测试不通过不能通过删冲突或挑选变体规避。
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading

clang, source, directory = sys.argv[1:]
root = Path(directory).resolve()
root.mkdir(parents=True)
checker = 'rb.enhance.core.OobSummary'
env = dict(os.environ)
env.pop('OOBSUM_CONFIG', None)
serial = 0


def command(wd, phase, cap=8, debug=True):
    cmd = [clang, '-cc1', '-analyze', '-triple', 'armebv7-linux-gnueabi', '-fno-signed-char',
           '-analyzer-checker=' + checker, '-analyzer-config', 'ipa=none', '-DCAP=' + str(cap),
           '-analyzer-config', checker + ':Phase=' + phase,
           '-analyzer-config', checker + ':WorkDir=' + str(wd)]
    if debug:
        cmd += ['-analyzer-config', checker + ':Dump=inputs']
    return cmd + [source]


def record(cmd, output):
    global serial
    serial += 1
    (root / ('%02d.log' % serial)).write_text(json.dumps(cmd) + '\n' + output)


def run(wd, phase, cap=8, debug=True, ok=True):
    cmd = command(wd, phase, cap, debug)
    p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    record(cmd, p.stdout + p.stderr)
    assert (p.returncode == 0) == ok, (cmd, p.returncode, p.stderr)
    return p.stderr


def observation(output, publication):
    rows = [json.loads(line.split('] ', 1)[1]) for line in output.splitlines()
            if line.startswith('[oobsum:inputs] ')]
    assert len(rows) == 1, rows
    row = rows[0]
    assert row['schema'] == 'oobsum-inputs/1' and not row['details_scanned'], row
    assert row['conflicting_tus'] == row['incomplete_files'] == -1, row
    assert row['publication'] == publication and row['tu'], row
    assert row['error'] == '', row
    return row


def hold(wd):
    (wd / 'fir').mkdir(parents=True)
    live = wd / 'fir/other-writer.tmp'
    live.write_text('self-owned active candidate')
    return live


# 其它写者候选不影响本TU成功发布；但不能借浅观察提前宣称输入已完整。
wd = root / 'active'
live = hold(wd)
published = observation(run(wd, 'export'), 'published')
assert published['status'] == 'pending_solve' and published['pending']
assert published['publication_error'] == ''
assert not (wd / 'dump/inputs.json').exists()
records = list((wd / 'dump' / published['tu']).glob('inputs-*.json'))
assert len(records) == 1 and json.loads(records[0].read_text()) == published
canonical = next((wd / 'fir').glob('*.json')).read_bytes()
# 内联也会保存FIR；并发发布观察不能混作solve扫描。
inline = observation(run(wd, 'inline'), 'published')
assert inline['status'] == 'pending_solve'
assert len(list((wd / 'dump' / published['tu']).glob('inputs-*.json'))) == 2
run(wd, 'solve', ok=False)
state = json.loads((wd / 'dump/inputs.json').read_text())
assert state['details_scanned'] and state['incomplete_files'] == 1
assert state['status'] == 'incomplete_export' and state['publication'] == 'not_applicable'
run(wd, 'report', ok=False)
live.unlink()
run(wd, 'solve')
text = run(wd, 'report')
assert '[OOB-DEFINITE]' in text

# 真实不同内容继续报错，发布结果与工作目录标记可各自复查；候选扫描不会掩盖原错误。
conflicted = root / 'conflicted'
live = hold(conflicted)
run(conflicted, 'export')
state = observation(run(conflicted, 'export', cap=32, ok=False), 'failed')
assert state['status'] == 'conflict' and state['conflict']
assert 'conflicting FIR variants' in state['publication_error']
assert len(list((conflicted / 'fir-conflicts' / state['tu']).glob('*.json'))) == 2
live.unlink()
run(conflicted, 'solve', ok=False)
state = json.loads((conflicted / 'dump/inputs.json').read_text())
assert state['details_scanned'] and state['conflicting_tus'] == 1 and state['incomplete_files'] == 0
assert not list((conflicted / 'solved').glob('*.json'))

# 真实发布I/O失败不能被全局的pending状态掩盖成本次成功；活跃候选类型也不在发布时扫描。
broken = root / 'broken'
(broken / 'fir' / (published['tu'] + '.json')).mkdir(parents=True)
state = observation(run(broken, 'export', ok=False), 'failed')
assert state['status'] == 'publication_failed' and state['publication_error']
bad_type = root / 'candidate-type'
(bad_type / 'fir/other-writer.tmp').mkdir(parents=True)
state = observation(run(bad_type, 'export'), 'published')
assert state['status'] == 'pending_solve'
run(bad_type, 'solve', ok=False)
assert json.loads((bad_type / 'dump/inputs.json').read_text())['status'] == 'io_error'

# 不开Dump的正常发布无新增诊断；规范FIR与开Dump相同。
quiet = root / 'quiet'
live = hold(quiet)
assert '[oobsum:inputs]' not in run(quiet, 'export', debug=False)
assert next((quiet / 'fir').glob('*.json')).read_bytes() == canonical

# 多进程相同内容发布，同时另一写者反复创建/删除候选；每个进程独立留存自己的快照。
parallel = root / 'parallel'
live = hold(parallel)
stop = threading.Event()


def churn():
    path = parallel / 'fir/changing.tmp'
    while not stop.is_set():
        path.write_text('active candidate')
        path.unlink()


thread = threading.Thread(target=churn)
thread.start()
try:
    commands = [command(parallel, 'export') for _ in range(8)]
    processes = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, env=env) for cmd in commands]
    for cmd, p in zip(commands, processes):
        output, _ = p.communicate(timeout=30)
        record(cmd, output)
        assert p.returncode == 0, output
        state = observation(output, 'published')
        assert state['status'] == 'pending_solve'
finally:
    stop.set()
    thread.join()
assert len(list((parallel / 'dump' / published['tu']).glob('inputs-*.json'))) == len(processes)
assert not (parallel / 'dump/inputs.json').exists()
live.unlink()
run(parallel, 'solve')
assert '[OOB-DEFINITE]' in run(parallel, 'report')
print('export inputs: shallow / per-process / local failure / real conflict / active candidates / strict solve: OK')
