#!/usr/bin/env python3
"""摘要实样回放器：读每个函数的摘要 JSON，在每个调用点做"代换 → 判定"，核对 JSON 里记录的结论，再对照用例头的 @expect。

**它只读机器字段**：计算前先把所有键名为 doc 的内容删掉（自然语言只给人看，引擎不读）。

两件事：
  1. 逐个调用点核对：被调者的每条 Req / Eff 代进来，结论是否与调用方 JSON 里记的一致（check）；
  2. 从局部事实推到底（derive）：只拿每个函数"自己算出来的"东西——本函数访问点生成的 Req（depth 0，含被覆盖的）、
     效果、调用点处的实参 / 局部区间 / 守卫——自底向上把所有上传的 Req、判定、入口账本**重新推一遍**，
     与 JSON 比对，最后得到告警清单，与用例头的 @expect 比对。

它是 06-spec.md §6.6（调用点五步的第 ②③④ 步）与 F-10…F-22（判定与报告）的**参考实现**：
C++ 引擎在这些用例上产出的摘要，必须与这里的 JSON 一致（06- T-06 快照测试的金标准）。
它不生成摘要（那是引擎从 FIR 算出来的），只验证"摘要这种形式足够把多层越界串起来判出来"，
并把每一步代换打印出来，方便看、方便调试。

用法：
    python3 replay.py                     # 回放全部用例，逐条核对
    python3 replay.py LV4-01 -v           # 只回放一个用例，打印每一步
    python3 replay.py --md out.md         # 生成逐用例的走查文档（摘要 JSON + 回放过程）
    python3 replay.py --strip DIR         # 把 104 份摘要去掉 doc 后写到 DIR（引擎实际存的"机器版"）
    python3 replay.py --dir DIR           # 从 DIR 读摘要（比如上面去掉 doc 的那份）：结果应与读原文件完全相同
    python3 replay.py --alerts FILE       # 把"从局部事实推到底"得到的最终告警写成 JSON（位置、消息、调用链、指纹）；
                                          # 本目录的 alerts.json 就是这样生成的，全量回放时自动与它比对
只依赖 Python 3 标准库。
"""
import argparse
import json
import os
import re
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
SUMS = HERE                       # 摘要从哪里读（--dir 可换成去掉 doc 的那份）
CASES = os.path.dirname(HERE)
CODE_TAGS = {'exact', 'const', 'check', 'mask', 'branch', 'loop', 'ret', 'guard', 'model'}  # model：SPEC-0034
INLINE_DEPTH = 2
TYPE_MAX = {'UINT8': 0xFF, 'UINT16': 0xFFFF, 'UINT32': 0xFFFFFFFF, 'size_t': 0xFFFFFFFF, 'CHAR': 0x7F, 'INT32': 0x7FFFFFFF}
TYPE_MIN = {'INT32': -0x80000000, 'CHAR': -0x80}

# ---------------------------------------------------------------- 表达式
# 线性式：常数 k + Σ c·atom。atom 是可哈希的元组：
#   ('s', name)                 符号：p0、p1->ulLen、g:g_x、调用方局部 i
#   ('ext', name)               ext(p0)、ext(p0->pucBuf)
#   ('bext', name)              bext(p0)：指针前面还有多少字节（负偏移，SPEC-0039）
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
    if kind == 'bext':
        return (2, 1, a[1])
    return (5, 0, show_atom(a))


def show_atom(a):
    if a[0] == 's':
        return a[1]
    if a[0] in ('ext', 'bext'):
        return '%s(%s)' % (a[0], a[1])
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
        if t in ('ext', 'bext', 'div', 'mod', 'wrap32', 'wrap16', 'mul') and self.toks[self.i + 1:self.i + 2] == ['(']:
            self.take()
            self.take('(')
            if t in ('ext', 'bext'):
                n = self.name()
                self.take(')')
                return Lin(0, {(t, n): 1})
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
        elif a[0] in ('ext', 'bext') and ('%s(%s)' % a) in m:
            out = out + m['%s(%s)' % a].scale(c)
        elif a[0] in ('ext', 'bext') and '->' in a[1] and a[1].split('->', 1)[0] in m.get('__alias__', {}):
            base, rest = a[1].split('->', 1)
            out = out + Lin(0, {(a[0], m['__alias__'][base] + '->' + rest): c})
        elif m.get('__poison__') and a[0] in ('s', 'ext', 'bext') and re.match(r'p\d+($|->)', a[1]):
            text = a[1] if a[0] == 's' else '%s(%s)' % a     # SPEC-0063：改名成非接口符号
            out = out + Lin(0, {('s', '_unmapped:' + text): c})
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
    if a[0] in ('ext', 'bext'):
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
            ty = p.get('type', '') if isinstance(p, dict) else (p.split(':', 1)[1].strip() if ':' in p else '')
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
        if a[0] in ('ext', 'bext'):
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
        for t in ('type', 'widen', 'data', 'model', 'check', 'guard', 'mask', 'loop', 'branch', 'ret', 'const', 'exact'):
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
    'req': ['id', 'kind', 'have', 'obj', 'end', 'bound', 'guard', 'access', 'path', 'depth', 'eff_depth', 'blind'],
    'call': ['site', 'callee', 'args', 'results'],
    'rep': ['id', 'level', 'reported', 'site', 'access', 'kind', 'obj', 'have', 'end', 'bound', 'depth', 'eff_depth', 'chain', 'fp'],
    'open': ['reason', 'at'],
    'access': ['fn', 'key', 'at', 'end'],
    'ptr': ['obj', 'have'],
}
# 对象（机器写法）：g:全局名 | local:局部名 | heap@分配调用点 | *pK（第 K 个形参所指对象）| *pK->f（形参所指结构体里指针字段 f 所指对象）
#                   | pK->f（形参所指结构体里的数组成员 f）
OBJ = re.compile(r'^(?:(?:g|local):\w+|heap@[\w./-]+:\d+|\*?p\d+(?:->\w+)?)$')
VIA = re.compile(r'^(?:g|local):\w+\.\w+$')
REASONS = {'fptr', 'unknown_callee', 'recursion', 'msg_edge', 'global_value', 'global_ptr_target', 'field_written_by_callee',
           'alloc_size_unknown', 'flexible_array', 'struct_overlay', 'pointer_stepping', 'string_length', 'nonlinear',
           'type_range_only', 'entry_unproven', 'cond_truncated', 'ambiguous_callee', 'not_exported', 'not_in_tu',
           'cfg_too_large', 'timeout', 'iter_limit', 'unsupported_construct', 'struct_by_value', 'inline_asm',
           'imprecise_join', 'untracked_pointer', 'typed_field'}


