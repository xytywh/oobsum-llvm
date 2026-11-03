"""检查真实 AST 产物：结构体步长、字段偏移、同一行两个访问、复制参数与全局来源。"""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
module = json.loads(next((root / 'fir').glob('*.json')).read_text())
assert 'sample text' not in json.dumps(module)
instructions = {}
for function in module['funcs']:
    for block in function['blocks']:
        for inst in block['insts']:
            if inst.get('diag'):
                instructions[function['key'], inst['diag']] = inst
rows = []
for path in (root / 'decisions').glob('*.jsonl'):
    rows.extend(json.loads(line) for line in path.read_text().splitlines()[1:])
bad = next(d for d in rows if d['fn'] == 'F:Bad' and d['label'] == 'definite')
info = bad['accesses'][0]
assert info['expression'] == 'p[i].value'
assert (info['element_type'], info['element_size'], info['width'], info['field_offset']) == ('Packet', 20, 4, 16)
inst = instructions[bad['access_key'], bad['access_ids'][0]]
assert bad['sink'].endswith(':%d:%d' % tuple(inst['loc'][1:]))
same = [d for d in rows if d['fn'] == 'F:SameLine' and d['label'] == 'definite']
assert {d['accesses'][0]['expression'] for d in same} == {'a[2]', 'b[4]'}
assert len({d['access_ids'][0] for d in same}) == 2
global_read = next(d for d in rows if d['fn'] == 'F:Global' and d['reason'] == 'untracked_pointer')
assert global_read['accesses'][0]['expression'] == 'p[i].value'
assert "global pointer 'g_table'" in global_read['accesses'][0]['origin']
copy = [d for d in rows if d['fn'] == 'F:Copy' and d.get('accesses')]
assert any(d['kind'] == 'W' and d['accesses'][0]['expression'] == 'd' for d in copy)
assert any(d['kind'] == 'R' and d['accesses'][0]['expression'] == 's' for d in copy)

assert all(d['accesses'][0]['element_size'] == 1 for d in copy)
assert global_read['accesses'][0]['mode'] == 'R'
