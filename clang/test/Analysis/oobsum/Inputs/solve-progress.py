"""校验真实 solve 产物：local 是输入事实，derived 包含跨 TU 结论；只输出计数的进度无源码。"""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
progress = json.loads((root / 'dump/solve/progress.json').read_text())
assert progress['stage'] == 'done' and progress['completed'] == progress['total'] == 2
assert progress['elapsed_s'] >= 0 and progress['pid'] > 0
index = json.loads((root / 'dump/solve/summary/index.json').read_text())
assert len(index) == 1 and index[0]['fn'] == 'Top_A'
stem = index[0]['stem']
local = json.loads((root / ('dump/solve/summary/' + stem + '.local.json')).read_text())
derived = json.loads((root / ('dump/solve/summary/' + stem + '.derived.json')).read_text())
assert local['reps'] == [] and any(c['callee'] == 'Fill' for c in local['calls'])
assert derived['reps'][0]['level'] == 'L2'
assert len(derived['reps'][0]['chain']) == 2