NL = re.compile(r'[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]|[A-Za-z]{2,}\s+[A-Za-z]{2,}\s+[A-Za-z]{2,}')


def strip_doc(o):
    """删掉所有键名为 doc 的内容：剩下的就是引擎读写的机器字段。"""
    if isinstance(o, dict):
        return {k: strip_doc(v) for k, v in o.items() if k != 'doc'}
    if isinstance(o, list):
        return [strip_doc(x) for x in o]
    return o


def nl_outside_doc(o, path='$'):
    """doc 之外不允许出现自然语言（中文、成句的英文）。"""
    out = []
    if isinstance(o, dict):
        for k, v in o.items():
            if k != 'doc':
                out += nl_outside_doc(v, path + '.' + k)
    elif isinstance(o, list):
        for i, x in enumerate(o):
            out += nl_outside_doc(x, '%s[%d]' % (path, i))
    elif isinstance(o, str) and NL.search(o):
        out.append('%s = %r' % (path, o))
    return out


def check_shape(s):
    """落盘格式的结构检查（字段齐全、表达式都能解析、账本原因在枚举里、doc 之外没有自然语言）。"""
    probs = [k for k in REQUIRED['fn'] if k not in s]
    probs += ['doc 之外有自然语言：%s' % x for x in nl_outside_doc(s)]
    for r in s.get('reqs', []):
        probs += ['req %s 缺 %s' % (r.get('id'), k) for k in REQUIRED['req'] if k not in r]
        probs += ['req %s.access 缺 %s' % (r.get('id'), k) for k in REQUIRED['access'] if k not in r.get('access', {})]
        if not OBJ.match(r.get('obj', '')):
            probs.append('req %s 的 obj 写法不对：%r' % (r.get('id'), r.get('obj')))
        for x in [r['have'], r['end'][0], r['end'][1], r.get('start', '0'), r['access'].get('end', '0')]:
            P(x)
        if r.get('depth') == 0 and (r['access'].get('fn') != s.get('fn') or P(r['access'].get('end', '0')) != P(r['end'][1])):
            probs.append('req %s 是本函数的访问（depth 0），access.fn / access.end 应与本函数、end 上界一致' % r.get('id'))
        for g in r.get('guard', []):
            C(g)
    for c in s.get('calls', []):
        probs += ['call %s 缺 %s' % (c.get('site'), k) for k in REQUIRED['call'] if k not in c]
        for a in c.get('args', []):
            for x in [a] + list((a.get('fields') or {}).values()) if isinstance(a, dict) else []:
                if isinstance(x, dict):
                    probs += ['call %s 的指针实参缺 %s' % (c.get('site'), k) for k in REQUIRED['ptr'] if k not in x]
                    if not OBJ.match(x.get('obj', '')):
                        probs.append('call %s 的指针实参 obj 写法不对：%r' % (c.get('site'), x.get('obj')))
    for r in s.get('reps', []):
        probs += ['rep %s 缺 %s' % (r.get('id'), k) for k in REQUIRED['rep'] if k not in r]
        probs += ['rep %s.access 缺 %s' % (r.get('id'), k) for k in REQUIRED['access'] if k not in r.get('access', {})]
        if not OBJ.match(r.get('obj', '')) or ('via' in r and not VIA.match(r['via'])):
            probs.append('rep %s 的 obj / via 写法不对' % r.get('id'))
    for o in s.get('opens', []):
        probs += ['open 缺 %s' % k for k in REQUIRED['open'] if k not in o]
        if o.get('reason') not in REASONS:
            probs.append('账本原因 %s 不在枚举里' % o.get('reason'))
    return probs


def load_case(cid):
    d = os.path.join(SUMS, cid)
    meta = json.load(open(os.path.join(HERE, cid, 'case.json'), encoding='utf-8'))   # case.json 是走查用的元数据（标题、代码），不是摘要
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


def pretty(o, ind=0, width=110):
    """摘要 JSON 的排版：放得下就一行，放不下再展开（与 cases/summaries/ 里的文件同一种排法）。"""
    one = json.dumps(o, ensure_ascii=False)
    if len(one) + ind <= width or not isinstance(o, (dict, list)) or not o:
        return one
    pad = ' ' * (ind + 2)
    if isinstance(o, list):
        return '[\n' + ',\n'.join(pad + pretty(x, ind + 2, width) for x in o) + '\n' + ' ' * ind + ']'
    items = [pad + json.dumps(k, ensure_ascii=False) + ': ' + pretty(v, ind + 2, width) for k, v in o.items()]
    return '{\n' + ',\n'.join(items) + '\n' + ' ' * ind + '}'


def machine(fns):
    return {k: strip_doc(v) for k, v in fns.items()}


# ---------------------------------------------------------------- 对象、告警文本、指纹（06- F-32 / F-34）
def subst_obj(obj, call):
    """被调者 Req 的对象代进调用点。*pK → 第 K 个指针实参的 obj；*pK->f → 它的 fields.f 的 obj（经由 via）。返回 (对象, via)。"""
    m = re.match(r'^(\*?)p(\d+)(?:->(\w+))?$', obj)
    if not m:
        return obj, None                        # g: / local: / heap@ 与调用点无关，原样上传
    star, k, f = m.group(1), int(m.group(2)), m.group(3)
    args = call.get('args', [])
    arg = args[k] if k < len(args) else None
    if not isinstance(arg, dict):
        return '?', None
    base = arg['obj']
    if f is None:
        return base, None
    bm = re.match(r'^\*(p\d+)$', base)
    if star:
        sub = (arg.get('fields') or {}).get(f)
        if isinstance(sub, dict):
            return sub['obj'], (base + '.' + f if base.startswith(('g:', 'local:')) else None)
        return ('*%s->%s' % (bm.group(1), f), None) if bm else ('?', None)
    return ('%s->%s' % (bm.group(1), f), None) if bm else ('?', None)


def obj_name(obj, params=()):
    """告警里对象的写法（F-32 的 {object}）。"""
    m = re.match(r'local:_unknown(\d+)$', obj)
    if m:                                              # 不跟踪的指针实参（SPEC-0062）
        return 'the object passed as argument %d' % (int(m.group(1)) + 1)
    if obj.startswith(('g:', 'local:')):
        return "'%s'" % obj.split(':', 1)[1]
    if obj.startswith('heap@'):
        return 'heap object allocated at %s' % obj[5:]
    names = [p['name'] if isinstance(p, dict) else p for p in params]
    return "'%s'" % re.sub(r'p(\d+)', lambda m: names[int(m.group(1))] if int(m.group(1)) < len(names) else m.group(0), obj)


