#!/usr/bin/env python3
"""场景用例的索引、语法检查与 Infer 标尺。

用例文件头里的标记（`@id` `@title` `@scene` `@year` `@story` `@expect`）是机器可读的；
"摘要 / 串联"两段是给人看的。跨文件用例是一个目录，标记写在其中一个 .c 文件里。

用法：
  python3 run.py index                         # 生成 INDEX.md（全部用例一览）
  python3 run.py doc                           # 刷新 ../05-summary-design-and-cases.md 里 <!-- CASES:… --> 标记之间的表
  python3 run.py check  --cc gcc|clang         # 语法检查（正常 + -DCASE_SHIM 两遍）
  python3 run.py infer  --infer <infer 可执行文件> [--only lv1,lv2] [--out DIR]
                                               # Infer 当标尺（自动加 -DCASE_SHIM），与 @expect 的 pa= 对照
  python3 run.py csa    [--only lv1,LV2-03] [--config k=v] [--save]
                                               # 本仓库的 clang 跑 rb.enhance.ipa.OobSummary，按判定日志与 @expect 对照，
                                               # 并与基线 csa-baseline.json 比，原来报出的消失了（且没证明安全）就失败（SPEC-0002）

@expect 的写法：  @expect  TP|TN  <入口函数>  pa=<L1|L2|-|记账>  [later=<…>]  [ai+pa=<…>]
  pa       12-15 交付（P0）的程序分析期望。"-" = 不报；"记账" = 不报，但留一条"未证明 / 断链"记录
  later    P1 或 2027 补上相应能力之后的程序分析期望
  ai+pa    交给 AI + 程序分析之后的期望
只依赖 Python 3 标准库。
"""
import argparse, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ['lv%d' % i for i in range(1, 11)]
TAG = re.compile(r'^\s*\*\s*@(\w+)\s+(.*?)\s*$')


def units(root=HERE):
    """每个用例单元：(级别, 名字, [源文件], 头部标记文件)"""
    out = []
    for lv in LEVELS:
        d = os.path.join(root, lv)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            if name.endswith('.c'):
                out.append((lv, name[:-2], [p], p))
            elif os.path.isdir(p):
                srcs = sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith('.c'))
                head = next((s for s in srcs if '@id' in open(s, encoding='utf-8').read()), srcs[0])
                out.append((lv, name, srcs, head))
    return out


def parse(head):
    meta = {'expect': []}
    for line in open(head, encoding='utf-8'):
        m = TAG.match(line)
        if not m:
            continue
        k, v = m.group(1), m.group(2)
        if k == 'expect':
            parts = v.split()
            e = {'kind': parts[0], 'entry': parts[1]}
            for kv in parts[2:]:
                if '=' in kv:
                    a, b = kv.split('=', 1)
                    e[a] = b
            meta['expect'].append(e)
        else:
            meta[k] = v
    return meta


def cmd_index(_):
    rows = []
    for lv, name, srcs, head in units():
        m = parse(head)
        exp = '；'.join('%s %s pa=%s%s' % (e['kind'], e['entry'], e.get('pa', '?'),
                                         (' ai+pa=' + e['ai+pa']) if 'ai+pa' in e else '') for e in m['expect'])
        rel = os.path.relpath(srcs[0] if len(srcs) == 1 else os.path.dirname(srcs[0]), HERE)
        rows.append('| %s | %s | %s | %s | %s | `%s` |' % (m.get('id', '?'), m.get('title', ''), m.get('scene', ''),
                                                         m.get('year', ''), exp, rel))
    text = ['# 场景用例一览（由 run.py index 生成）', '',
            '| 编号 | 用例 | 场景 / 签名 | 年内 | 期望 | 文件 |', '|---|---|---|---|---|---|'] + rows
    open(os.path.join(HERE, 'INDEX.md'), 'w', encoding='utf-8').write('\n'.join(text) + '\n')
    print('INDEX.md：%d 个用例' % len(rows))


def _exp_text(e):
    t = 'pa=' + e.get('pa', '?')
    for k in ('later', 'ai+pa'):
        if k in e:
            t += ' · %s=%s' % (k, e[k])
    return t


