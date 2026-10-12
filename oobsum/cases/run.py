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


def units():
    """每个用例单元：(级别, 名字, [源文件], 头部标记文件)"""
    out = []
    for lv in LEVELS:
        d = os.path.join(HERE, lv)
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
    out = os.path.abspath(a.out)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['index', 'check', 'infer', 'doc'])
    ap.add_argument('--cc', default='clang')
    ap.add_argument('--infer')
    ap.add_argument('--only')
    ap.add_argument('--out', default=os.path.join(os.getcwd(), 'infer-out-cases'))
    a = ap.parse_args()
    if a.mode == 'index':
        cmd_index(a)
    elif a.mode == 'doc':
        cmd_doc(a)
    elif a.mode == 'check':
        sys.exit(cmd_check(a))
    else:
        if not a.infer:
            sys.exit('infer 模式需要 --infer')
        cmd_infer(a)


if __name__ == '__main__':
    main()
