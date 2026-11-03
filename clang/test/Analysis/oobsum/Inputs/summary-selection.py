"""SPEC-0129：复用两 TU 测试夹具，覆盖摘要选择、零匹配、写盘失败和精确字面量过滤。

正例：真实 local/derived 可读取；反例：未知函数在启动 bxen 前失败。
边界：同名静态函数与跨目标歧义、C++ 运算符 / 空白名；回归：所有调试方式不改 solved。
本脚本由 summary-selection.test 驱动，只使用自有合成 FIR，不读取业务代码。
"""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

driver, clang, sources, output = sys.argv[1:]
root = Path(output).resolve()
root.mkdir(parents=True)
checker = 'rb.enhance.core.OobSummary'
spec = importlib.util.spec_from_file_location('driver', driver)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def run(args, code=0):
    p = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    assert 'Traceback' not in p.stderr
    return p.stdout + p.stderr


def tool(*args, code=0):
    return run([sys.executable, driver, *args], code)


plain = root / 'plain'
tool('local', '--workdir', plain, '--clang', clang, Path(sources) / 'a.c', Path(sources) / 'b.c')


def copy_fir(name):
    wd = root / name
    shutil.copytree(plain / 'fir', wd / 'fir')
    return wd


def load(wd, name):
    return json.loads((wd / 'dump/solve/summary' / name).read_text())


def solve(wd, pattern):
    stub = wd / 'stub.c'
    stub.write_text('void stub(void) {}\n')
    return run([clang, '-cc1', '-analyze', '-analyzer-checker=' + checker,
                '-analyzer-config', 'ipa=none', '-analyzer-config', checker + ':Phase=solve',
                '-analyzer-config', checker + ':WorkDir=' + str(wd), '-analyzer-config', checker + ':Dump=summary',
                '-analyzer-config', checker + ':DumpFilter=' + pattern, stub])


def same_solved(wd):
    expected = {p.name: p.read_bytes() for p in (plain / 'solved').glob('*.json')}
    assert {p.name: p.read_bytes() for p in (wd / 'solved').glob('*.json')} == expected


zero = copy_fir('zero')
text = solve(zero, '^NO_SUCH_FUNCTION$')
assert 'DumpFilter matched no functions' in text
assert 'summary available=5 matched=0 saved=0 write_failures=0' in text
assert load(zero, 'index.json') == []
assert load(zero, 'status.json') == dict(schema='oobsum-summary-status/1', available=5, matched=0,
                                       saved=0, write_failures=0, index_written=True)
same_solved(zero)
text = tool('summary', '--workdir', zero, '--list', '--contains', 'Top_A')
assert '筛选匹配 1' in text and 'Top_A' in text
text = tool('summary', '--workdir', zero, '--list', '--counts-only')
assert 'FIR 函数 5' in text and 'Top_A' not in text and 'target=' not in text
assert '空索引' in tool('summary', '--workdir', zero, '--function', 'Top_A', code=1)

# 有全局空索引时，不用以前 inline/report 留下的局部文件假冒本次求解产物。
old = zero / 'dump/old/summary/Top_A.json'
old.parent.mkdir(parents=True)
old.write_text('{"fn":"STALE"}')
assert module._summary_of(str(zero), 'old', 'Top_A') is None
shutil.copytree(plain / 'decisions', zero / 'decisions')
assert '没有可唯一读取的摘要' in tool('explain', '--workdir', zero, 'Top_A')

unknown = copy_fir('unknown')
text = tool('solve', '--workdir', unknown, '--bxen', root / 'DO_NOT_START',
            '--summary-function', 'not_a_function', '--run', code=1)
assert '尚未启动 bxen' in text and not (unknown / 'solve_stub').exists()

# bxen 在本地不可用；适配器只转交实际 clang，不模拟摘要结果。
adapter = root / 'bxen'
adapter.write_text('#!' + sys.executable + '\n' +
                   'import os, sys\n' +
                   'extra = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("-extra_args=")).split()\n' +
                   'source = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("-analysis_file="))\n' +
                   'os.execv(' + repr(clang) + ', [' + repr(clang) +
                   ', "-cc1", "-analyze", "-analyzer-checker=' + checker + '"] + extra + [source])\n')