BOUND_PHRASE = {'loop': 'a loop condition', 'check': 'a check', 'guard': 'a guard condition', 'mask': 'a bit mask',
                'branch': 'a branch constant', 'ret': 'a return value range', 'const': 'a constant',
                'model': 'a size argument of a library function',
                'exact': 'the call arguments'}  # exact 的 L2：函数指针的多个目标之一（SPEC-0058）


def alert_msg(rep, params=()):
    """F-32 消息模板：只用 rep 的机器字段。跨文件的链把整条链附在末尾（F-33 ③）。"""
    w = 'write' if rep['kind'] == 'W' else 'read'
    name = obj_name(rep['obj'], params)
    size = '%s bytes' % rep['have'] + ('; via %s' % rep['via'].split(':', 1)[1] if rep.get('via') else '')
    if rep.get('dir'):                                 # 负偏移：越到指针前面的字节数（SPEC-0039 §2.3）
        s = '%s out-of-bound %s through a %d-level call chain: up to %s bytes before %s (%s available before the pointer) %s' % (
            'Definite' if rep['level'] == 'L1' else 'Possible', w, rep['depth'], rep['end'][1], name, size,
            'are accessed' if rep['level'] == 'L1' else 'may be accessed')
        if rep['level'] != 'L1':
            s += '; the bound comes from %s' % BOUND_PHRASE.get(rep['bound'], rep['bound'])
    elif rep['level'] == 'L1':
        s = 'Definite out-of-bound %s through a %d-level call chain: up to byte %s of %s (%s) is accessed' % (
            w, rep['depth'], rep['end'][1], name, size)
    else:
        s = 'Possible out-of-bound %s through a %d-level call chain: up to byte %s of %s (%s) may be accessed; the bound comes from %s' % (
            w, rep['depth'], rep['end'][1], name, size, BOUND_PHRASE.get(rep['bound'], rep['bound']))
    hops = [x.split('@', 1) for x in rep['chain']]
    if len({site.rsplit(':', 1)[0] for _, site in hops}) > 1:
        s += ' [%s]' % ' -> '.join('%s (%s)' % (f, site) for f, site in hops)
    return s


def xxh64(data, seed=0):
    """XXH64，与 LLVM 14 的 llvm::xxHash64(StringRef) 逐位相同（实测）。"""
    M = (1 << 64) - 1
    P1, P2, P3, P4, P5 = 11400714785074694791, 14029467366897019727, 1609587929392839161, 9650029242287828579, 2870177450012600261
    rotl = lambda x, r: ((x << r) | (x >> (64 - r))) & M
    rnd = lambda acc, lane: (rotl((acc + lane * P2) & M, 31) * P1) & M
    n, i = len(data), 0
    if n >= 32:
        v = [(seed + P1 + P2) & M, (seed + P2) & M, seed & M, (seed - P1) & M]
        while i + 32 <= n:
            for j in range(4):
                v[j] = rnd(v[j], int.from_bytes(data[i + 8 * j:i + 8 * j + 8], 'little'))
            i += 32
        h = (rotl(v[0], 1) + rotl(v[1], 7) + rotl(v[2], 12) + rotl(v[3], 18)) & M
        for x in v:
            h = ((h ^ rnd(0, x)) * P1 + P4) & M
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while i + 8 <= n:
        h = (rotl(h ^ rnd(0, int.from_bytes(data[i:i + 8], 'little')), 27) * P1 + P4) & M
        i += 8
    if i + 4 <= n:
        h = (rotl(h ^ ((int.from_bytes(data[i:i + 4], 'little') * P1) & M), 23) * P2 + P3) & M
        i += 4
    while i < n:
        h = (rotl(h ^ ((data[i] * P5) & M), 11) * P1) & M
        i += 1
    h = ((h ^ (h >> 33)) * P2) & M
    h = ((h ^ (h >> 29)) * P3) & M
    return h ^ (h >> 32)


def fingerprint(level, entry_key, access, kind, obj):
    """F-34：不含行号。访问用"访问函数键 + 访问点自身的末端式"标识，对象用机器写法。"""
    s = 'OobSummary|%s|%s|%s|%s|%s|%s' % (level, entry_key, access['key'], kind, access['end'], obj)
    return '%016x' % xxh64(s.encode('utf-8'))


def make_alert(fn, s, rep):
    """最终告警（06- F-30…F-35）：全部由机器字段生成。note 只给汇合点所在文件里的那几跳（F-33 ②），别的文件只在总链里（③）。"""
    hops = [x.split('@', 1) for x in rep['chain']]
    here = rep['site'].rsplit(':', 1)[0]
    notes = ['Call chain: ' + ' -> '.join('%s (%s)' % (f, site) for f, site in hops)]
    for i, (f, site) in enumerate(hops):
        if site.rsplit(':', 1)[0] == here:
            notes.append('%s: %s' % (site, 'call to %s' % hops[i + 1][0] if i + 1 < len(hops)
                                     else 'the %s happens here' % ('write' if rep['kind'] == 'W' else 'read')))
    return {'checker': 'OobSummary',
            'bug_type': 'Out-of-bound access via call chain (%s)' % ('definite' if rep['level'] == 'L1' else 'possible'),
            'category': 'Memory error', 'level': rep['level'], 'fn': fn, 'loc': rep['site'],
            'msg': alert_msg(rep, s.get('params', [])), 'notes': notes, 'fp': rep['fp']}


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
            # 指针前面的字节数；没写 back → 不是接口符号的名字，调用点上只会走向记账（不当成 0，SPEC-0039 §2.2）
            m['bext(%s)' % p] = P(arg['back']) if 'back' in arg else Lin(0, {('s', '_noback%d' % i): 1})
            effd = max(effd, arg.get('eff_depth', 0))
            if 'alias' in arg:
                alias[p] = arg['alias']
            for f, v in arg.get('fields', {}).items():
                if isinstance(v, dict):
                    m['ext(%s->%s)' % (p, f)] = P(v['have'])
                    m['bext(%s->%s)' % (p, f)] = P(v['back']) if 'back' in v else Lin(0, {('s', '_noback%d_%s' % (i, f)): 1})
                    effd = max(effd, v.get('eff_depth', 0))
                else:
                    m['%s->%s' % (p, f)] = P(v)
        elif arg is not None:
            m[p] = P(arg)
    m['__alias__'] = alias
    m['__poison__'] = True  # 表里没有的被调者符号不能当成调用方的（SPEC-0063）
    return m, effd


