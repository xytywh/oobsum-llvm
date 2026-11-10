"""SPEC-0134：纯合成运行产物验证分阶段比较，不读取公司数据。"""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys

tool, output = sys.argv[1:]
root = Path(output).resolve()
a, b = root / 'a', root / 'b'
for directory, suffix in [('fir', '.json'), ('solved', '.json'), ('decisions', '.jsonl')]:
    (a / directory).mkdir(parents=True)
    (a / directory / ('tu' + suffix)).write_text('{"schema":"synthetic", "private_name":"PRIVATE_NAME"}\n')
row = dict(description='rb.enhance.core.OobSummary', file='/private/data.c', line=1,
           reason='warning: [OOB-DEFINITE] PRIVATE_NAME', md5='111', language='c/c++')
other = dict(row, line=2, md5='222')
(a / 'report.json').write_text(json.dumps({'problems': [row, other]}))
shutil.copytree(a, b)


def run(code=0, *extra):
    p = subprocess.run([sys.executable, tool, str(a), str(b), '--json', *extra], capture_output=True, text=True)
    assert p.returncode == code, (p.returncode, p.stdout, p.stderr)
    assert 'PRIVATE_NAME' not in p.stdout and '/private/data.c' not in p.stdout
    result = json.loads(p.stdout)
    return result


assert run()['equal'] is True
(b / 'report.json').write_text(json.dumps({'problems': [other, row]}, indent=3))
assert run()['equal'] is True
changed = copy.deepcopy(row)
changed['md5'] = '333'
(b / 'report.json').write_text(json.dumps({'problems': [changed, other]}))
r = run(1)
assert r['first_change'] == 'report' and r['stages']['report']['only_md5_differs'] is True
changed['reason'] += ' different'
(b / 'report.json').write_text(json.dumps({'problems': [changed, other]}))
r = run(1)
assert r['stages']['report']['without_md5'] == {'removed': 1, 'added': 1}
(b / 'report.json').write_text(json.dumps({'problems': [row, other, row]}))
r = run(1, '--details-json', str(root / 'details.json'))
assert r['stages']['report']['exact_records'] == {'removed': 0, 'added': 1}
details = json.loads((root / 'details.json').read_text())
assert details['report']['added'][0]['occurrences'] == 1
assert len(details['report']['added'][0]['records']) == 1
assert 'PRIVATE_NAME' not in (root / 'details.json').read_text()
shutil.copyfile(a / 'report.json', b / 'report.json')
for folder, suffix in [('decisions', '.jsonl'), ('solved', '.json'), ('fir', '.json')]:
    path = b / folder / ('tu' + suffix)
    original = path.read_bytes()
    path.write_bytes(original + b' ')
    assert run(1)['first_change'] == folder
    path.write_bytes(original)
(b / 'fir/new.json').write_text('{}')
assert run(1)['stages']['fir']['added'] == 1
(b / 'fir/new.json').unlink()
(a / 'inputs-pending').mkdir()
assert run(2)['guards']['left']['invalid'] is True
assert run(0, '--through', 'export')['equal'] is True
(a / 'inputs-pending').rmdir()
(a / 'fir-conflicts').mkdir()
assert run(2, '--through', 'export')['complete'] is False
(a / 'fir-conflicts').rmdir()
alternate = root / 'alternate-report.json'
shutil.copyfile(b / 'report.json', alternate)
(b / 'report.json').unlink()
assert run(2)['errors'][0]['stage'] == 'report'
assert run(0, '--report-right', str(alternate))['equal'] is True
shutil.rmtree(b / 'fir')
assert any(e['stage'] == 'fir' for e in run(2)['errors'])
print('compare-runs: stages / order / md5 / duplicates / privacy / incomplete: OK')
