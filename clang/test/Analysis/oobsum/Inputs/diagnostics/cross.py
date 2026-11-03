"""跨 TU 的访问在 b.c sink 报告；包含完整链，实际源码和 caret 紧随主 warning。"""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
output = Path(sys.argv[2]).read_text().splitlines()
rows = [json.loads(line) for path in (root / 'decisions').glob('*.jsonl')
        for line in path.read_text().splitlines()[1:]]
row = next(d for d in rows if d['fn'] == 'F:Top_A' and d['label'] == 'possible')
assert row['site'].endswith('/a.c:16:0')
assert row['sink'].rsplit(':', 2)[0].endswith('/b.c')
assert 'Top_A (a.c:16) -> Fill (b.c:16)' in row['msg']
assert 'Context: Fill(aucBuf, 17)' in row['msg']
for i, text in enumerate(output):
    if 'warning: [OOB-POSSIBLE]' in text:
        assert 'b.c:16:' in text and 'Call chain:' in text
        assert '^' in output[i + 2], output[i:i + 4]
        break
else:
    raise AssertionError('no POSSIBLE warning at the sink')
