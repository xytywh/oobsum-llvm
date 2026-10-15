#!/usr/bin/env python3
"""core/Range 的金标准（oobsum/specs/SPEC-0011 §5）：下面列的查询，用 replay.py 的 Env / entails_le0（参考实现）算出结果，
写成 range-golden.txt，C++ 的 RangeTest 逐条比对。

用法：python3 -I gen_range_golden.py
格式（制表符分隔）：
  R <形参类型,…> <字段名:类型;…> <环境守卫 条件@标签;…> <式子> <hi|lo> <值|none> <标签|->
  E <形参类型,…> <字段名:类型;…> <环境守卫 条件@标签;…> <d> <守卫 条件;…> <1|0>
只依赖 Python 3 标准库。
"""
import importlib.util, os

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('replay', os.path.join(HERE, '..', '..', '..', 'cases', 'summaries', 'replay.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

# (形参类型, 字段, 环境守卫, 式子, 上 / 下)
RANGES = [
    ('UINT32', '', '', 'p0', 'hi'), ('UINT32', '', '', 'p0', 'lo'), ('INT32', '', '', 'p0', 'lo'),
    ('CHAR', '', '', 'p0', 'lo'), ('size_t', '', '', 'p0', 'hi'), ('UINT8 *', '', '', 'p0', 'hi'),
    ('UINT32', '', 'p0 <= 15@check', '4*p0 + 4', 'hi'), ('UINT32', '', 'p0 < 16@loop', 'p0', 'hi'),
    ('UINT32', '', 'p0 >= 3@check', 'p0', 'lo'), ('UINT32', '', '2*p0 <= 7@check', 'p0', 'hi'),
    ('UINT32', '', '3*p0 >= 7@check', 'p0', 'lo'), ('UINT32', '', 'p0 <= 1099511627776@guard', 'p0', 'hi'),
    ('UINT32', '', 'p0 <= 15@check;p0 <= 20@loop', 'p0', 'hi'), ('UINT32', '', 'p0 <= 20@loop;p0 <= 15@check', 'p0', 'hi'),
    ('UINT32,UINT32', '', 'p0 + p1 <= 10@check', 'p0', 'hi'), ('UINT32', '', '', 'ext(p0)', 'hi'),
    ('UINT32', '', '', 'ext(p0)', 'lo'), ('UINT8 *,UINT32', '', '', 'ext(p0) - 4*p1', 'hi'),
    ('UINT8 *,UINT32', '', 'p1 >= 1@check', '16 - 4*p1', 'hi'), ('UINT8 *,UINT32', '', 'p1 <= 100@check', 'div(p1 + 31, 32)', 'hi'),
    ('UINT8 *,UINT32', '', 'p1 <= 100@check', 'div(p1 + 31, 32)', 'lo'), ('UINT32', '', '', 'mod(p0, 8)', 'hi'),
    ('UINT32', '', '', 'mod(p0, 8)', 'lo'), ('UINT8 *,UINT32', '', 'p1 >= 1@check', 'wrap32(p1 - 1)', 'hi'),
    ('UINT8 *,UINT32', '', '', 'wrap32(p1 - 1)', 'hi'), ('UINT8 *,UINT32', '', '', 'wrap32(p1 - 1)', 'lo'),
    ('UINT32', '', 'p0 <= 100@check', 'wrap16(p0)', 'hi'), ('UINT32', '', 'p0 <= 100@check', 'wrap16(p0)', 'lo'),
    ('UINT32,UINT32', '', 'p0 <= 4@check;p1 <= 8@loop', 'mul(p0, p1)', 'hi'), ('UINT8 *', 'p0->ulLen:UINT16', '', 'p0->ulLen', 'hi'),
    ('UINT32', '', '', 'g:g_x', 'hi'), ('UINT32', '', '', 'i', 'lo'), ('UINT32', '', '', '5', 'hi'),
    ('UINT32,UINT32', '', 'p0 <= 3@loop;p1 <= 4@check', 'p0 + p1', 'hi'), ('UINT32,UINT32', '', 'p0 <= 3@guard;p1 <= 4@mask', 'p0 + p1', 'hi'),
    ('UINT32,UINT32', '', 'p1 <= 4@check', 'p0 + p1', 'hi'), ('UINT32', '', 'p0 <= 4@check', '10 - p0', 'lo'),
    ('UINT32', '', 'p0 <= 4@check', '10 - p0', 'hi'), ('UINT32', '', 'p0 == 7@branch', 'p0', 'lo'),
    ('UINT8 *,UINT32', 'p0->ulLen:UINT32', 'p0->ulLen <= 64@check', '2*p0->ulLen + p1', 'hi'),
]
# (形参类型, 字段, 环境守卫, d, 守卫)
ENTAILS = [
    ('UINT32', '', 'p0 <= 15@check', '4*p0 + 4 - 64', ''), ('UINT32', '', '', '4*p0 - 60', 'p0 <= 15'),
    ('UINT32,UINT32', '', '', 'p0 - p1', 'p0 <= p1'), ('UINT32,UINT32', '', '', 'p0 - p1 + 1', 'p0 <= p1'),
    ('UINT8 *,UINT32', '', '', '16*p1 - 12 - ext(p0)', '16*p1 <= ext(p0)'), ('UINT32,UINT32', '', '', 'p0 - 10', 'p1 <= 3'),
    ('UINT32', '', '', '2*p0 - 7', 'p0 <= 3'), ('UINT32,UINT32', '', 'p1 <= 5@check', 'p0 + p1 - 10', 'p0 <= 4'),
    ('UINT32', '', '', 'p0 - 5', 'p0 >= 3'), ('UINT8 *', '', '', 'ext(p0) - 4', ''),
    ('UINT8 *', '', '', '4 - ext(p0)', 'ext(p0) >= 8'), ('UINT32,UINT32', '', '', 'p0 + p1 - 9', 'p0 <= 4;p1 <= 5'),
    ('UINT32,UINT32', '', '', 'p0 + p1 - 8', 'p0 <= 4;p1 <= 5'), ('UINT32', '', '', 'div(p0, 2) - 3', 'div(p0, 2) <= 3'),
]


def env_of(params, fields, guards):
    ps = [{'type': t} for t in params.split(',')] if params else []
    fs = {f.split(':')[0]: {'type': f.split(':')[1]} for f in fields.split(';')} if fields else {}
    env = replay.Env(ps, fs)
    for g in guards.split(';') if guards else []:
        cond, tag = g.rsplit('@', 1)
        for atom in replay.C(cond):
            env.add_guard(atom, tag)
    return env


def main():
    rows = []
    for params, fields, guards, expr, side in RANGES:
        v, tag = env_of(params, fields, guards).range_of(replay.P(expr), side == 'hi')
        rows.append('R\t%s\t%s\t%s\t%s\t%s\t%s\t%s' % (params, fields, guards, expr, side,
                                                     'none' if v is None else int(v), tag or '-'))
    for params, fields, guards, d, conds in ENTAILS:
        atoms = [a for c in conds.split(';') if c for a in replay.C(c)]
        ok = replay.entails_le0(replay.P(d), atoms, env_of(params, fields, guards))
        rows.append('E\t%s\t%s\t%s\t%s\t%s\t%d' % (params, fields, guards, d, conds, 1 if ok else 0))
    with open(os.path.join(HERE, 'range-golden.txt'), 'w', encoding='utf-8') as f:
        f.write('# 由 gen_range_golden.py 用 replay.py 的 Env / entails_le0 生成，不要手改\n')
        f.write('\n'.join(rows) + '\n')
    print('range-golden.txt：%d 条（R %d，E %d）' % (len(rows), len(RANGES), len(ENTAILS)))


if __name__ == '__main__':
    main()