def cmd_doc(_):
    doc = os.path.join(HERE, '..', '05-summary-design-and-cases.md')
    text = open(doc, encoding='utf-8').read()
    by_lv = {}
    for lv, name, srcs, head in units():
        by_lv.setdefault(lv, []).append((name, srcs, parse(head)))

    def table(lv):
        out = ['| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |', '|---|---|---|---|---|---|---|']
        for name, srcs, m in by_lv.get(lv, []):
            tp = next((e for e in m['expect'] if e['kind'] == 'TP'), {})
            tn = next((e for e in m['expect'] if e['kind'] == 'TN'), {})
            rel = os.path.relpath(srcs[0] if len(srcs) == 1 else os.path.dirname(srcs[0]), os.path.join(HERE, '..'))
            out.append('| %s | %s | %s | %s | %s | %s | `%s` |' % (m.get('id'), m.get('title'), m.get('scene'), m.get('year'),
                                                               _exp_text(tp), _exp_text(tn), rel))
        return '\n'.join(out)

    def stats():
        out = []
        tot = [0, 0, 0, 0]
        for lv in LEVELS:
            items = by_lv.get(lv, [])
            tps = [next((e for e in m['expect'] if e['kind'] == 'TP'), {}) for _, _, m in items]
            rep_ = sum(1 for e in tps if e.get('pa') in ('L1', 'L2'))
            quiet = len(tps) - rep_
            later = sum(1 for e in tps if e.get('pa') not in ('L1', 'L2') and ('later' in e or 'ai+pa' in e))
            out.append('| %s | %d | %d | %d | %d |' % (lv.upper().replace('LV', 'Lv'), len(items), rep_, quiet, later))
            for i, v in enumerate((len(items), rep_, quiet, later)):
                tot[i] += v
        out.append('| **合计** | **%d** | **%d** | **%d** | **%d** |' % tuple(tot))
        return '\n'.join(out)

    def put(text, key, body):
        b, e = '<!-- CASES:%s -->' % key, '<!-- /CASES:%s -->' % key
        if e in text:
            i, j = text.index(b) + len(b), text.index(e)
            return text[:i] + '\n' + body + '\n' + text[j:]
        return text.replace(b, b + '\n' + body + '\n' + e, 1)

    def appendix():
        out = []
        for lv in LEVELS:
            out.append('### %s' % lv.upper().replace('LV', 'Lv'))
            for name, srcs, m in by_lv.get(lv, []):
                head = open(srcs[0] if len(srcs) == 1 else next(x for x in srcs if '@id' in open(x, encoding='utf-8').read()),
                            encoding='utf-8').read()
                block = head[head.index('/*') + 2: head.index('*/')]
                lines = [ln[3:] if ln.startswith(' * ') else ln.lstrip(' *') for ln in block.splitlines()]
                story, body, mode = [], [], None
                for ln in lines:
                    if ln.startswith('@'):
                        mode = 'story' if ln.startswith('@story') else 'tag'
                        if mode == 'story':
                            story.append(ln[len('@story'):].strip())
                        continue
                    if mode == 'story' and ln.strip() and not body:
                        story.append(ln.strip())
                        continue
                    mode = 'body' if (ln.strip() or body) else mode
                    if mode == 'body':
                        body.append(ln.rstrip())
                while body and not body[-1].strip():
                    body.pop()
                exp = '；'.join('%s `%s` %s' % (e['kind'], e['entry'], _exp_text(e)) for e in m['expect'])
                out.append('')
                out.append('**%s %s**（%s · %s）' % (m.get('id'), m.get('title'), m.get('scene'), m.get('year')))
                out.append('')
                out.append(''.join(story) + '　期望：' + exp)
                out.append('')
                out.append('```text')
                out.extend(body)
                out.append('```')
            out.append('')
        return '\n'.join(out).rstrip()

    for lv in LEVELS:
        text = put(text, lv, table(lv))
    text = put(text, 'stats', stats())
    text = put(text, 'appendix', appendix())
    open(doc, 'w', encoding='utf-8').write(text)
    print('已刷新 05- 里的用例表')