def has_local(a, locs):
    """不透明项的内层（递归）是否含调用方局部。"""
    if a[0] == 's':
        return a[1] in locs
    if a[0] in ('ext', 'bext'):
        return False
    return any(has_local(x, locs) for x in opaque_atoms(a))


def eliminate_locals(l, locs, upper):
    """把调用方局部换成它在调用点的界（符号界也可以）。返回 (新式, 用到的标签, 非空约束)。
    不透明项里的局部也换（SPEC-0049）：div 单调代入；mod / wrap 在区间内时等同内层，否则取 [0, n − 1] / 类型范围。"""
    out, tags, ne = Lin(l.k), set(), []
    for a, c in l.t.items():
        if a[0] == 's' and a[1] in locs:
            lo, hi, tag = locs[a[1]]
            out = out + (hi if (c > 0) == upper else lo).scale(c)
            tags.add(tag)
            g = lo - hi
            if g.const() is None:
                ne.append(g)
        elif a[0] in ('div', 'mod', 'wrap') and has_local(a, locs):
            up = (c > 0) == upper
            inner = unfreeze(a[2] if a[0] == 'wrap' else a[1])
            if a[0] == 'div':
                b, t, n2 = eliminate_locals(inner, locs, up if a[2] > 0 else not up)
                out = out + mk_opaque(('div', b.key(), a[2])).scale(c)
                tags |= t
                ne += n2
                continue
            ilo, tlo, nlo = eliminate_locals(inner, locs, False)
            ihi, thi, nhi = eliminate_locals(inner, locs, True)
            span = a[2] if a[0] == 'mod' else 1 << a[1]
            if a[0] == 'mod' and a[2] <= 0:
                out = out + Lin(0, {a: c})
            elif ilo.const() is not None and ihi.const() is not None and 0 <= ilo.const() and ihi.const() < span:
                out = out + (ihi if up else ilo).scale(c)
                tags |= tlo | thi
                ne += nlo + nhi
            elif a[0] == 'wrap' or (ilo.const() is not None and ilo.const() >= 0):
                out = out + Lin(span - 1 if up else 0).scale(c)
                tags.add('mask' if a[0] == 'mod' else 'type')
            else:
                out = out + Lin(0, {a: c})
        else:
            out = out + Lin(0, {a: c})
    return out, tags, ne


def parent_call(req, call):
    """成员数组实参的外层对象（SPEC-0035）：req 的对象是 *pK、第 K 个实参带 parent → 把它换成外层对象的调用点；否则 None。"""
    m = re.match(r'^\*p([0-9]+)$', req['obj'])
    if req.get('dir') or not m or int(m.group(1)) >= len(call['args']):
        return None                                    # 反向 Req 只查成员起点（SPEC-0039 §2.1）
    k = int(m.group(1))
    arg = call['args'][k]
    if not isinstance(arg, dict) or 'parent' not in arg:
        return None
    outer = {'obj': arg['parent']['obj'], 'have': arg['parent']['have']}
    if 'eff_depth' in arg:
        outer['eff_depth'] = arg['eff_depth']
    args = list(call['args'])
    args[k] = outer
    return dict(call, args=args)


def classify_all(req, call, caller, callee, env_guards):
    """调用点判定的全部结局 [(结论, 调用点, 是不是外层对象那一条)]（SPEC-0035 §2.2）：成员已经报 / 记账 → 只有它；
    成员 safe → 外层不 safe 就换成外层的；成员 up → 外层不 safe 时两条都要。"""
    mine = classify(req, call, caller, callee, env_guards)
    outer = parent_call(req, call)
    if outer is None or mine['outcome'] not in ('safe', 'up'):
        return [(mine, call, False)]
    o = classify(req, outer, caller, callee, env_guards)
    if o['outcome'] == 'safe':
        return [(mine, call, False)]
    return ([] if mine['outcome'] == 'safe' else [(mine, call, False)]) + [(o, outer, True)]


def classify(req, call, caller, callee, env_guards):
    m, effd = build_map(call, callee)
    have = subst(P(req['have']), m)
    elo, ehi = [subst(P(x), m) for x in req['end']]
    guards = [g for s in req.get('guard', []) for g in C(subst_cond(s, m))]
    locs = {n: (P(v['range'][0]), P(v['range'][1]), v.get('bound', 'loop')) for n, v in call.get('locals', {}).items()}
    env = Env(caller.get('params', []), caller.get('fields', {}))
    for g, tag in env_guards:
        env.add_guard(g, tag)
    obj2, via = subst_obj(req['obj'], call)
    trace = {'have': have, 'end': (elo, ehi), 'guards': guards, 'obj': obj2, 'via': via}
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
    # 实参是不跟踪的指针（调用点记录的占位符 local:_unknownK / _unknownK）：判不了也传不上去（SPEC-0062）
    if obj2.startswith('local:_unknown') and show(have) == obj2[6:]:
        return dict(trace, outcome='open', reason='untracked_pointer', why='实参是不跟踪的指针，大小未知')
    # 被调者经实参所指对象里的指针访问，调用方不知道那个指针指向哪里（SPEC-0063）
    if any(a[0] == 's' and a[1].startswith('_unmapped:') for a in have.t):
        return dict(trace, outcome='open', reason='untracked_pointer', why='对象大小 %s 在调用方不知道' % show(have))
    # ② 局部换成区间
    ehi2, tags_hi, ne = eliminate_locals(ehi, locs, True)
    elo2, _, _ = eliminate_locals(elo, locs, False)
    if call.get('alt'):
        elo2 = P('0')  # 函数指针的多个目标之一：这次不一定执行（只判 L2，SPEC-0058 §2.3）
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
        # widening 去掉的上界是指针步进（A-45）；type / data 只剩类型范围（SPEC-0038）
        return dict(base, outcome='open', reason='pointer_stepping' if bound == 'widen' else 'type_range_only', why='上界只来自 %s，不算代码给出的界' % bound)
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
    if r.get('obj') != mine['obj']:
        probs.append('obj: JSON %s / 回放 %s' % (r.get('obj'), mine['obj']))
    return probs


