#!/usr/bin/env python3
"""摘要实样回放器：读每个函数的摘要 JSON，在每个调用点做"代换 → 判定"，核对 JSON 里记录的结论，再对照用例头的 @expect。

它是 06-spec.md §6.6（调用点五步的第 ②③④ 步）与 F-10…F-22（判定与报告）的**参考实现**：
C++ 引擎在这些用例上产出的摘要，必须与这里的 JSON 一致（06- T-06 快照测试的金标准）。
它不生成摘要（那是引擎从 FIR 算出来的），只验证"摘要这种形式足够把多层越界串起来判出来"，
并把每一步代换打印出来，方便看、方便调试。

用法：
    python3 replay.py                     # 回放全部用例，逐条核对
    python3 replay.py LV4-01 -v           # 只回放一个用例，打印每一步
    python3 replay.py --md out.md         # 生成逐用例的走查文档（摘要 JSON + 回放过程）
只依赖 Python 3 标准库。
"""
import argparse
import json
import os
import re
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.dirname(HERE)
CODE_TAGS = {'exact', 'const', 'check', 'mask', 'branch', 'loop', 'ret', 'guard'}
INLINE_DEPTH = 2
TYPE_MAX = {'UINT8': 0xFF, 'UINT16': 0xFFFF, 'UINT32': 0xFFFFFFFF, 'size_t': 0xFFFFFFFF, 'CHAR': 0x7F, 'INT32': 0x7FFFFFFF}
TYPE_MIN = {'INT32': -0x80000000, 'CHAR': -0x80}

# ---------------------------------------------------------------- 表达式
# 线性式：常数 k + Σ c·atom。atom 是可哈希的元组：
#   ('s', name)                 符号：p0、p1->ulLen、g:g_x、调用方局部 i
#   ('ext', name)               ext(p0)、ext(p0->pucBuf)
#   ('div', key, n) ('mod', key, n) ('wrap', bits, key) ('mul', key, key)   不透明项，key 是内层线性式的冻结形式


class Lin(object):
    __slots__ = ('k', 't')

    def __init__(self, k=0, t=None):
        self.k = k
        self.t = {a: c for a, c in (t or {}).items() if c != 0}

    def const(self):
        return self.k if not self.t else None

    def key(self):
        return (self.k, tuple(sorted(self.t.items(), key=lambda x: atom_order(x[0]))))

    def __add__(self, o):
        t = dict(self.t)
        for a, c in o.t.items():
            t[a] = t.get(a, 0) + c
        return Lin(self.k + o.k, t)

    def __neg__(self):
        return Lin(-self.k, {a: -c for a, c in self.t.items()})

    def __sub__(self, o):
        return self + (-o)

    def scale(self, c):
        return Lin(self.k * c, {a: v * c for a, v in self.t.items()})

    def __eq__(self, o):
        return isinstance(o, Lin) and self.key() == o.key()

    def __hash__(self):
        return hash(self.key())

    def __repr__(self):
        return show(self)


def unfreeze(key):
    return Lin(key[0], dict(key[1]))


def atom_order(a):
    kind = a[0]
    if kind == 's':
        n = a[1]
        m = re.match(r'p(\d+)$', n)
        if m:
            return (0, int(m.group(1)), n)
        m = re.match(r'p(\d+)->', n)
        if m:
            return (1, int(m.group(1)), n)
        if n.startswith('g:'):
            return (3, 0, n)
        return (4, 0, n)
    if kind == 'ext':
        return (2, 0, a[1])
    return (5, 0, show_atom(a))


def show_atom(a):
    if a[0] == 's':
        return a[1]
    if a[0] == 'ext':
        return 'ext(%s)' % a[1]
    if a[0] in ('div', 'mod'):
        return '%s(%s, %d)' % (a[0], show(unfreeze(a[1])), a[2])
    if a[0] == 'wrap':
        return 'wrap%d(%s)' % (a[1], show(unfreeze(a[2])))
    if a[0] == 'mul':
        return 'mul(%s, %s)' % (show(unfreeze(a[1])), show(unfreeze(a[2])))
    raise ValueError(a)


def show(l):
    parts = []
    for a, c in sorted(l.t.items(), key=lambda x: atom_order(x[0])):
        s = show_atom(a)
        if c == 1:
            term = s
        elif c == -1:
            term = '-' + s
        else:
            term = '%s*%s' % (c, s)
        parts.append(term)
    if l.k or not parts:
        parts.append(str(l.k))
    out = parts[0]
    for p in parts[1:]:
        out += ' - ' + p[1:] if p.startswith('-') else ' + ' + p
    return out