def cmd_check(a):
    bad = 0
    n = 0
    for shim in (False, True):
        for lv, name, srcs, head in units():
            for s in srcs:
                n += 1
                args = [a.cc, '-std=c11', '-fsyntax-only', '-Wall', '-Wextra',
                        '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable']
                if shim:
                    args.append('-DCASE_SHIM')
                r = subprocess.run(args + [s], capture_output=True, text=True)
                if r.returncode != 0 or r.stderr.strip():
                    bad += 1
                    print('== %s%s\n%s' % (os.path.relpath(s, HERE), ' [shim]' if shim else '', r.stderr.strip()))
    print('语法检查：%d 次编译，失败 %d' % (n, bad))
    return 1 if bad else 0


def cmd_infer(a):
    only = set(a.only.split(',')) if a.only else None
    out = os.path.abspath(a.out or os.path.join(os.getcwd(), 'infer-out-cases'))
    os.makedirs(out, exist_ok=True)
    rows = []
    for lv, name, srcs, head in units():
        if only and lv not in only:
            continue
        m = parse(head)
        rd = os.path.join(out, name)
        if os.path.isdir(rd):
            shutil.rmtree(rd)
        args = [a.infer, '--bufferoverrun-only', '--headers', '--results-dir', rd, '--no-progress-bar', '--',
                'clang', '-DCASE_SHIM', '-c'] + srcs
        subprocess.run(args, capture_output=True, text=True, timeout=600, cwd=out)
        try:
            rep = json.load(open(os.path.join(rd, 'report.json')))
        except (OSError, ValueError):
            rep = []
        got = {}
        for r in rep:
            if r['bug_type'].startswith('BUFFER_OVERRUN_'):
                got.setdefault(r['procedure'].split('(')[0], set()).add(r['bug_type'].replace('BUFFER_OVERRUN_', ''))
        for e in m['expect']:
            g = ','.join(sorted(got.get(e['entry'], []))) or '-'
            pa = e.get('pa', '?')
            if pa in ('L1', 'L2'):
                verdict = '一致' if g == pa else ('级别不同' if g != '-' else 'Infer 未报')
            elif pa in ('-', '记账'):
                verdict = '一致' if g == '-' else 'Infer 报了'
            else:
                verdict = ''
            rows.append((m.get('id', name), e['kind'], e['entry'], pa, g, verdict))
    w = max(len(r[2]) for r in rows) if rows else 10
    print('%-8s %-3s %-*s %-8s %-8s %s' % ('用例', '', w, '入口', '期望pa', 'Infer', '对照'))
    for r in rows:
        print('%-8s %-3s %-*s %-8s %-8s %s' % (r[0], r[1], w, r[2], r[3], r[4], r[5]))
    from collections import Counter
    c = Counter(r[5] for r in rows)
    print('合计：' + '，'.join('%s %d' % (k or '其它', v) for k, v in c.items()))
    json.dump(rows, open(os.path.join(out, 'yardstick.json'), 'w'), ensure_ascii=False, indent=1)


# ---- csa：本仓库的 clang 跑 rb.enhance.ipa.OobSummary，按判定日志与 @expect 对照（SPEC-0002） ----

REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CHECKER = 'rb.enhance.ipa.OobSummary'
STRENGTH = {'definite': 3, 'possible': 2, 'suspect': 1, 'safe': 0}
PA_LABEL = {'L1': 'definite', 'L2': 'possible', '记账': 'suspect'}
VERDICTS = ['通过', '通过+S', '级别不同', '漏报', '误报', '超出预期', '需跨TU']


def _fn_name(fn):
    """判定的 fn 以后是函数键（F:名字、F@<文件 id>:名字#哈希），取函数名（SPEC-0002 §2.2）"""
    return fn.rsplit(':', 1)[-1].split('#', 1)[0]


def _tag(d):
    return d['label'].upper() + (':' + d['reason'] if d.get('reason') else '')


def _pad(s, w):
    """按终端显示宽度补空格（中文占两格）"""
    import unicodedata
    return s + ' ' * max(0, w - sum(2 if unicodedata.east_asian_width(ch) in 'WF' else 1 for ch in s))


