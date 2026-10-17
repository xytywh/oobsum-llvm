# replay-sync.test 的脚本（SPEC-0100）：replay.py 的 classify 与引擎对 SPEC-0095 / 0099 的写法判得一样。
import sys

sys.path.insert(0, sys.argv[1])
import replay  # noqa: E402


def req(have, end, guard):
    return {'id': 'R1', 'kind': 'W', 'have': have, 'obj': '*p0', 'end': [end, end], 'bound': 'exact', 'guard': guard,
            'access': {'fn': 'Put', 'key': 'F:Put', 'at': 'a.c:2', 'end': end}, 'path': ['Put@a.c:2'], 'depth': 0,
            'eff_depth': 0, 'blind': False}


def run(name, r, args, params=()):
    o = replay.classify(r, {'site': 't.c:3', 'callee': 'Put', 'args': args, 'results': []},
                        {'params': list(params), 'fields': {}}, None, [])
    print(name, o['outcome'], replay.show(o['have']), replay.show(o.get('end2', (o['end'][0], o['end'][1]))[1]),
          o.get('bound', ''), ' '.join(replay.show_atom_le0(g) for g in o.get('guards2', [])))


# SPEC-0095：实参偏移 [2, 6]（have 是最小剩余 10）→ 按对象起点 16、末端 19 → L2
run('range', req('ext(p0)', '13', []), [{'obj': 'local:a', 'have': '10', 'off_lo': '2', 'off_hi': '6', 'off_bound': 'branch'}])
# 偏移上界只来自类型 → 记账
run('type', req('ext(p0)', '13', []), [{'obj': 'local:a', 'have': '-239', 'off_lo': '0', 'off_hi': '255', 'off_bound': 'type'}])
# SPEC-0099：守卫 nn:p1 >= 1，实参 0 → safe；局部数组 → L1
r = req('ext(p0)', '11', ['nn:p1 >= 1'])
run('null', r, [{'obj': 'local:a', 'have': '4'}, '0'])
run('buf', r, [{'obj': 'local:a', 'have': '4'}, {'obj': 'local:b', 'have': '1'}])
# 调用方把自己的形参传下去 → 往上传，守卫换成调用方的 nn:p1
run('up', r, [{'obj': '*p0', 'have': 'ext(p0)', 'alias': 'p0'}, {'obj': '*p1', 'have': 'ext(p1)', 'alias': 'p1'}],
    [{'name': 'a', 'type': 'UINT8*'}, {'name': 'q', 'type': 'UINT8*'}])
