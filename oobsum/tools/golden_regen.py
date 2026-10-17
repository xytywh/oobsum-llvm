#!/usr/bin/env python3
"""金标准重算（oobsum/specs/SPEC-0019 §2.4）：有意修改金标准的局部事实之后，按 replay.py 的规则把推出来的部分重算并写回。

局部事实（不动）：depth 0 的 Req 及其 absorbed、调用点实参 / 局部区间 / 守卫、效果、模型调用的 results、不挂 Req 的账本。
重算：上传的 Req（depth ≥ 1；已有的按键保留原 id、doc、absorbed，新的补 id 与 start）、对已定义被调者的调用点 results、
入口的账本（opens 里挂 Req 的那些；已有的保留 doc）。判定（reps）不改写：推出的判定若与 JSON 不同就报错、不写。

用法：
  python3 oobsum/tools/golden_regen.py LV5-01 …           # 只打印会改哪些函数
  python3 oobsum/tools/golden_regen.py --write LV5-01 …   # 写回 cases/summaries/<用例>/<函数>.json（排版同 replay.pretty）
写回后跑 python3 oobsum/cases/summaries/replay.py 确认 ① ② 都过。只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True  # 不在 cases/summaries 里留 __pycache__
import argparse, copy, importlib.util, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARIES = os.path.abspath(os.path.join(HERE, '..', 'cases', 'summaries'))

spec = importlib.util.spec_from_file_location('replay', os.path.join(SUMMARIES, 'replay.py'))
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)


def next_id(used, prefix='R'):
    n = 1
    while '%s%d' % (prefix, n) in used:
        n += 1
    used.add('%s%d' % (prefix, n))
    return '%s%d' % (prefix, n)


def dominated_by(r, keep):
    """r 并进 keep 里的哪一条（同 replay.absorb 的条件；r 自己留下时就是它自己）。"""
    for k in keep:
        if k is r:
            return k
    for k in keep:
        if R.show(r['have']) == R.show(k['have']) and r['obj'] == k['obj'] and r['gkey'] == k['gkey'] and \
                r.get('dir') == k.get('dir'):
            d = (r['hi'] - k['hi']).const()
            if d is not None and d <= 0:
                return k
    return None


def regen(cid):
    """返回 (新的带 doc 的摘要 {fn: json}, 有改动的函数列表, 错误列表)。"""
    meta, docs = R.load_case(cid)
    fns = R.machine(docs)
    order = R.topo(fns)
    callers = R.callers_of(fns)
    out = copy.deepcopy(docs)
    final = {}  # fn → 重算后的 Req（机器版，含 id），给调用方用
    errors = []
    for fn in order:
        s = fns[fn]
        d = out[fn]
        old_by_key = {R.json_req_key(r): r for r in d['reqs'] if r['depth'] != 0}
        used = {r['id'] for r in d['reqs']}
        cand = []
        local_json = {}
        for r in d['reqs']:
            if r['depth'] != 0:
                continue
            g = [x for c in r.get('guard', []) for x in R.C(c)]
            base = {'have': R.P(r['have']), 'obj': r['obj'], 'guards': g, 'gkey': frozenset(R.show_atom_le0(x) for x in g),
                    'depth': 0, 'eff_depth': r['eff_depth'], 'bound': r['bound'], 'path': r['path'], 'access': r['access'],
                    'src': 'local', 'dir': r.get('dir')}
            main = dict(base, lo=R.P(r['end'][0]), hi=R.P(r['end'][1]), kind=r['kind'], json=r)
            cand.append(main)
            local_json[id(main)] = r
            for a in r.get('absorbed', []):
                cand.append(dict(base, lo=R.P(a['end'][0]), hi=R.P(a['end'][1]), kind=a['kind'], src='absorbed', at=a['at']))
        pending = []  # (call, 被调者的 Req, 结论)
        for call in s['calls']:
            callee = fns.get(call['callee'])
            if callee is None:
                continue
            env_guards = [(g, x.get('bound', 'guard')) for x in call.get('guards', []) for g in R.C(x['cond'])]
            m, _ = R.build_map(call, callee)
            for r in final[call['callee']]:
                for mine, ucall, outer in R.classify_all(r, call, s, callee, env_guards):   # 外层对象那一条（SPEC-0035）
                    c = None
                    if mine['outcome'] == 'up':
                        g = mine.get('guards2', [])
                        mm = R.build_map(ucall, callee)[0]
                        c = {'have': mine['have'], 'obj': mine['obj'], 'lo': mine['end2'][0], 'hi': mine['end2'][1], 'guards': g,
                             'gkey': frozenset(R.show_atom_le0(x) for x in g), 'depth': mine['depth'], 'eff_depth': mine['eff_depth'],
                             'bound': mine['bound'], 'path': ['%s@%s' % (fn, call['site'])] + r['path'], 'access': r['access'],
                             'kind': r['kind'], 'src': 'up', 'blind': r['blind'], 'dir': mine.get('dir') or r.get('dir'),
                             'start': R.subst(R.P(r['start']), mm) if 'start' in r else None}
                        cand.append(c)
                    pending.append((call, r, mine, c, ucall, outer))
        keep = R.absorb(cand)
        # 留下的 → JSON
        reqs = []
        json_of = {}
        for k in keep:
            if k['src'] == 'local':
                j = local_json[id(k)]
            elif k['src'] == 'absorbed':
                errors.append('%s %s：absorbed 里的访问 %s 留了下来（局部事实本身要改）' % (cid, fn, k['at']))
                continue
            else:
                key = R.req_key(k['have'], k['obj'], k['lo'], k['hi'], k['guards'], k['depth'], k.get('dir'))
                j = old_by_key.get(key)
                if j is None:
                    j = {'id': next_id(used), 'kind': k['kind']}
                    if k.get('dir'):
                        j['dir'] = k['dir']  # 负偏移（SPEC-0039）
                    j.update({'have': R.show(k['have']), 'obj': k['obj'], 'end': [R.show(k['lo']), R.show(k['hi'])]})
                    if k['start'] is not None:
                        j['start'] = R.show(k['start'])
                    j.update({'bound': k['bound'], 'guard': [R.show_atom_le0(x) for x in k['guards']], 'access': k['access'],
                              'path': k['path'], 'depth': k['depth'], 'eff_depth': k['eff_depth'], 'blind': k['blind']})
                # 新并进来的本函数局部访问记进 absorbed
                for c in cand:
                    if c['src'] in ('local', 'absorbed') and c is not k and dominated_by(c, keep) is k:
                        at = c['access']['at'] if c['src'] == 'local' else c['at']
                        entry = {'kind': c['kind'], 'end': [R.show(c['lo']), R.show(c['hi'])], 'at': at}
                        if entry not in j.setdefault('absorbed', []):
                            j['absorbed'].append(entry)
            json_of[id(k)] = j
            reqs.append(j)
        # 已有的按原来的顺序，新的接在后面
        position = {id(j): i for i, j in enumerate(d['reqs'])}
        ranked = [(position.get(id(j), len(position) + n), j) for n, j in enumerate(reqs)]
        reqs = [j for _, j in sorted(ranked, key=lambda x: x[0])]
        d['reqs'] = reqs
        final[fn] = [R.strip_doc(j) for j in reqs]
        # 调用点 results
        reps = {(x['site'], json.dumps(x['access'], sort_keys=True)): x for x in d['reps']}
        new_results = {}
        for call, r, mine, c, ucall, outer in pending:
            res = {'req': r['id'], 'outcome': mine['outcome']}
            if outer:
                res['parent'] = True
            if mine['outcome'] == 'up':
                k = dominated_by(c, keep)
                res['as'] = json_of[id(k)]['id']
            elif mine['outcome'] in ('L1', 'L2'):
                got = R.rep_of(mine, ucall, fn, s, r)
                want = reps.get((got['site'], json.dumps(got['access'], sort_keys=True)))
                bad = R.diff_rep(want, got) if want else ['JSON 里没有这条判定']
                if bad:
                    errors.append('%s %s @ %s：判定变了（%s），本工具不改写判定' % (cid, fn, call['site'], '；'.join(bad)))
                    continue
                res['report'] = want['id']
            elif mine['outcome'] == 'open':
                res['reason'] = mine['reason']
            new_results.setdefault((call['site'], call['callee']), []).append(res)
        for call in d['calls']:
            if fns.get(call['callee']) is None:
                continue
            effs = [x for x in call.get('results', []) if 'eff' in x]
            call['results'] = new_results.get((call['site'], call['callee']), []) + effs
        # 入口账本
        if fn not in callers:
            old_opens = {(o.get('req'), o['reason']): o for o in d['opens'] if o.get('req')}
            opens = [o for o in d['opens'] if not o.get('req')]
            env = R.Env(s.get('params', []), s.get('fields', {}))
            for k in keep:
                j = json_of.get(id(k))
                if j is None:
                    continue
                _, tag = env.range_of(k['hi'] - k['have'], True)
                reason = 'type_range_only' if k['have'].const() is not None and tag == 'type' else 'entry_unproven'
                if R.typed_field(k, s.get('params', [])):
                    reason = 'typed_field'
                if k.get('dir') in ('sub', 'subback'):
                    reason = 'subobject_overflow'  # 内层 Req（SPEC-0104）
                o = old_opens.get((j['id'], reason))
                if o is None:
                    o = {'reason': reason, 'at': j['access']['at'] if j['depth'] == 0 else d['loc'], 'req': j['id']}
                opens.append(o)
            d['opens'] = opens
    changed = [fn for fn in order if out[fn] != docs[fn]]
    return out, changed, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cases', nargs='+')
    ap.add_argument('--write', action='store_true')
    a = ap.parse_args()
    bad = False
    for cid in a.cases:
        out, changed, errors = regen(cid)
        for e in errors:
            print('✗', e)
        bad = bad or bool(errors)
        print('%-8s 要改 %d 个函数：%s' % (cid, len(changed), '、'.join(changed) or '-'))
        if a.write and not errors:
            for fn in changed:
                with open(os.path.join(SUMMARIES, cid, fn + '.json'), 'w', encoding='utf-8') as f:
                    f.write(R.pretty(out[fn]) + '\n')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