def rep_of(mine, call, fn, s, r_callee):
    """调用点判出 L1 / L2 时，按机器字段组装判定记录（与 JSON reps[] 同形，不含 id / doc）。"""
    rep = {'level': mine['outcome'], 'reported': max(mine['depth'], mine['eff_depth']) > INLINE_DEPTH, 'site': call['site'],
           'access': r_callee['access'], 'kind': r_callee['kind'], 'obj': mine['obj']}
    if r_callee.get('dir'):
        rep['dir'] = r_callee['dir']                   # 负偏移（SPEC-0039）
    if mine.get('via'):
        rep['via'] = mine['via']
    lo, hi = mine['end2']
    if mine.get('hi_end') is not None:                 # 守卫界判定（F-12b）：末端取守卫给出的范围
        hi = Lin(int(mine['hi_end']))
        if mine.get('lo_end') is not None:
            lo = Lin(int(mine['lo_end']))
    rep.update({'have': show(mine['have']), 'end': [show(lo), show(hi)], 'bound': mine['bound'], 'depth': mine['depth'],
                'eff_depth': mine['eff_depth'], 'chain': ['%s@%s' % (fn, call['site'])] + r_callee['path']})
    rep['fp'] = fingerprint(rep['level'], s['key'], rep['access'], rep['kind'], fingerprint_obj(rep['obj'], rep.get('dir')))
    return rep


def fingerprint_obj(obj, dir_):
    """指纹里的对象：反向 Req 加 "#back"，与同一次访问的正向区分（SPEC-0039）；正向不变。"""
    return obj + '#' + dir_ if dir_ else obj


def diff_rep(want, got):
    keys = [k for k in REQUIRED['rep'] if k != 'id'] + ['via', 'dir']
    return ['%s: JSON %s / 推出 %s' % (k, json.dumps(want.get(k), ensure_ascii=False), json.dumps(got.get(k), ensure_ascii=False))
            for k in keys if want.get(k) != got.get(k)]


def apply_eff(eff, call):
    """被调者的效果代进调用点 → 结构化的"应用后的效果"（与 JSON 里 results[].apply 同形）。"""
    m, effd = build_map(call, None)
    if eff['kind'] == 'ret_new':
        return {'kind': 'ret_new', 'size': show(subst(P(eff['size']), m)), 'maybe_null': bool(eff.get('maybe_null'))}
    if eff['kind'] == 'ret_int':
        return {'kind': 'ret_int', 'range': [show(subst(P(x), m)) for x in eff['range']]}
    if eff['kind'] == 'ret_cond':
        return {'kind': 'ret_cond', 'when': eff['when'], 'then': [canon_cond(subst_cond(s, m)) for s in eff['then']]}
    if eff['kind'] == 'field':
        k, f = re.match(r'p(\d+)->(\w+)$', eff['target']).groups()
        return {'kind': 'field', 'arg': int(k), 'field': f, 'value': show(subst(P(eff['value']), m))}
    if eff['kind'] == 'out_new':                       # 出参写回的新对象（SPEC-0053；写进字段：SPEC-0061）
        k, f = re.match(r'p(\d+)(?:->(\w+))?$', eff['target']).groups()
        a = {'kind': 'out_new', 'arg': int(k), 'size': show(subst(P(eff['size']), m)), 'maybe_null': bool(eff.get('maybe_null'))}
        if f:
            a['field'] = f
        return a
    return None


def canon_cond(s):
    return ' && '.join(show_atom_le0(g) for g in C(s))


def same_apply(a, b):
    if a is None or b is None or a['kind'] != b['kind']:
        return False
    if a['kind'] == 'ret_new':
        return P(a['size']) == P(b['size']) and a['maybe_null'] == b.get('maybe_null', False)
    if a['kind'] == 'ret_cond':
        return a['when'] == b['when'] and sorted(a['then']) == sorted(canon_cond(x) for x in b['then'])
    if a['kind'] == 'field':
        return a['arg'] == b['arg'] and a['field'] == b['field'] and P(a['value']) == P(b['value'])
    if a['kind'] == 'out_new':                         # SPEC-0053 / 0061
        return a['arg'] == b['arg'] and a.get('field') == b.get('field') and P(a['size']) == P(b['size']) and \
            a['maybe_null'] == b.get('maybe_null', False)
    if a['kind'] == 'ret_int':
        return [P(x) for x in a['range']] == [P(x) for x in b['range']]
    return False


def fmt_apply(a):
    if a is None:
        return '?'
    if a['kind'] == 'ret_new':
        return 'new(%s)%s' % (a['size'], ' 或 NULL' if a['maybe_null'] else '')
    if a['kind'] == 'ret_cond':
        return '%s ⇒ %s' % (a['when'], ' 且 '.join(a['then']))
    if a['kind'] == 'field':
        return '实参 %d 所指对象的 %s := %s' % (a['arg'], a['field'], a['value'])
    if a['kind'] == 'out_new':                         # SPEC-0053 / 0061
        lhs = '实参 %d 所指对象的 %s' % (a['arg'], a['field']) if a.get('field') else '*实参 %d' % a['arg']
        return '%s := new(%s)%s' % (lhs, a['size'], ' 或 NULL' if a['maybe_null'] else '')
    return json.dumps(a, ensure_ascii=False)


# ---------------------------------------------------------------- 回放一个用例
def label(a):
    if isinstance(a, dict):
        return a.get('doc') or a['obj']          # 有 doc 用原文，没有就用机器写法
    return '…' if a is None else str(a)


