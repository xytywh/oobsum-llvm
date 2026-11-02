"""同名跨目标的两份 Entry 都要有判定；摘要文件和 explain 查询不能混用。"""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
root = Path(sys.argv[1])
found = {}
for path in (root / 'solved').glob('*.json'):
    tu = json.loads(path.read_text())
    rows = [d for d in tu['decisions'] if d['fn'] == 'F:Entry']
    found[Path(tu['file']).name] = (tu['tu'], rows)
assert any(d['label'] == 'definite' for d in found['a32.c'][1])
assert found['a64.c'][1] and all(d['label'] == 'safe' for d in found['a64.c'][1])
index = json.loads((root / 'dump/solve/summary/index.json').read_text())
assert len(index) == len({e['stem'] for e in index}) == 2
assert len({e['target'] for e in index}) == 2
spec = importlib.util.spec_from_file_location('driver', sys.argv[2])
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)
for name, size in [('a32.c', '4'), ('a64.c', '16')]:
    summary = driver._summary_of(str(root), found[name][0], 'Entry')
    assert summary is not None and summary['calls'][0]['args'][0]['have'] == size