TOK = re.compile(r'\s*(0x[0-9a-fA-F]+|\d+|->|<=|>=|==|!=|[A-Za-z_][A-Za-z0-9_]*(?::[A-Za-z_][A-Za-z0-9_]*)?|[-+*(),<>])')


def tokenize(s):
    pos, out = 0, []
    s = s.strip()
    while pos < len(s):
        m = TOK.match(s, pos)
        if not m:
            raise ValueError('cannot parse %r at %d' % (s, pos))
        out.append(m.group(1))
        pos = m.end()
    return out


class Parser(object):
    def __init__(self, s):
        self.toks = tokenize(s)
        self.i = 0
        self.src = s

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self, want=None):
        t = self.peek()
        if want is not None and t != want:
            raise ValueError('expect %r got %r in %r' % (want, t, self.src))
        self.i += 1
        return t

    def expr(self):
        neg = False
        if self.peek() == '-':
            self.take()
            neg = True
        l = self.term()
        if neg:
            l = -l
        while self.peek() in ('+', '-'):
            op = self.take()
            r = self.term()
            l = l + r if op == '+' else l - r
        return l

    def term(self):
        l = self.factor()
        while self.peek() == '*':
            self.take()
            r = self.factor()
            if l.const() is not None:
                l = r.scale(l.const())
            elif r.const() is not None:
                l = l.scale(r.const())
            else:
                l = mk_opaque(('mul', l.key(), r.key()))
        return l

    def name(self):
        n = self.take()
        while self.peek() == '->':
            self.take()
            n += '->' + self.take()
        return n

    def factor(self):
        t = self.peek()
        if t == '(':
            self.take()
            e = self.expr()
            self.take(')')
            return e
        if t is not None and re.match(r'0x|\d', t):
            self.take()
            return Lin(int(t, 0))
        if t in ('ext', 'div', 'mod', 'wrap32', 'wrap16', 'mul') and self.toks[self.i + 1:self.i + 2] == ['(']:
            self.take()
            self.take('(')
            if t == 'ext':
                n = self.name()
                self.take(')')
                return Lin(0, {('ext', n): 1})
            a = self.expr()
            self.take(',') if t in ('div', 'mod', 'mul') else None
            if t in ('div', 'mod'):
                n = self.expr().const()
                self.take(')')
                return mk_opaque((t, a.key(), n))
            if t == 'mul':
                b = self.expr()
                self.take(')')
                return mk_opaque(('mul', a.key(), b.key()))
            self.take(')')
            return mk_opaque(('wrap', int(t[4:]), a.key()))
        return Lin(0, {('s', self.name()): 1})


