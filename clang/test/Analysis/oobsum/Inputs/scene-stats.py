"""SPEC-0128：从真实标签与独立合成统计输入核查口径，输出不得含业务名字。"""
import json
from pathlib import Path
import subprocess
import sys

tool, root = Path(sys.argv[1]), Path(sys.argv[2])
def run(*args, ok=True):
    result = subprocess.run([sys.executable, str(tool)] + list(args), capture_output=True, text=True)
    assert (result.returncode == 0) == ok, result.stderr
    assert 'MUST_NOT_PRINT_NAME' not in result.stdout
    return json.loads(result.stdout) if ok else result.stderr

real = run('--workdir', str(root / 'real'), '--json')
assert real['scenes']['LV3'].get('possible', 0) >= 1
directory = root / 'fake/decisions'
directory.mkdir(parents=True)
group = '123456789abcdef0'
rows = [{'schema': 'oobsum-decisions/1'},
        {'label': 'definite', 'emitted': True, 'report_group': group, 'fp': 'a', 'scenes': [3], 'fn': 'MUST_NOT_PRINT_NAME'},
        {'label': 'suspect', 'emitted': True, 'report_group': group, 'fp': 'b', 'scenes': [7, 9]},
        {'label': 'possible', 'emitted': False, 'fp': 'c', 'scenes': [2]}]
(directory / 'x.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
common = ['--workdir', str(root / 'fake'), '--json']
physical = run(*common)
assert physical['judgments'] == 2 and physical['units'] == 1
assert physical['scenes']['LV9']['definite'] == 1
multi = run(*(common + ['--multi']))
assert all(multi['scenes'][name]['definite'] == 1 for name in ['LV3', 'LV7', 'LV9'])
raw = run(*(common + ['--unit', 'decisions', '--include-unemitted']))
assert raw['units'] == 3 and raw['scenes']['LV2']['possible'] == 1
annotation = root / 'annotations.json'
annotation.write_text(json.dumps({group: ['LV10']}))
manual = run(*(common + ['--annotations', str(annotation)]))
assert manual['scenes']['LV10']['definite'] == 1
report = root / 'report.json'
report.write_text(json.dumps({'problems': [{'description': 'rb.enhance.ipa.OobSummary', 'reason': '[OOB-POSSIBLE] old'}]}))
old = run('--report', str(report), '--json')
assert old['scenes']['UNCLASSIFIED']['possible'] == 1
assert 'Traceback' not in run('--report', str(root / 'missing'), ok=False)