def _entry_actual(ds):
    """入口的实际（SPEC-0002 §2.2）：(显示文本, 报出的最强标签——不含 safe，没有为 None)"""
    em = [d for d in ds if d.get('emitted')]
    pool = em or ds
    if not pool:
        return '未判', None
    best = max(pool, key=lambda d: STRENGTH.get(d['label'], -1))
    if not em:
        return '%s(未报出:%s)' % (_tag(best), best.get('not_emitted_because', '?')), None
    text = _tag(best) + ('×%d' % len(em) if len(em) > 1 else '')
    return text, (best['label'] if best['label'] != 'safe' else None)


def _reachable(starts, edges):
    """从 starts 经静态调用能到达的函数键（含自己，SPEC-0047）"""
    seen, todo = set(), list(starts)
    while todo:
        k = todo.pop()
        if k in seen:
            continue
        seen.add(k)
        todo.extend(edges.get(k, ()))
    return seen


def _verdict(kind, pa, rep, cross_tu):
    """对照口径（SPEC-0002 §2.3，修订 06- T-03）；rep 是报出的最强标签（不含 safe）"""
    strong = rep in ('definite', 'possible')
    if pa in ('L1', 'L2'):
        if rep == PA_LABEL[pa]:
            return '通过'
        if rep:
            return '级别不同'
        return '需跨TU' if cross_tu else '漏报'
    if pa in ('记账', '-'):
        tp_ledger = pa == '记账' and kind == 'TP'
        if strong:
            return '误报' if kind == 'TN' else '超出预期'
        if rep == 'suspect':
            return '通过' if tp_ledger else '通过+S'
        if tp_ledger:
            return '需跨TU' if cross_tu else '漏报'
        return '通过'
    return '?'


def _show(p):
    r = os.path.relpath(p)
    return p if r.startswith('..') else r


def _read_logs(wd, srcs, root):
    """读一个用例单元的判定日志；返回 (判定列表, 缺日志的源文件)"""
    decs, seen = [], set()
    ddir = os.path.join(wd, 'decisions')
    for f in sorted(os.listdir(ddir)) if os.path.isdir(ddir) else []:
        lines = [json.loads(ln) for ln in open(os.path.join(ddir, f), encoding='utf-8') if ln.strip()]
        if not lines or lines[0].get('schema') != 'oobsum-decisions/1':
            continue
        seen.add(os.path.normpath(lines[0]['file']))
        for d in lines[1:]:
            path, line, col = d['site'].rsplit(':', 2)
            if os.path.isabs(path):
                path = os.path.relpath(path, root)
            d['site'] = '%s:%s:%s' % (path, line, col)
            # 调用链终点（访问点）的 文件:行（SPEC-0002 §2.4 的修订：同一次访问改报在汇合点也算仍报出）
            if d.get('chain'):
                apath, aline = d['chain'][-1].split('@', 1)[1].rsplit(':', 1)
                if os.path.isabs(apath):
                    apath = os.path.relpath(apath, root)
                d['access_site'] = '%s:%s' % (apath, aline)
            decs.append(d)
    return decs, [s for s in srcs if os.path.normpath(s) not in seen]


