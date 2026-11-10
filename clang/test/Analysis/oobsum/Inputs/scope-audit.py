"""SPEC-0135自有合成统计与实际引擎实验；不包含业务数据。"""
import json
import os
from pathlib import Path
import subprocess
import sys

tool, clang, output = sys.argv[1:]
root = Path(output).resolve()
wd = root / 'stats'
(wd / 'fir').mkdir(parents=True)
(wd / 'decisions').mkdir()


def fn(key, size=1, calls=(), flags=()):
    blocks = [dict(id=i, insts=[], succ=[]) for i in range(size)]
    blocks[0]['insts'] = [dict(op='call', callee=c) for c in calls]
    return dict(key=key, name='PRIVATE_FN', blocks=blocks, flags=list(flags))


def module(name, funcs, triple='target-a'):
    (wd / 'fir' / name).write_text(json.dumps(dict(target=dict(triple=triple, ptr=4, long=4, big_endian=True), funcs=funcs)))


module('1.json', [fn('F:Entry', calls=('F:Leaf', 'F:Library', ['ind', 'PRIVATE_FN'])),
                  fn('F:Leaf', flags=['cfg_too_large']), fn('F:Big', 10, flags=['PRIVATE_FLAG']),
                  fn('F:X', calls=['F:Y']), fn('F:Y', calls=['F:X']), fn('F:NoCFG', flags=['no_cfg'])])
module('2.json', [fn('F:Entry')])
module('3.json', [fn('F:Entry')], 'target-b')
rows = [dict(schema='oobsum-decisions/1'),
        dict(schema='oobsum-decision/1', label='definite', emitted=True, depth=64, eff_depth=0, fn='PRIVATE_FN'),
        dict(schema='oobsum-decision/1', label='suspect', emitted=False, depth=1, eff_depth=2,
             reason='type_range_only', not_emitted_because='reason_filtered'),
        dict(schema='oobsum-decision/1', label='suspect', emitted=True, reason='PRIVATE_REASON')]
(wd / 'decisions/1.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))


def run(*args, code=0):
    p = subprocess.run([sys.executable, tool, *map(str, args)], capture_output=True, text=True)
    assert p.returncode == code, (p.returncode, p.stdout, p.stderr)
    assert 'PRIVATE_' not in p.stdout and '/private/' not in p.stdout
    return json.loads(p.stdout)


r = run('stats', '--workdir', wd, '--out', root / 'counts.json')
assert r == json.loads((root / 'counts.json').read_text())
assert r['raw_function_definitions'] == 8 and r['unique_function_keys'] == 7
assert r['duplicate_definitions'] == 1 and r['duplicate_external_keys'] == 1
assert r['flags_raw']['cfg_too_large'] == 1 and r['flags_unique']['cfg_too_large'] == 1
assert r['flags_raw']['no_cfg'] == 1
assert r['max_lowered_cfg_blocks'] == 10 and r['total_lowered_cfg_blocks_raw'] == 15
assert r['graph']['recursive_sccs'] == 1 and r['graph']['recursive_functions'] == 2
assert r['graph']['direct_dag_longest_edges'] is None and r['graph']['condensed_longest_edges'] == 1
assert r['graph']['calls_missing_fir_body'] == 1 and r['graph']['indirect_call_sites'] == 1
assert r['observed_depth_max'] == 64 and r['observed_eff_depth_max'] == 2
assert r['labels'] == dict(definite=1, suspect=2)
assert r['suspect_reasons'] == dict(type_range_only=1, other=1)
assert r['filtered'] == dict(reason_filtered=1)
bad_label = list(rows) + [dict(schema='oobsum-decision/1', label='PRIVATE_LABEL')]
(wd / 'decisions/bad.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in bad_label))
assert run('stats', '--workdir', wd, code=2)['read_errors'] == 1
(wd / 'decisions/bad.jsonl').unlink()
(wd / 'inputs-pending').mkdir()
assert run('stats', '--workdir', wd, '--fir-only')['input_guard']['pending'] is True
(wd / 'fir/bad.json').write_text('broken')
assert run('stats', '--workdir', wd, '--fir-only', code=2)['read_errors'] == 1
(wd / 'fir/bad.json').unlink()
(wd / 'fir-conflicts').mkdir()
assert run('stats', '--workdir', wd, '--fir-only', code=2)['audit_complete'] is False
empty = root / 'no-logs'
(empty / 'fir').mkdir(parents=True)
(empty / 'fir/a.json').write_text((wd / 'fir/1.json').read_text())
assert run('stats', '--workdir', empty, code=2)['audit_complete'] is False
assert run('stats', '--workdir', empty, '--fir-only')['audit_complete'] is True
os.environ['OOBSUM_CONFIG'] = 'Phase=export WorkDir=' + str(root / 'DO_NOT_TOUCH')
r = run('bench', '--clang', clang, '--out', root / 'bench', '--depths', '1,4,16', '--timeout', '20')
assert not (root / 'DO_NOT_TOUCH').exists()
os.environ.pop('OOBSUM_CONFIG', None)
assert r['all_passed'] is True and len(r['cases']) == 5
by_name = {c['case']: c for c in r['cases']}
assert by_name['chain-16']['stats']['max_lowered_cfg_blocks'] == 3
assert by_name['chain-16']['stats']['total_lowered_cfg_blocks_raw'] > 3
assert by_name['chain-16']['stats']['observed_depth_max'] == 16
assert by_name['chain-16']['stats']['graph']['direct_dag_longest_edges'] == 16
assert by_name['wide-low']['stats']['flags_unique']['cfg_too_large'] == 1
assert by_name['wide-high']['stats']['emitted_labels']['definite'] == 1
assert (root / 'bench/results.json').is_file() and list((root / 'bench').glob('*/stderr.log'))
print('scope audit: graph / counters / privacy / incomplete / real depth and CFG pairs: OK')
