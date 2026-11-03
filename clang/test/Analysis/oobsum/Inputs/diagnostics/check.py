"""真实判断验证 3 个元素的 i<n / i<=n，以及 36 个元素的 MIN 上界 96 / 36。"""
import json
from pathlib import Path
import sys

root, output = Path(sys.argv[1]), Path(sys.argv[2]).read_text().splitlines()
rows = [json.loads(line) for path in (root / 'decisions').glob('*.jsonl')
        for line in path.read_text().splitlines()[1:]]
for name in ['Correct', 'Clamped']:
    assert not any(d['fn'] == 'F:' + name and d['emitted'] and d['label'] != 'safe' for d in rows)
wrong = next(d for d in rows if d['fn'] == 'F:OffByOne' and d['emitted'])
assert '64' == wrong['end'][1] and '60' == wrong['have']
assert '20 bytes each, 3 complete elements' in wrong['msg']
assert 'indices relative to this pointer: [0, 3], valid [0, 2]' in wrong['msg']
large = next(d for d in rows if d['fn'] == 'F:TooLarge' and d['emitted'])
assert large['label'] == 'possible' and '36 complete elements' in large['msg']
same = [d for d in rows if d['fn'] == 'F:SameLine' and d['emitted']]
assert len(same) >= 3 and len({d['report_group'] for d in same}) == 1
assert sum(d['diagnostic_primary'] for d in same) == 1
assert {a['expression'] for d in same for a in d['accesses']} >= {'a[2]', 'b[4]', 'a[i]'}
positions = []
for i, line in enumerate(output):
    if ': warning: [OOB-' in line:
        assert i + 2 < len(output) and '^' in output[i + 2], output[i:i + 4]
        positions.append(line.split(': warning:')[0].rsplit(':', 1)[0])
assert len(positions) == len(set(positions)), positions
assert list((root / 'dump').glob('*/diagnostics.jsonl'))