def cmd_csa(a):
    root = os.path.abspath(a.cases or HERE)
    clang = a.clang or os.path.join(REPO, 'cmake-build-release', 'bin', 'clang')
    out = os.path.abspath(a.out or os.path.join(REPO, 'cmake-build-release', 'oobsum-csa'))
    baseline = a.baseline or os.path.join(root, 'csa-baseline.json')
    if a.save and a.only:
        sys.exit('--save 不能与 --only 同用（基线必须覆盖全部用例）')
    if not os.access(clang, os.X_OK):
        sys.exit('找不到 clang：%s（先 ninja -C cmake-build-release clang，或用 --clang）' % clang)
    res = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    only = set(a.only.split(',')) if a.only else None

    entries, emitted, others, errors = [], [], [], []
    safe_sites, ran, ntu = set(), set(), 0
    reported_accesses = set()  # (用例, 访问点 文件:行)：报出的判定的调用链终点
    safe_accesses = set()      # (用例, 函数, 访问点 文件:行)：在这个函数里证明了安全的访问（SPEC-0025）
    safe_anywhere = set()      # (用例, 访问点)：有 SAFE 判定的访问（在调用链上哪一层证明的都算，SPEC-0034）
    silent_accesses = set()    # (用例, 访问点)：有既不是 SAFE、也没报出的判定（被 InlineDepth 等滤掉）
    for lv, name, srcs, head in units(root):
        m = parse(head)
        cid = m.get('id', name)
        if only and lv not in only and cid not in only:
            continue
        ran.add(cid)
        wd = os.path.join(out, cid)
        if os.path.isdir(wd):
            shutil.rmtree(wd)
        os.makedirs(wd)
        edges = {}  # 函数键 → 被调者的键（Dump=summary 的 calls，SPEC-0047）
        names_of = {}  # 函数键 → 函数名
        xtu = len(srcs) > 1 and not any(kv.startswith('Phase=') for kv in a.config)  # 多文件：三阶段（SPEC-0055）
        want_dump = not any(kv.startswith('Dump=') for kv in a.config)

        def cc1(src, extra, err):
            args = [clang, '-cc1', '-triple', a.triple, '-analyze', '-resource-dir', res,
                    '-internal-isystem', os.path.join(res, 'include'), '-analyzer-checker=' + CHECKER,
                    '-analyzer-config', '%s:WorkDir=%s' % (CHECKER, wd)]
            if a.triple.startswith(('arm', 'thumb', 'aarch64')):
                args.append('-fno-signed-char')  # driver 对 ARM / AArch64 会加；直接跑 cc1 要自己加（SPEC-0007 §2.1）
            for kv in a.config + extra:
                args += ['-analyzer-config', '%s:%s' % (CHECKER, kv)]
            args += a.cc1_arg + [src]
            r = subprocess.run(args, capture_output=True, text=True)
            err.write('== %s\n%s%s' % (' '.join(args), r.stdout, r.stderr))
            if r.returncode != 0:
                errors.append('%s：cc1 退出码 %d（见 %s）' % (os.path.relpath(src, root), r.returncode,
                                                           os.path.join(wd, 'stderr.txt')))
            for line in r.stderr.splitlines():
                if line.startswith('[oobsum:summary] '):
                    try:
                        sm = json.loads(line[len('[oobsum:summary] '):])
                    except ValueError:
                        continue
                    names_of[sm['key']] = sm['fn']
                    edges.setdefault(sm['key'], set()).update(c.get('callee_key', '') for c in sm.get('calls', []))

        dump = ['Dump=summary'] if want_dump else []  # 取调用图（SPEC-0047），不影响结果
        with open(os.path.join(wd, 'stderr.txt'), 'w', encoding='utf-8') as err:
            if xtu:
                for s in srcs:
                    ntu += 1
                    cc1(s, ['Phase=export'], err)
                stub_dir = os.path.join(wd, 'solve_stub')
                os.makedirs(stub_dir, exist_ok=True)
                stub = os.path.join(stub_dir, 'stub.c')
                open(stub, 'w', encoding='utf-8').write('void oobsum_solve_stub(void) {}\n')
                cc1(stub, ['Phase=solve'] + dump, err)
                for s in srcs:
                    cc1(s, ['Phase=report'], err)
            else:
                for s in srcs:
                    ntu += 1
                    cc1(s, dump, err)
        decs, missing = _read_logs(wd, srcs, root)
        for s in missing:
            errors.append('%s：没有写出判定日志（checker 没跑？）' % os.path.relpath(s, root))

        names = {e['entry'] for e in m['expect']}
        for e in m['expect']:
            actual, rep = _entry_actual([d for d in decs if _fn_name(d['fn']) == e['entry']])
            pa = e.get('pa', '?')
            own_keys = {k for k, n in names_of.items() if n == e['entry']}
            reach = _reachable(own_keys, edges) - own_keys
            if e['kind'] == 'TP' and rep not in ('definite', 'possible'):
                # L1 / L2 可以判在入口调用链上的汇合点（SPEC-0048）：取那里报出的最强 DEFINITE / POSSIBLE
                strong = [d for d in decs if d.get('emitted') and d['label'] in ('definite', 'possible') and d['fn'] in reach]
                if strong:
                    best = max(strong, key=lambda d: STRENGTH.get(d['label'], -1))
                    rep = best['label']
                    actual = '%s@%s' % (_tag(best), names_of.get(best['fn'], _fn_name(best['fn']))) + \
                        ('×%d' % len(strong) if len(strong) > 1 else '')
            if pa == '记账' and e['kind'] == 'TP' and rep is None:
                # 账本可以在入口经静态调用能到达的函数里（被调者本函数的账本、没有调用方的函数，SPEC-0047）
                sus = [d for d in decs if d.get('emitted') and d['label'] == 'suspect' and d['fn'] in reach]
                if sus:
                    rep = 'suspect'
                    actual = '%s@%s' % (_tag(sus[0]), names_of.get(sus[0]['fn'], _fn_name(sus[0]['fn']))) + \
                        ('×%d' % len(sus) if len(sus) > 1 else '')
            entries.append({'case': cid, 'kind': e['kind'], 'entry': e['entry'], 'pa': pa, 'actual': actual,
                            'verdict': _verdict(e['kind'], pa, rep, len(srcs) > 1 and not xtu)})
        for d in decs:
            key = (cid, d['site'], d['kind'])
            if d['label'] == 'safe':
                safe_sites.add(key)
                safe_sites.add((cid, d['site'], ''))  # 账本判定（没有 kind）：同一处有任意 kind 的 SAFE 就算证明了（SPEC-0024）
            if d.get('emitted') and d.get('access_site'):
                reported_accesses.add((cid, d['access_site']))
            if d['label'] == 'safe' and d.get('access_site'):
                safe_accesses.add((cid, d['fn'], d['access_site']))
                safe_anywhere.add((cid, d['access_site']))
            if d['label'] != 'safe' and not d.get('emitted') and d.get('access_site'):
                silent_accesses.add((cid, d['access_site']))
            if d.get('emitted'):
                rec = {'case': cid, 'site': d['site'], 'kind': d['kind'], 'fn': d['fn'], 'label': d['label'],
                       'reason': d.get('reason', '')}
                if d.get('access_site'):
                    rec['access'] = d['access_site']  # 调用链终点（访问点）：回退检查按访问认（SPEC-0025）
                emitted.append(rec)
                if _fn_name(d['fn']) not in names:
                    others.append(rec)
    emitted.sort(key=lambda r: (r['case'], r['site'], r['kind'], r['label'], r['reason']))
    result = {'schema': 'oobsum-csa-result/1',
              'options': {'triple': a.triple, 'config': a.config, 'cc1': a.cc1_arg},
              'entries': entries, 'emitted': emitted}

    print('== clang=%s  triple=%s  用例 %d 个、TU %d 个、入口 %d 个' % (clang, a.triple, len(ran), ntu, len(entries)))
    w = max([len(e['entry']) for e in entries] + [4])
    for row in [('用例', '', '入口', 'pa', '实际', '对照')] + [
            (e['case'], e['kind'], e['entry'], e['pa'], e['actual'], e['verdict']) for e in entries]:
        print(' '.join(_pad(x, n) for x, n in zip(row, (8, 3, w, 5, 34))) + ' ' + row[5])
    for r in others:
        print('其它报出：%s %s %s %s fn=%s' % (r['case'], r['site'], r['kind'], _tag(r), r['fn']))
    from collections import Counter
    c = Counter(e['verdict'] for e in entries)
    print('合计 %d 个入口：%s；其它报出 %d 条' % (len(entries), '，'.join('%s %d' % (v, c[v]) for v in VERDICTS + ['?'] if c[v]),
                                          len(others)))
    for msg in errors:
        print('✗ 运行错误：' + msg)

    # 召回不回退（SPEC-0002 §2.4）：基线里报出的，这次同一处要么仍报出，要么有 SAFE；
    # 或者这处访问是某条报出的判定的调用链终点（同一次访问改报在了汇合点，SPEC-0023 修订）；
    # 或者同一个函数里这次访问被证明了安全（SPEC-0025 修订：入口账本记在函数声明处，改判后位置在调用点）；
    # 或者这次访问在调用链上某层证明了安全、且以它为终点的判定都是 SAFE 或报出了（SPEC-0034 修订：引擎每条路径都有判定）
    regress = []
    if os.path.exists(baseline):
        base = json.load(open(baseline, encoding='utf-8'))
        if base.get('options') != result['options']:
            print('注意：选项与基线不同（基线 %s）' % json.dumps(base.get('options'), ensure_ascii=False))
        have = {(r['case'], r['site'], r['kind']) for r in emitted}
        for r in base.get('emitted', []):
            k = (r['case'], r['site'], r['kind'])
            access = r.get('access', r['site'].rsplit(':', 1)[0])
            moved = (r['case'], r['site'].rsplit(':', 1)[0]) in reported_accesses or \
                (r['case'], access) in reported_accesses or (r['case'], r['fn'], access) in safe_accesses or \
                ((r['case'], access) in safe_anywhere and (r['case'], access) not in silent_accesses)
            if r['case'] in ran and k not in have and k not in safe_sites and not moved:
                regress.append(r)
        old = {(e['case'], e['kind'], e['entry']): e for e in base.get('entries', [])}
        for e in entries:
            o = old.get((e['case'], e['kind'], e['entry']))
            if o and (o['actual'], o['verdict']) != (e['actual'], e['verdict']):
                print('变化：%s %s %s  %s/%s → %s/%s' % (e['case'], e['kind'], e['entry'], o['actual'], o['verdict'],
                                                     e['actual'], e['verdict']))
        for r in regress:
            print('✗ 回退：%s %s %s %s fn=%s 不再报出，同一处也没有 SAFE' % (r['case'], r['site'], r['kind'], _tag(r), r['fn']))
        print('基线 %s：回退 %d 条' % (_show(baseline), len(regress)))
    else:
        print('基线 %s 不存在，不比（用 --save 生成）' % _show(baseline))

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'csa-result.json'), 'w', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=1)
        f.write('\n')
    if a.save:
        if errors:
            sys.exit('有运行错误，不写基线')
        with open(baseline, 'w', encoding='utf-8') as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
            f.write('\n')
        print('已写基线 %s' % _show(baseline))
        return 0
    return 1 if errors or regress else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['index', 'check', 'infer', 'doc', 'csa'])
    ap.add_argument('--cc', default='clang')
    ap.add_argument('--infer')
    ap.add_argument('--only')
    ap.add_argument('--out', help='infer：默认 ./infer-out-cases；csa：默认 <仓库>/cmake-build-release/oobsum-csa')
    ap.add_argument('--clang', help='csa：默认 <仓库>/cmake-build-release/bin/clang')
    ap.add_argument('--triple', default='armebv7-linux-gnueabi', help='csa：公司 ILP32 + 大端的本地近似')
    ap.add_argument('--cases', help='csa：用例根目录（默认本目录）')
    ap.add_argument('--baseline', help='csa：默认 <用例根目录>/csa-baseline.json')
    ap.add_argument('--save', action='store_true', help='csa：把这次结果写成新基线（有意改变时）')
    ap.add_argument('--config', action='append', default=[], metavar='K=V', help='csa：追加 checker 选项')
    ap.add_argument('--cc1-arg', action='append', default=[], metavar='ARG', help='csa：追加 cc1 参数（写成 --cc1-arg=-DX）')
    a = ap.parse_args()
    if a.mode == 'index':
        cmd_index(a)
    elif a.mode == 'doc':
        cmd_doc(a)
    elif a.mode == 'check':
        sys.exit(cmd_check(a))
    elif a.mode == 'csa':
        sys.exit(cmd_csa(a))
    else:
        if not a.infer:
            sys.exit('infer 模式需要 --infer')
        cmd_infer(a)


if __name__ == '__main__':
    main()