def mk_opaque(a):
    """不透明项：内层是常数就当场算出来。"""
    if a[0] in ('div', 'mod'):
        c = unfreeze(a[1]).const()
        if c is not None:
            return Lin(c // a[2] if a[0] == 'div' else c % a[2])
    elif a[0] == 'wrap':
        c = unfreeze(a[2]).const()
        if c is not None:
            return Lin(c % (1 << a[1]))
    elif a[0] == 'mul':
        x, y = unfreeze(a[1]).const(), unfreeze(a[2]).const()
        if x is not None and y is not None:
            return Lin(x * y)
    return Lin(0, {a: 1})


def P(s):
    p = Parser(str(s))
    e = p.expr()
    if p.peek() is not None:
        raise ValueError('trailing %r in %r' % (p.peek(), s))
    return e


CMP = re.compile(r'<=|>=|==|<|(?<!-)>')


def split_cmp(s):
    m = CMP.search(s)
    if not m:
        raise ValueError('not a condition: %r' % s)
    return s[:m.start()], m.group(0), s[m.end():]


def C(s):
    """条件 → 若干个 'L ≤ 0' 形式的原子。"""
    if True:
        a, op, b = split_cmp(s)
        if True:
            l, r = P(a), P(b)
            if op == '<=':
                return [l - r]
            if op == '<':
                return [l - r + Lin(1)]
            if op == '>=':
                return [r - l]
            if op == '>':
                return [r - l + Lin(1)]
            return [l - r, r - l]


def show_atom_le0(g):
    """把 'L ≤ 0' 打印成可读的不等式。"""
    pos = Lin(0, {a: c for a, c in g.t.items() if c > 0})
    neg = Lin(0, {a: -c for a, c in g.t.items() if c < 0})
    k = g.k
    if not pos.t:
        return '%s >= %d' % (show(neg), k) if k else '%s >= 0' % show(neg)
    return '%s <= %s' % (show(pos), show(neg + Lin(-k)))


def subst(l, m):
    """m: 符号名 / 'ext(名)' → Lin。"""
    out = Lin(l.k)
    for a, c in l.t.items():
        if a[0] == 's' and a[1] in m:
            out = out + m[a[1]].scale(c)
        elif a[0] == 's' and '->' in a[1] and a[1].split('->', 1)[0] in m.get('__alias__', {}):
            base, rest = a[1].split('->', 1)
            out = out + Lin(0, {('s', m['__alias__'][base] + '->' + rest): c})
        elif a[0] == 'ext' and ('ext(%s)' % a[1]) in m:
            out = out + m['ext(%s)' % a[1]].scale(c)
        elif a[0] == 'ext' and '->' in a[1] and a[1].split('->', 1)[0] in m.get('__alias__', {}):
            base, rest = a[1].split('->', 1)
            out = out + Lin(0, {('ext', m['__alias__'][base] + '->' + rest): c})
        elif a[0] in ('div', 'mod'):
            out = out + mk_opaque((a[0], subst(unfreeze(a[1]), m).key(), a[2])).scale(c)
        elif a[0] == 'wrap':
            out = out + mk_opaque(('wrap', a[1], subst(unfreeze(a[2]), m).key())).scale(c)
        elif a[0] == 'mul':
            out = out + mk_opaque(('mul', subst(unfreeze(a[1]), m).key(), subst(unfreeze(a[2]), m).key())).scale(c)
        else:
            out = out + Lin(0, {a: c})
    return out


def is_iface(a):
    """调用方的接口符号（形参、字段、ext、全局）。"""
    if a[0] == 's':
        return bool(re.match(r'p\d+($|->)', a[1])) or a[1].startswith('g:')
    if a[0] == 'ext':
        return True
    return all(is_iface(x) for x in opaque_atoms(a))


def opaque_atoms(a):
    keys = [a[1]] if a[0] in ('div', 'mod') else [a[2]] if a[0] == 'wrap' else [a[1], a[2]]
    out = []
    for k in keys:
        out.extend(unfreeze(k).t.keys())
    return out


# ---------------------------------------------------------------- 取值范围
class Env(object):
    """调用点处的环境：符号的上下界（带来源标签）。"""

    def __init__(self, params, fields):
        self.lo, self.hi = {}, {}
        for i, p in enumerate(params):
            ty = p.split(':', 1)[1].strip() if ':' in p else ''
            self._type('p%d' % i, ty)
        for f, d in fields.items():
            self._type(f, d.get('type', ''))

    def _type(self, n, ty):
        if ty.endswith('*'):
            return
        if ty in TYPE_MAX:
            self.hi[('s', n)] = (TYPE_MAX[ty], 'type')
            self.lo[('s', n)] = (TYPE_MIN.get(ty, 0), 'type')

    def add_guard(self, g, tag):
        """单符号守卫 c·s + k ≤ 0 → 收窄 s 的界。"""
        if len(g.t) != 1:
            return
        (a, c), = g.t.items()
        if c > 0:
            v = Fraction(-g.k, c)
            v = v.numerator // v.denominator
            if a not in self.hi or v < self.hi[a][0] or self.hi[a][1] == 'type':
                self.hi[a] = (v, tag)
        else:
            v = Fraction(g.k, -c)
            v = -((-v.numerator) // v.denominator)
            if a not in self.lo or v > self.lo[a][0] or self.lo[a][1] == 'type':
                self.lo[a] = (v, tag)

    def bound(self, a, upper):
        if a[0] == 'ext':
            return (None, None) if upper else (0, 'type')
        if a[0] == 's':
            return (self.hi if upper else self.lo).get(a, (None, None))
        if a[0] == 'div':
            v, t = self.range_of(unfreeze(a[1]), upper)
            return (v // a[2], t) if v is not None else (None, None)
        if a[0] == 'mod':
            return (a[2] - 1, 'mask') if upper else (0, 'mask')
        if a[0] == 'wrap':
            lo, _ = self.range_of(unfreeze(a[2]), False)
            hi, t = self.range_of(unfreeze(a[2]), True)
            if lo is not None and lo >= 0 and hi is not None and hi < (1 << a[1]):
                return (hi, t) if upper else (lo, t)
            return ((1 << a[1]) - 1, 'type') if upper else (0, 'type')
        if a[0] == 'mul':
            x = self.range_of(unfreeze(a[1]), upper)
            y = self.range_of(unfreeze(a[2]), upper)
            if x[0] is not None and y[0] is not None:
                return (x[0] * y[0], x[1] or y[1])
        return (None, None)

    def range_of(self, l, upper):
        v, tags = Fraction(l.k), set()
        for a, c in l.t.items():
            b, t = self.bound(a, upper if c > 0 else not upper)
            if b is None:
                return (None, None)
            v += c * b
            if t:
                tags.add(t)
        tag = None
        for t in ('type', 'widen', 'data', 'check', 'guard', 'mask', 'loop', 'branch', 'ret', 'const', 'exact'):
            if t in tags:
                tag = t
                break
        return (v, tag)


def entails_le0(d, guards, env):
    """守卫 ⇒ d ≤ 0？（06- A-50：区间 + 单原子线性组合）"""
    v, _ = env.range_of(d, True)
    if v is not None and v <= 0:
        return True
    for g in guards:
        for a, c in d.t.items():
            b = g.t.get(a, 0)
            if c > 0 and b > 0:
                lam = Fraction(c, b)
                rest = Lin(0)
                rest.k = d.k - lam * g.k
                rest.t = {x: d.t.get(x, 0) - lam * g.t.get(x, 0) for x in set(d.t) | set(g.t)}
                rest.t = {x: y for x, y in rest.t.items() if y != 0}
                v, _ = env.range_of(rest, True)
                if v is not None and v <= 0:
                    return True
    return False


# ---------------------------------------------------------------- 载入
REQUIRED = {
    'fn': ['schema', 'fn', 'key', 'loc', 'params', 'status', 'reqs', 'effs', 'calls', 'reps', 'opens'],
    'req': ['id', 'kind', 'bytes', 'obj', 'have', 'end', 'bound', 'guard', 'access', 'path', 'depth', 'eff_depth', 'blind', 'text'],
    'call': ['site', 'callee', 'args', 'results'],
    'rep': ['id', 'level', 'reported', 'site', 'access', 'object', 'have', 'end', 'bound', 'depth', 'eff_depth', 'chain', 'msg', 'fp'],
    'open': ['reason', 'at', 'detail'],
}
REASONS = {'fptr', 'unknown_callee', 'recursion', 'msg_edge', 'global_value', 'global_ptr_target', 'field_written_by_callee',
           'alloc_size_unknown', 'flexible_array', 'struct_overlay', 'pointer_stepping', 'string_length', 'nonlinear',
           'type_range_only', 'entry_unproven', 'cond_truncated', 'ambiguous_callee', 'not_exported', 'not_in_tu',
           'cfg_too_large', 'timeout', 'iter_limit', 'unsupported_construct', 'struct_by_value', 'inline_asm',
           'imprecise_join', 'untracked_pointer'}


def check_shape(s):
    """落盘格式的结构检查（字段齐全、表达式都能解析、账本原因在枚举里）。"""
    probs = [k for k in REQUIRED['fn'] if k not in s]
    for r in s.get('reqs', []):
        probs += ['req %s 缺 %s' % (r.get('id'), k) for k in REQUIRED['req'] if k not in r]
        for x in [r['have'], r['end'][0], r['end'][1], r.get('start', '0')]:
            P(x)
        for g in r.get('guard', []):
            C(g)
    for c in s.get('calls', []):
        probs += ['call %s 缺 %s' % (c.get('site'), k) for k in REQUIRED['call'] if k not in c]
    for r in s.get('reps', []):
        probs += ['rep %s 缺 %s' % (r.get('id'), k) for k in REQUIRED['rep'] if k not in r]
    for o in s.get('opens', []):
        probs += ['open 缺 %s' % k for k in REQUIRED['open'] if k not in o]
        if o.get('reason') not in REASONS:
            probs.append('账本原因 %s 不在枚举里' % o.get('reason'))
    return probs


def load_case(cid):
    d = os.path.join(HERE, cid)
    meta = json.load(open(os.path.join(d, 'case.json'), encoding='utf-8'))
    fns = {}
    for f in sorted(os.listdir(d)):
        if f.endswith('.json') and f != 'case.json':
            s = json.load(open(os.path.join(d, f), encoding='utf-8'))
            bad = check_shape(s)
            if bad:
                raise SystemExit('%s/%s 格式不对：%s' % (cid, f, '；'.join(bad)))
            fns[s['fn']] = s
    meta['expect'] = read_expect(meta['files'])
    return meta, fns


def read_expect(files):
    out = []
    for f in files:
        head = open(os.path.join(CASES, f), encoding='utf-8').read(4000)
        for m in re.finditer(r'@expect\s+(TP|TN)\s+(\S+)\s+pa=(\S+)', head):
            out.append({'kind': m.group(1), 'entry': m.group(2), 'pa': m.group(3)})
    return out


def topo(fns):
    """自底向上：按"离叶子的层数"排，同层按名字。"""
    level = {}

    def lv(n, stack=()):
        if n in level:
            return level[n]
        if n not in fns or n in stack:
            return -1
        level[n] = 1 + max([lv(c['callee'], stack + (n,)) for c in fns[n].get('calls', [])] + [-1])
        return level[n]
    for n in fns:
        lv(n)
    return sorted(fns, key=lambda n: (level[n], n))


def callers_of(fns):
    out = {}
    for n, s in fns.items():
        for c in s.get('calls', []):
            out.setdefault(c['callee'], set()).add(n)
    return out


# ---------------------------------------------------------------- 回放一个调用点
def build_map(call, callee):
    m, alias, effd = {}, {}, 0
    for i, arg in enumerate(call['args']):
        p = 'p%d' % i
        if isinstance(arg, dict):
            m['ext(%s)' % p] = P(arg['have'])
            effd = max(effd, arg.get('eff_depth', 0))
            if 'alias' in arg:
                alias[p] = arg['alias']
            for f, v in arg.get('fields', {}).items():
                if isinstance(v, dict):
                    m['ext(%s->%s)' % (p, f)] = P(v['have'])
                    effd = max(effd, v.get('eff_depth', 0))
                else:
                    m['%s->%s' % (p, f)] = P(v)
        elif arg is not None:
            m[p] = P(arg)
    m['__alias__'] = alias
    return m, effd


def eliminate_locals(l, locs, upper):
    """把调用方局部换成它在调用点的界（符号界也可以）。返回 (新式, 用到的标签, 非空约束)。"""
    out, tags, ne = Lin(l.k), set(), []
    for a, c in l.t.items():
        if a[0] == 's' and a[1] in locs:
            lo, hi, tag = locs[a[1]]
            out = out + (hi if (c > 0) == upper else lo).scale(c)
            tags.add(tag)
            g = lo - hi
            if g.const() is None:
                ne.append(g)
        else:
            out = out + Lin(0, {a: c})
    return out, tags, ne


def classify(req, call, caller, callee, env_guards):
    m, effd = build_map(call, callee)
    have = subst(P(req['have']), m)
    elo, ehi = [subst(P(x), m) for x in req['end']]
    guards = [g for s in req.get('guard', []) for g in C(subst_cond(s, m))]
    locs = {n: (P(v['range'][0]), P(v['range'][1]), v.get('bound', 'loop')) for n, v in call.get('locals', {}).items()}
    env = Env(caller.get('params', []), caller.get('fields', {}))
    for g, tag in env_guards:
        env.add_guard(g, tag)
    trace = {'have': have, 'end': (elo, ehi), 'guards': guards}
    # ① 守卫：常量假 → 这次调用走不到访问
    live = []
    for g in guards:
        gmax, _, _ = eliminate_locals(g, locs, True)
        gmin, _, _ = eliminate_locals(g, locs, False)
        if gmin.const() is not None and gmin.const() > 0:
            return dict(trace, outcome='safe', why='守卫 %s 在此为假，访问走不到' % show_atom_le0(g))
        if gmax.const() is not None and gmax.const() <= 0:
            continue
        live.append(gmax)
    # ② 局部换成区间
    ehi2, tags_hi, ne = eliminate_locals(ehi, locs, True)
    elo2, _, _ = eliminate_locals(elo, locs, False)
    bound = (tags_hi.pop() if tags_hi else req.get('bound', 'exact'))
    for g in live + ne:
        env.add_guard(g, 'guard')
    dhi, dlo = ehi2 - have, elo2 - have
    depth = req.get('depth', 0) + 1
    eff_depth = max(req.get('eff_depth', 0), effd)
    base = dict(trace, end2=(elo2, ehi2), depth=depth, eff_depth=eff_depth, bound=bound)
    # ③ 判定
    if dhi.const() is not None:
        if dhi.const() <= 0:
            return dict(base, outcome='safe', why='最大末端 %s ≤ %s' % (show(ehi2), show(have)))
        if dlo.const() is not None and dlo.const() > 0:
            return dict(base, outcome='L1', why='最小末端 %s > %s，对所有取值都越界' % (show(elo2), show(have)))
        if bound in CODE_TAGS:
            return dict(base, outcome='L2', why='最大末端 %s > %s（上界来自 %s），最小末端 %s 不越界' % (show(ehi2), show(have), bound, show(elo2)))
        return dict(base, outcome='open', reason='type_range_only' if bound == 'type' else 'pointer_stepping', why='上界只来自 %s，不算代码给出的界' % bound)
    if entails_le0(dhi, live + ne + [g for g, _ in env_guards], env):
        return dict(base, outcome='safe', why='守卫蕴含 %s ≤ %s' % (show(ehi2), show(have)))
    # 守卫界判定：对象已知（have 是常数），剩下的符号都被代码给出的界框住
    if have.const() is not None:
        vmax, tmax = env.range_of(dhi, True)
        if vmax is not None and tmax in CODE_TAGS:
            if vmax <= 0:
                return dict(base, outcome='safe', why='在守卫给出的范围内最大末端也不越界')
            vmin, _ = env.range_of(dlo, False)
            hi_end = vmax + have.const()
            lo_end = (vmin + have.const()) if vmin is not None else None
            if vmin is not None and vmin > 0:
                return dict(base, outcome='L1', bound=tmax, why='在 %s 给出的范围内最小末端 %s 也越界' % (tmax, lo_end))
            return dict(base, outcome='L2', bound=tmax, hi_end=hi_end, lo_end=lo_end,
                        why='%s 给出的范围内最大末端 %s > %s，最小末端 %s 不越界' % (tmax, hi_end, show(have), lo_end))
    if all(is_iface(a) for a in list(dhi.t) + list(dlo.t)):
        new_guards = [g for g in live + ne if all(is_iface(a) for a in g.t)]
        return dict(base, outcome='up', guards2=new_guards)
    return dict(base, outcome='open', reason='entry_unproven', why='含调用方局部且没有界')


def subst_cond(s, m):
    a, op, b = split_cmp(s)
    return '%s %s %s' % (show(subst(P(a), m)), op, show(subst(P(b), m)))


def same_req(r, mine):
    """回放算出的上传条件与 JSON 里调用方那条 Req 是否一致。"""
    probs = []
    if P(r['have']) != mine['have']:
        probs.append('have: JSON %s / 回放 %s' % (r['have'], show(mine['have'])))
    lo, hi = [P(x) for x in r['end']]
    if lo != mine['end2'][0] or hi != mine['end2'][1]:
        probs.append('end: JSON [%s, %s] / 回放 [%s, %s]' % (r['end'][0], r['end'][1], show(mine['end2'][0]), show(mine['end2'][1])))
    jg = set(g for s in r.get('guard', []) for g in C(s))
    mg = set(mine.get('guards2', []))
    if jg != mg:
        probs.append('guard: JSON %s / 回放 %s' % (sorted(show_atom_le0(g) for g in jg), sorted(show_atom_le0(g) for g in mg)))
    if r.get('depth') != mine['depth']:
        probs.append('depth: JSON %s / 回放 %s' % (r.get('depth'), mine['depth']))
    return probs


def check_eff(eff, call):
    m, effd = build_map(call, None)
    if eff['kind'] == 'ret_new':
        return 'new(%s)%s' % (show(subst(P(eff['size']), m)), ' | NULL' if eff.get('maybe_null') else '')
    if eff['kind'] == 'ret_int':
        return '[%s, %s]' % tuple(show(subst(P(x), m)) for x in eff['range'])
    if eff['kind'] == 'ret_cond':
        return '%s ⇒ %s' % (eff['when'], ', '.join(subst_cond(s, m) for s in eff['then']))
    if eff['kind'] == 'field':
        return '%s := %s' % (eff['target'], show(subst(P(eff['value']), m)))
    return None


# ---------------------------------------------------------------- 回放一个用例
def replay(cid, verbose=False, out=None):
    meta, fns = load_case(cid)
    order = topo(fns)
    callers = callers_of(fns)
    errors, lines, decided = [], [], {}

    def say(s):
        lines.append(s)
        if verbose:
            print(s)

    say('## %s %s' % (cid, meta['title']))
    for fn in order:
        s = fns[fn]
        reqs = {r['id']: r for r in s.get('reqs', [])}
        reps = {r['id']: r for r in s.get('reps', [])}
        for call in s.get('calls', []):
            callee = fns.get(call['callee'])
            say('- %s @ %s  调用 %s(%s)' % (fn, call['site'], call['callee'], ', '.join(
                (a['ptr'] if isinstance(a, dict) else '…' if a is None else str(a)) for a in call['args'])))
            if callee is None:
                for res in call.get('results', []):
                    say('    · %s' % res.get('note', res.get('outcome')))
                continue
            env_guards = [(g, x.get('bound', 'guard')) for x in call.get('guards', []) for g in C(x['cond'])]
            for res in call.get('results', []):
                if 'note' in res:
                    say('    · %s' % res['note'])
                    continue
                if 'eff' in res:
                    eff = callee['effs'][res['eff']]
                    got = check_eff(eff, call)
                    ok = got is None or norm_text(got) == norm_text(res['value'])
                    say('    · 效果 %s → %s %s' % (eff['kind'], got, '' if ok else '✗ JSON 记的是 %s' % res['value']))
                    if not ok:
                        errors.append('%s %s 效果不一致' % (cid, fn))
                    continue
                r = next((x for x in callee['reqs'] if x['id'] == res['req']), None)
                if r is None:
                    errors.append('%s %s: 被调者没有 %s' % (cid, call['callee'], res['req']))
                    continue
                mine = classify(r, call, s, callee, env_guards)
                show_end = fmt_range(mine['end'])
                if 'end2' in mine and (mine['end2'][0] != mine['end'][0] or mine['end2'][1] != mine['end'][1]):
                    show_end += ' → 局部换成区间 %s' % fmt_range(mine['end2'])
                gs = [g for g in mine.get('guards', []) if g.const() is None]
                if mine['outcome'] == 'up':
                    gs = mine.get('guards2', [])
                if gs:
                    show_end += '，守卫 %s' % ' 且 '.join(show_atom_le0(g) for g in gs)
                if any(g.const() is not None for g in mine.get('guards', [])):
                    show_end += '，守卫在此%s' % ('不成立' if any(g.const() > 0 for g in mine['guards'] if g.const() is not None) else '成立')
                msg = '    · %s.%s：末端 %s，对象 %s → %s' % (call['callee'], r['id'], show_end, show(mine['have']), mine['outcome'])
                if mine.get('why'):
                    msg += '（%s）' % mine['why']
                ok = mine['outcome'] == res['outcome']
                if ok and mine['outcome'] == 'up':
                    probs = same_req(reqs[res['as']], mine)
                    msg += ' → 本函数 %s' % res['as']
                    if probs:
                        ok = False
                        msg += ' ✗ ' + '；'.join(probs)
                if ok and mine['outcome'] in ('L1', 'L2'):
                    rep = reps.get(res['report'])
                    reported = max(mine['depth'], mine['eff_depth']) > INLINE_DEPTH
                    if rep is None or rep['level'] != mine['outcome'] or rep['depth'] != mine['depth'] or rep['reported'] != reported:
                        ok = False
                        msg += ' ✗ 报告记录不一致'
                    else:
                        msg += ' → %s（depth %d, eff_depth %d，%s）' % (res['report'], mine['depth'], mine['eff_depth'], '报' if reported else '≤ %d 层，交现有规则' % INLINE_DEPTH)
                        decided[(fn, rep['id'])] = rep
                if ok and mine['outcome'] == 'open' and mine.get('reason') != res.get('reason'):
                    ok = False
                    msg += ' ✗ 账本原因 JSON %s / 回放 %s' % (res.get('reason'), mine.get('reason'))
                if not ok and mine['outcome'] != res['outcome']:
                    msg += ' ✗ JSON 记的是 %s' % res['outcome']
                if not ok:
                    errors.append('%s %s @ %s %s' % (cid, fn, call['site'], r['id']))
                say(msg)
        if fn not in callers and s.get('reqs'):
            for r in s['reqs']:
                op = next((o for o in s.get('opens', []) if o.get('req') == r['id']), None)
                say('- %s 是入口（没有调用方）：%s 到此为止 → 记账 %s' % (fn, r['id'], op['reason'] if op else '✗ 缺账本记录'))
                if op is None:
                    errors.append('%s %s 入口 Req 未记账' % (cid, fn))
    say('')
    verdicts = []
    for e in meta['expect']:
        reps = [r for (f, _), r in decided.items() if f == e['entry'] and r['reported']]
        got = reps[0]['level'] if reps else '-'
        want = e['pa'] if e['pa'] in ('L1', 'L2') else '-'
        ok = got == want and len(reps) <= 1
        verdicts.append((e, got, ok))
        say('- 期望 %s %s pa=%s → 回放 %s %s' % (e['kind'], e['entry'], e['pa'], got, '✓' if ok else '✗'))
        if not ok:
            errors.append('%s %s 期望 %s 回放 %s' % (cid, e['entry'], e['pa'], got))
    return lines, errors, verdicts, meta, fns, order


def fmt_range(r):
    return show(r[0]) if r[0] == r[1] else '[%s, %s]' % (show(r[0]), show(r[1]))


def norm_text(s):
    return re.sub(r'\s+', '', s)


def all_cases():
    ids = [d for d in os.listdir(HERE) if os.path.isfile(os.path.join(HERE, d, 'case.json'))]
    return sorted(ids, key=lambda x: [int(n) for n in re.findall(r'\d+', x)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cases', nargs='*')
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--md')
    a = ap.parse_args()
    ids = a.cases or all_cases()
    total_err, rows = [], []
    try:
        import jsonschema
        schema = json.load(open(os.path.join(HERE, 'summary.schema.json'), encoding='utf-8'))
        n = 0
        for cid in ids:
            for f in sorted(os.listdir(os.path.join(HERE, cid))):
                if f.endswith('.json') and f != 'case.json':
                    jsonschema.validate(json.load(open(os.path.join(HERE, cid, f), encoding='utf-8')), schema)
                    n += 1
        print('结构检查：%d 份摘要按 summary.schema.json 校验通过' % n)
    except ImportError:
        print('结构检查：没装 jsonschema，只做内置检查（字段齐全、表达式可解析、账本原因在枚举里）')
    for cid in ids:
        lines, errors, verdicts, meta, fns, order = replay(cid, a.verbose)
        total_err.extend(errors)
        rows.append((cid, meta, verdicts, len(fns), lines, fns, order))
        if not a.verbose:
            print('%-7s %-3s 个函数  %s' % (cid, len(fns), '  '.join('%s %s→%s%s' % (e['kind'], e['pa'], g, '' if ok else '✗') for e, g, ok in verdicts)))
    if a.md:
        write_md(a.md, rows)
    print('\n回放 %d 个用例，%d 处不一致' % (len(ids), len(total_err)))
    for e in total_err:
        print('  ✗', e)
    sys.exit(1 if total_err else 0)


def write_md(path, rows):
    out = []
    for cid, meta, verdicts, n, lines, fns, order in rows:
        out.append('### %s %s\n' % (cid, meta['title']))
        out.append(meta.get('point', '') + '\n')
        out.append('**代码**（`%s`；只保留函数体，头注释略）\n\n```c\n%s\n```\n' % ('`、`'.join(meta['files']), meta['code'].rstrip()))
        out.append('**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/%s/<函数名>.json`）\n' % cid)
        out.append('| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |\n|---|---|---|---|')
        for fn in order:
            s = fns[fn]
            req = '<br>'.join('**%s** %s' % (r['id'], cell(r['text'])) + ('（depth %d）' % r['depth'] if r['depth'] else '') for r in s['reqs']) or '—'
            eff = '<br>'.join(cell(e['text'] or e['kind']) for e in s['effs']) or '—'
            dec = ['**%s %s**（depth %d%s）%s' % (r['id'], r['level'], r['depth'], '' if not r['eff_depth'] else ', eff_depth %d' % r['eff_depth'],
                                                 '' if r['reported'] else ' 不报') for r in s['reps']]
            dec += ['账本 `%s`%s' % (o['reason'], '（%s）' % o['req'] if o.get('req') else '') for o in s['opens']]
            out.append('| `%s` | %s | %s | %s |' % (fn, req, eff, '<br>'.join(dec) or '—'))
        out.append('')
        for fn in order:
            if fn in meta.get('show_open', []):
                out.append('<details open><summary><code>%s.json</code></summary>\n\n```json\n%s```\n</details>\n' % (
                    fn, open(os.path.join(HERE, cid, fn + '.json'), encoding='utf-8').read()))
        out.append('**回放**（`python3 replay.py %s -v`）\n\n```text\n%s\n```\n' % (cid, '\n'.join(l for l in lines[1:] if l)))
    body = '\n'.join(out)
    mark = '<!-- WALKTHROUGH -->'
    if os.path.exists(path) and mark in open(path, encoding='utf-8').read():
        head = open(path, encoding='utf-8').read().split(mark)[0]
        body = head + mark + '\n\n' + body
    open(path, 'w', encoding='utf-8').write(body)


def cell(s):
    return s.replace('|', '\\|')


def one_line(s):
    bits = []
    if s.get('reqs'):
        bits.append('%d 条 Req' % len(s['reqs']))
    if s.get('effs'):
        bits.append('%d 条 Eff' % len(s['effs']))
    if s.get('reps'):
        bits.append('%d 条判定' % len(s['reps']))
    if s.get('opens'):
        bits.append('%d 条账本' % len(s['opens']))
    return '、'.join(bits) or '空摘要（不影响任何访问）'


if __name__ == '__main__':
    main()