def replay(cid, verbose=False, out=None):
    meta, fns_doc = load_case(cid)
    fns = machine(fns_doc)            # 计算只用机器字段
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
            dcall = next(c for c in fns_doc[fn]['calls'] if c['site'] == call['site'] and c['callee'] == call['callee'])
            say('- %s @ %s  调用 %s(%s)' % (fn, call['site'], call['callee'], ', '.join(label(a) for a in dcall['args'])))
            if not call.get('results'):
                say('    · 被调者没有 Req、没有要用的 Eff')
            if callee is None:
                for res in call.get('results', []):
                    if 'gen' in res:
                        say('    · 访问类模型 %s → 在本函数生成 %s' % (call['callee'], '、'.join(res['gen'])))
                    elif 'gen_eff' in res:
                        say('    · 分配器模型 %s → 本函数的效果 %s' % (call['callee'], '、'.join('effs[%d]' % i for i in res['gen_eff'])))
                    elif 'pure' in res:
                        say('    · 纯函数模型 %s：不失效、不产生条件' % call['callee'])
                    elif 'open' in res:
                        say('    · 未知函数 %s：记账 %s' % (call['callee'], res['open']))
                continue
            env_guards = [(g, x.get('bound', 'guard')) for x in call.get('guards', []) for g in C(x['cond'])]
            for res in call.get('results', []):
                if 'eff' in res:
                    eff = callee['effs'][res['eff']]
                    got = apply_eff(eff, call)
                    ok = same_apply(got, res['apply'])
                    say('    · 效果 %s → %s %s' % (eff['kind'], fmt_apply(got), '' if ok else '✗ JSON 记的是 %s' % res['apply']))
                    if not ok:
                        errors.append('%s %s 效果不一致' % (cid, fn))
                    continue
                r = next((x for x in callee['reqs'] if x['id'] == res['req']), None)
                if r is None:
                    errors.append('%s %s: 被调者没有 %s' % (cid, call['callee'], res['req']))
                    continue
                used = parent_call(r, call) if res.get('parent') else call
                if used is None:
                    errors.append('%s %s: %s 的实参没有外层对象（parent）' % (cid, fn, call['site']))
                    continue
                mine = classify(r, used, s, callee, env_guards)
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
                msg = '    · %s.%s%s：末端 %s，对象 %s（%s 字节）→ %s' % (call['callee'], r['id'], '（外层）' if res.get('parent') else '', show_end,
                                                              mine['obj'], show(mine['have']), mine['outcome'])
                if mine.get('why'):
                    msg += '（%s）' % mine['why']
                ok = mine['outcome'] == res['outcome']
                if ok and mine['outcome'] == 'up':
                    probs = same_req(reqs[res['as']], mine)
                    if reqs[res['as']].get('dir', '') != r.get('dir', ''):   # SPEC-0039
                        probs.append('dir: JSON %s / 被调者 %s' % (reqs[res['as']].get('dir', ''), r.get('dir', '')))
                    msg += ' → 本函数 %s' % res['as']
                    if probs:
                        ok = False
                        msg += ' ✗ ' + '；'.join(probs)
                if ok and mine['outcome'] in ('L1', 'L2'):
                    rep = reps.get(res['report'])
                    got = rep_of(mine, used, fn, s, r)
                    reported = got['reported']
                    bad = diff_rep(rep, got) if rep else ['JSON 里没有 %s' % res['report']]
                    drep = next((x for x in fns_doc[fn]['reps'] if x['id'] == res['report']), {})
                    dmsg = (drep.get('doc') or {}).get('msg')
                    if rep and dmsg is not None and alert_msg(got, s.get('params', [])) != dmsg:   # 没有 doc（机器版）就不比
                        bad.append('告警文本与 doc.msg 不同')
                    if bad:
                        ok = False
                        msg += ' ✗ 判定记录不一致：' + '；'.join(bad)
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
    return lines, errors, verdicts, meta, fns_doc, order


# ---------------------------------------------------------------- 从局部事实推到底
def req_key(have, obj, lo, hi, guards, depth, dir_=None):
    key = (show(have), obj, show(lo), show(hi), frozenset(show_atom_le0(g) for g in guards), depth)
    return key + (dir_,) if dir_ else key              # 正向的键不变（SPEC-0039）


def json_req_key(r):
    return req_key(P(r['have']), r['obj'], P(r['end'][0]), P(r['end'][1]), [g for s in r.get('guard', []) for g in C(s)], r['depth'],
                   r.get('dir'))


def typed_field(r, params):
    """SPEC-0019 §2.1：对象是入口自己的形参所指 *pK、have 就是 ext(pK)、末端上界是常数且 ≤ 形参声明的所指类型大小。"""
    m = re.match(r'^\*p(\d+)$', r['obj'])
    if not m or r['hi'].const() is None:
        return False
    k = int(m.group(1))
    if k >= len(params) or 'size' not in params[k] or show(r['have']) != 'ext(p%d)' % k:
        return False
    return r['hi'].const() <= params[k]['size']


def absorb(reqs):
    """覆盖合并（06- A-44）：同一对象、同样守卫，end 上界恒不大于另一条的那条并进去。"""
    keep = []
    for i, r in enumerate(reqs):
        dominated = False
        for j, r2 in enumerate(reqs):
            if i == j or show(r['have']) != show(r2['have']) or r['obj'] != r2['obj'] or r['gkey'] != r2['gkey'] or \
                    r.get('dir') != r2.get('dir'):
                continue
            d = (r['hi'] - r2['hi']).const()
            if d is not None and (d < 0 or (d == 0 and j < i)):
                dominated = True
                break
        if not dominated:
            keep.append(r)
    return keep


