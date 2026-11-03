"""检查策略不伪造 SAFE，普通指针路径 / 固定内层界不被尾数组掩盖。"""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
def read(directory):
    return [json.loads(line) for path in (root / directory / 'decisions').glob('*.jsonl')
            for line in path.read_text().splitlines()[1:]]

default, report, zero = [read(d) for d in ('default', 'report', 'zero')]
def rows(data, name):
    return [d for d in data if d['fn'] == 'F:' + name and d['label'] != 'safe']

for name in ['Flex', 'Zero', 'One', 'Four', 'Both', 'Returned', 'Field']:
    data = rows(default, name)
    assert data, name
    assert all(not d['emitted'] and d.get('not_emitted_because') == 'flexible_array' for d in data), (name, data)
    assert any(d['emitted'] for d in rows(report, name)), name
for name in ['Five', 'NotLast', 'UnionCase', 'Array', 'Mixed', 'Inner', 'HeapMixed']:
    assert any(d['emitted'] for d in rows(default, name)), (name, rows(default, name))
for name in ['Flex', 'Zero']:
    assert all(not d['emitted'] for d in rows(zero, name)), name
for name in ['One', 'Four']:
    assert any(d['emitted'] for d in rows(zero, name)), name
# 打开报告只变 disposition / group，不改变判断标签或所需字节。
semantic = lambda ds: sorted((d['fn'], d['label'], d['reason'], d['have'], tuple(d['end']), d['fp']) for d in ds)
assert semantic(default) == semantic(report)