adapter.chmod(0o755)
selected = copy_fir('selected')
text = tool('solve', '--workdir', selected, '--bxen', adapter, '--summary-function', 'Top_A',
            '--extra', '-analyzer-config ' + checker + ':Dump=stats -analyzer-config ' + checker + ':DumpFilter=NEVER', '--run')
assert '摘要已保存：1' in text
assert load(selected, 'status.json')['matched'] == 1
local = json.loads(tool('summary', '--workdir', selected, '--function', 'Top_A', '--view', 'local'))
derived = json.loads(tool('summary', '--workdir', selected, '--function', 'Top_A', '--view', 'derived'))
assert local['reps'] == [] and any(c['callee'] == 'Fill' for c in local['calls'])
assert derived['reps'][0]['level'] == 'L2' and len(derived['reps'][0]['chain']) == 2
same_solved(selected)

multiple = copy_fir('multiple')
text = tool('solve', '--workdir', multiple, '--bxen', adapter, '--summary-function', 'Init',
            '--summary-function', 'Top_A', '--summary-function', 'Top_A', '--run')
assert '摘要预检：匹配 3' in text and '摘要已保存：3' in text
assert '同名函数' in tool('summary', '--workdir', multiple, '--function', 'Init', code=1)
same_solved(multiple)

# 某个宿主退出0但未运行 checker，不能用上次的索引假装本次保存成功。
noop = root / 'noop'
noop.write_text('#!' + sys.executable + '\n')
noop.chmod(0o755)
assert '本次 solve 未更新摘要索引' in tool('solve', '--workdir', selected, '--bxen', noop,
                                       '--summary-function', 'Top_A', '--run', code=1)

# 老 checker 没有 status.json，查询仍兼容。
(selected / 'dump/solve/summary/status.json').unlink()
assert json.loads(tool('summary', '--workdir', selected, '--function', 'Top_A')) == local
index = load(selected, 'index.json')
duplicate = dict(index[0], target='another-target')
(selected / 'dump/solve/summary/index.json').write_text(json.dumps(index + [duplicate]))
assert '同名函数' in tool('summary', '--workdir', selected, '--function', 'Top_A', code=1)
assert json.loads(tool('summary', '--workdir', selected, '--function', index[0]['key'],
                       '--target', index[0]['target'])) == local

failed = copy_fir('failed')
(failed / 'dump/solve/summary/index.json').mkdir(parents=True)
text = solve(failed, '^Top_A$')
assert 'cannot save summary index' in text
assert load(failed, 'status.json')['write_failures'] == 1
assert load(failed, 'status.json')['index_written'] is False
same_solved(failed)

failed_view = copy_fir('failed-view')
entry = index[0]
(failed_view / 'dump/solve/summary' / (entry['stem'] + '.local.json')).mkdir(parents=True)
assert 'cannot save local summary' in solve(failed_view, '^Top_A$')
assert load(failed_view, 'index.json') == []  # 两种视图都成功才入索引。
assert load(failed_view, 'status.json')['saved'] == 0
assert load(failed_view, 'status.json')['write_failures'] == 1
same_solved(failed_view)

# 用实际 LLVM Regex 验证转义；函数名来自合成 FIR，避免为工具测试添加无关 C++ 源码。
operators = copy_fir('operators')
names = ['N::operator[]', 'N::operator new', 'N::operator()', 'N::operator+']
for path in sorted((operators / 'fir').glob('*.json')):
    m = json.loads(path.read_text())
    for fn in m['funcs']:
        if names:
            fn['name'] = names.pop()
    path.write_text(json.dumps(m))
chosen = ['N::operator[]', 'N::operator new', 'N::operator()', 'N::operator+']
pattern = module._literal_filter(chosen)
assert not any(c.isspace() for c in pattern)
solve(operators, pattern)
assert load(operators, 'status.json')['matched'] == 4
assert {e['fn'] for e in load(operators, 'index.json')} == set(chosen)

empty = copy_fir('empty')
for path in (empty / 'fir').glob('*.json'):
    m = json.loads(path.read_text())
    m['funcs'] = []
    path.write_text(json.dumps(m))
assert 'no available function summaries' in solve(empty, '^Top_A$')
assert load(empty, 'status.json')['available'] == 0
print('summary selection: OK')