def derive(cid):
    """只用每个函数自己的局部事实，自底向上推出全部上传 Req、判定与入口账本，组装最终告警，再与 JSON 和 @expect 对照。

    "局部事实"＝本函数访问点当场生成的 Req（depth 0，含被覆盖的 absorbed）、调用点处的实参（值、指针的 obj / have / 字段）、
    局部区间、守卫。上传来的 Req（depth ≥ 1）和 reps / opens 一概不读，全部重新推出来。
    """
    meta, fns_doc = load_case(cid)
    fns = machine(fns_doc)
    order = topo(fns)
    callers = callers_of(fns)
    dreqs, alerts, probs, stat = {}, [], [], {'up': 0, 'rep': 0, 'entry': 0}
    for fn in order:
        s = fns[fn]
        cand = []
        for r in s['reqs']:
            if r['depth'] != 0:
                continue                                   # 上传来的不拿，自己推
            g = [x for c in r.get('guard', []) for x in C(c)]
            for kind, end, at in [(r['kind'], r['end'], r['access']['at'])] + [(a['kind'], a['end'], a['at']) for a in r.get('absorbed', [])]:
                cand.append({'have': P(r['have']), 'obj': r['obj'], 'lo': P(end[0]), 'hi': P(end[1]), 'guards': g,
                             'gkey': frozenset(show_atom_le0(x) for x in g), 'depth': 0, 'eff_depth': r['eff_depth'], 'bound': r['bound'],
                             'path': r['path'], 'access': r['access'], 'kind': kind, 'dir': r.get('dir')})
        reps = []
        for call in s['calls']:
            callee = fns.get(call['callee'])
            if callee is None:
                continue
            env_guards = [(g, x.get('bound', 'guard')) for x in call.get('guards', []) for g in C(x['cond'])]
            for r in dreqs[call['callee']]:
                rj = {'have': show(r['have']), 'obj': r['obj'], 'end': [show(r['lo']), show(r['hi'])],
                      'guard': [show_atom_le0(g) for g in r['guards']], 'depth': r['depth'], 'eff_depth': r['eff_depth'], 'bound': r['bound']}
                if r.get('dir'):
                    rj['dir'] = r['dir']                   # 负偏移（SPEC-0039）
                path = ['%s@%s' % (fn, call['site'])] + r['path']
                for mine, used, _ in classify_all(rj, call, s, callee, env_guards):   # 成员数组实参：外层也查（SPEC-0035）
                    if mine['outcome'] == 'up':
                        g = mine.get('guards2', [])
                        cand.append({'have': mine['have'], 'obj': mine['obj'], 'lo': mine['end2'][0], 'hi': mine['end2'][1], 'guards': g,
                                     'gkey': frozenset(show_atom_le0(x) for x in g), 'depth': mine['depth'], 'eff_depth': mine['eff_depth'],
                                     'bound': mine['bound'], 'path': path, 'access': r['access'], 'kind': r['kind'],
                                     'dir': r.get('dir')})
                        stat['up'] += 1
                    elif mine['outcome'] in ('L1', 'L2'):
                        reps.append(rep_of(mine, used, fn, s, r))
                        stat['rep'] += 1
        mine_reqs = absorb(cand)
        dreqs[fn] = mine_reqs
        got = sorted(req_key(r['have'], r['obj'], r['lo'], r['hi'], r['guards'], r['depth'], r.get('dir')) for r in mine_reqs)
        want = sorted(json_req_key(r) for r in s['reqs'])
        if [repr(x) for x in got] != [repr(x) for x in want]:
            probs.append('%s %s：推出的 Req 与 JSON 不同\n      推出 %s\n      JSON %s' % (cid, fn, got, want))
        # 判定：推出的每条与 JSON reps[] 的机器字段逐项比；告警文本由模板生成，与 doc.msg 比（doc 只用来对照，不参与计算）
        jreps = list(s['reps'])
        if len(reps) != len(jreps):
            probs.append('%s %s：推出 %d 条判定 / JSON %d 条' % (cid, fn, len(reps), len(jreps)))
        for rep in reps:
            want_rep = next((x for x in jreps if x['site'] == rep['site'] and x['access'] == rep['access']), None)
            bad = diff_rep(want_rep, rep) if want_rep else ['JSON 里没有这条']
            doc_msg = next((((x.get('doc') or {}).get('msg')) for x in fns_doc[fn]['reps'] if want_rep and x['id'] == want_rep['id']), None)
            if doc_msg is not None and alert_msg(rep, s.get('params', [])) != doc_msg:   # 没有 doc（机器版）就不比
                bad.append('告警文本与 doc.msg 不同')
            if bad:
                probs.append('%s %s @ %s：推出的判定与 JSON 不同：%s' % (cid, fn, rep['site'], '；'.join(bad)))
            if rep['reported']:
                alerts.append(make_alert(fn, s, rep))
        if fn not in callers:
            for r in mine_reqs:
                env = Env(s.get('params', []), s.get('fields', {}))
                _, tag = env.range_of(r['hi'] - r['have'], True)
                reason = 'type_range_only' if r['have'].const() is not None and tag == 'type' else 'entry_unproven'
                if typed_field(r, s.get('params', [])):
                    reason = 'typed_field'
                stat['entry'] += 1
                if reason not in [o['reason'] for o in s['opens'] if o.get('req')]:
                    probs.append('%s %s：入口账本应为 %s' % (cid, fn, reason))
    verdicts = []
    for e in meta['expect']:
        mine = [a for a in alerts if a['fn'] == e['entry']]
        got = mine[0]['level'] if mine else '-'
        want = e['pa'] if e['pa'] in ('L1', 'L2') else '-'
        verdicts.append((e, got, got == want and len(mine) <= 1))
        if not verdicts[-1][2]:
            probs.append('%s %s：推出的告警 %s / 期望 %s' % (cid, e['entry'], got, e['pa']))
    for a in alerts:
        a['case'] = cid
    return alerts, probs, verdicts, stat


def fmt_range(r):
    return show(r[0]) if r[0] == r[1] else '[%s, %s]' % (show(r[0]), show(r[1]))


def all_cases():
    ids = [d for d in os.listdir(HERE) if os.path.isfile(os.path.join(HERE, d, 'case.json'))]
    return sorted(ids, key=lambda x: [int(n) for n in re.findall(r'\d+', x)])


def main():
    global SUMS
    ap = argparse.ArgumentParser()
    ap.add_argument('cases', nargs='*')
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--md')
    ap.add_argument('--strip', metavar='DIR')
    ap.add_argument('--dir', metavar='DIR')
    ap.add_argument('--alerts', metavar='FILE')
    a = ap.parse_args()
    if a.dir:
        SUMS = os.path.abspath(a.dir)
        print('摘要从 %s 读' % SUMS)
    ids = a.cases or all_cases()
    total_err, rows = [], []
    nfile, nl_hits = 0, []
    for cid in ids:
        for f in sorted(os.listdir(os.path.join(SUMS, cid))):
            if f.endswith('.json') and f != 'case.json':
                nfile += 1
                nl_hits += ['%s/%s %s' % (cid, f, x) for x in nl_outside_doc(json.load(open(os.path.join(SUMS, cid, f), encoding='utf-8')))]
    try:
        import jsonschema
        schema = json.load(open(os.path.join(HERE, 'summary.schema.json'), encoding='utf-8'))
        # 校验前去掉根上的相对 $id（"oobsum-summary/1"）：schema 里的 $ref 全是 "#/definitions/…"，不依赖基址，去掉不改变校验结果。
        # jsonschema 3.x（Ubuntu 22.04 的 python3-jsonschema，cloud-init 带进来）会把相对 $id 与基址再拼一次，
        # 得到 oobsum-summary/oobsum-summary/1，本地查不到就当远程地址去取，抛 RefResolutionError。
        schema.pop('$id', None)
        bad = []
        for cid in ids:
            for f in sorted(os.listdir(os.path.join(SUMS, cid))):
                if f.endswith('.json') and f != 'case.json':
                    try:
                        jsonschema.validate(json.load(open(os.path.join(SUMS, cid, f), encoding='utf-8')), schema)
                    except jsonschema.ValidationError as e:
                        bad.append('%s/%s 不符合 schema：%s（位置 %s）' % (cid, f, e.message, '/'.join(str(x) for x in e.absolute_path)))
        print('结构检查：%d 份摘要按 summary.schema.json 校验%s；doc 之外的自然语言 %d 处' % (
            nfile, '通过' if not bad else '有 %d 份不通过' % len(bad), len(nl_hits)))
        for b in bad:
            print('  ✗', b)
        if bad:
            sys.exit(1)
    except ImportError:
        print('结构检查：没装 jsonschema，只做内置检查（字段齐全、表达式可解析、账本原因在枚举里）；doc 之外的自然语言 %d 处' % len(nl_hits))
    except Exception as e:  # 校验器自身出错（版本不兼容等）：不挡回放，但把异常写清楚
        print('结构检查：⚠ jsonschema %s 校验器出错（%s: %s），只做内置检查（字段齐全、表达式可解析、账本原因在枚举里）；doc 之外的自然语言 %d 处' % (
            getattr(sys.modules.get('jsonschema'), '__version__', '?'), type(e).__name__, e, len(nl_hits)))
    for cid in ids:
        lines, errors, verdicts, meta, fns, order = replay(cid, a.verbose)
        total_err.extend(errors)
        rows.append((cid, meta, verdicts, len(fns), lines, fns, order))
        if not a.verbose:
            print('%-7s %-3s 个函数  %s' % (cid, len(fns), '  '.join('%s %s→%s%s' % (e['kind'], e['pa'], g, '' if ok else '✗') for e, g, ok in verdicts)))
    if a.md:
        write_md(a.md, rows)
    print('\n① 逐调用点核对：%d 个用例，%d 处不一致' % (len(ids), len(total_err)))
    total_err += nl_hits
    dprobs, allalerts, st = [], [], {'up': 0, 'rep': 0, 'entry': 0}
    for cid in ids:
        alerts, probs, verdicts, stat = derive(cid)
        dprobs += probs
        allalerts += alerts
        for k in st:
            st[k] += stat[k]
    print('② 从局部事实推到底：推出上传 Req %d 条、判定 %d 条、入口账本 %d 条，与 JSON %s；'
          '最终告警 %d 条（位置、消息、调用链、指纹都由机器字段生成），与 @expect %s'
          % (st['up'], st['rep'], st['entry'], '一致' if not dprobs else '有 %d 处不同' % len(dprobs), len(allalerts),
             '一致' if not [p for p in dprobs if '期望' in p] else '不一致'))
    if a.verbose or dprobs:
        for x in allalerts:
            print('    告警  %-7s %-2s %-24s @ %-34s fp %s' % (x['case'], x['level'], x['fn'], x['loc'], x['fp']))
    for p in dprobs:
        print('  ✗', p)
    total_err += dprobs
    gold = os.path.join(HERE, 'alerts.json')
    if not a.cases and os.path.exists(gold) and not a.alerts:
        same = json.load(open(gold, encoding='utf-8')) == allalerts
        print('   与 alerts.json（Report.cpp 的金标准）%s' % ('逐条相同' if same else '不同 ✗'))
        if not same:
            total_err.append('最终告警与 alerts.json 不同（有意修改就用 --alerts alerts.json 重新生成）')
    if a.alerts:
        open(a.alerts, 'w', encoding='utf-8').write(pretty(allalerts) + '\n')
        print('   最终告警已写到 %s' % a.alerts)
    if a.strip:
        n = 0
        for cid in ids:
            os.makedirs(os.path.join(a.strip, cid), exist_ok=True)
            for f in sorted(os.listdir(os.path.join(SUMS, cid))):
                if f.endswith('.json') and f != 'case.json':
                    s = strip_doc(json.load(open(os.path.join(SUMS, cid, f), encoding='utf-8')))
                    open(os.path.join(a.strip, cid, f), 'w', encoding='utf-8').write(pretty(s) + '\n')
                    n += 1
        print('③ 去掉 doc 的机器版已写到 %s（%d 份）；用 --dir %s 再跑一遍，结果应完全相同' % (a.strip, n, a.strip))
    for e in total_err:
        print('  ✗', e)
    sys.exit(1 if total_err else 0)


def m_req(r):
    """Req 的机器写法（一行）：[守卫 ⇒] 末端 ≤ have @ 对象 · 界的来源。"""
    lo, hi = r['end']
    rng = hi if lo == hi else '[%s, %s]' % (lo, hi)
    g = ' && '.join(r.get('guard', []))
    return '`%s%s <= %s` @ `%s` · %s%s' % (g + ' ⇒ ' if g else '', rng, r['have'], r['obj'], r['bound'],
                                         '（depth %d）' % r['depth'] if r['depth'] else '')


def m_eff(e):
    if e['kind'] == 'ret_new':
        return '`ret = new(%s)%s`' % (e['size'], ' | NULL' if e.get('maybe_null') else '')
    if e['kind'] == 'ret_int':
        return '`ret ∈ [%s, %s]`' % tuple(e['range'])
    if e['kind'] == 'ret_cond':
        return '`%s ⇒ %s`' % (e['when'], ' && '.join(e['then']))
    if e['kind'] == 'field':
        return '`%s := %s`' % (e['target'], e['value'])
    return '`%s`' % json.dumps({k: v for k, v in e.items() if k != 'doc'}, ensure_ascii=False)


def write_md(path, rows):
    out = []
    for cid, meta, verdicts, n, lines, fns, order in rows:
        out.append('### %s %s\n' % (cid, meta['title']))
        out.append(meta.get('point', '') + '\n')
        out.append('**代码**（`%s`；只保留函数体，头注释略）\n\n```c\n%s\n```\n' % ('`、`'.join(meta['files']), meta['code'].rstrip()))
        out.append('**摘要一览**（自底向上；每行一个函数；表里只列**机器字段**——引擎读的就是这些，说明文字在 JSON 的 `doc` 里；'
                   '完整 JSON 在 `cases/summaries/%s/<函数名>.json`）\n' % cid)
        out.append('| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |\n|---|---|---|---|')
        for fn in order:
            s = fns[fn]
            req = '<br>'.join('**%s** %s' % (r['id'], cell(m_req(r))) for r in s['reqs']) or '—'
            eff = '<br>'.join(cell(m_eff(e)) for e in s['effs']) or '—'
            dec = ['**%s %s** `%s` 末端 %s > %s（depth %d%s）%s' % (r['id'], r['level'], r['obj'], r['end'][1], r['have'], r['depth'],
                                                              '' if not r['eff_depth'] else ', eff_depth %d' % r['eff_depth'],
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


if __name__ == '__main__':
    main()
