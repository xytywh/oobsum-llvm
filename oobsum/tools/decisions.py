#!/usr/bin/env python3
"""汇总 OobSummary 的判定日志（SPEC-0001 §5）：<WorkDir>/decisions/*.jsonl。

    python3 decisions.py <WorkDir>                                  # 按标签 / 原因 / 去向计数 + 报了的条目
    python3 decisions.py <WorkDir> --label suspect --reason entry_unproven
    python3 decisions.py <WorkDir> --all                            # 连没报的也列出来（带 not_emitted_because）
    python3 decisions.py <WorkDir> --jsonl out.jsonl --emitted       # 导出给大模型复核（只要报了的）

只依赖 Python 3 标准库。
"""
import argparse
import collections
import glob
import json
import os
import sys


def load(workdir):
    files = sorted(glob.glob(os.path.join(workdir, 'decisions', '*.jsonl')))
    if not files:
        sys.exit('%s/decisions/ 下没有判定日志（WorkDir 设对了吗？）' % workdir)
    tus, rows = [], []
    for f in files:
        with open(f, encoding='utf-8') as fp:
            for i, line in enumerate(fp):
                line = line.strip()
                if not line:
                    continue
                d = json.loads(line)
                if d.get('schema') == 'oobsum-decisions/1':
                    tus.append(d)
                elif d.get('schema') == 'oobsum-decision/1':
                    d['_tu'] = os.path.basename(f)[:-6]
                    rows.append(d)
    return tus, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('workdir')
    ap.add_argument('--label')
    ap.add_argument('--reason')
    ap.add_argument('--emitted', action='store_true', help='只要报了的')
    ap.add_argument('--all', action='store_true', help='列出所有条目（默认只列报了的）')
    ap.add_argument('--jsonl', help='把筛选后的条目写成 JSONL')
    a = ap.parse_args()
    tus, rows = load(a.workdir)
    sel = [r for r in rows if (not a.label or r['label'] == a.label) and (not a.reason or r.get('reason') == a.reason)
           and (not a.emitted or r['emitted'])]
    print('== %d 个 TU，%d 条判定（筛选后 %d 条）' % (len(tus), len(rows), len(sel)))
    by = collections.Counter((r['label'], r.get('reason') or '-', 'emitted' if r['emitted'] else r.get('not_emitted_because', '?'))
                             for r in sel)
    print('%-10s %-28s %-16s %s' % ('label', 'reason', 'disposition', 'count'))
    for (lab, rea, dis), n in sorted(by.items(), key=lambda x: (-x[1], x[0])):
        print('%-10s %-28s %-16s %d' % (lab, rea, dis, n))
    show = sel if a.all else [r for r in sel if r['emitted']]
    if show:
        print('\n== %s' % ('全部条目' if a.all else '报了的条目'))
        for r in sorted(show, key=lambda r: (r['site'], r['fp'])):
            tag = r['label'].upper() + (':' + r['reason'] if r.get('reason') else '')
            extra = '' if r['emitted'] else '  (未报：%s)' % r.get('not_emitted_because')
            print('  %-40s [%s] depth=%s fn=%s fp=%s%s' % (r['site'], tag, r.get('depth'), r.get('fn'), r.get('fp'), extra))
    if a.jsonl:
        with open(a.jsonl, 'w', encoding='utf-8') as fp:
            for r in sel:
                fp.write(json.dumps({k: v for k, v in r.items() if not k.startswith('_')}, ensure_ascii=False) + '\n')
        print('\n== 写出 %d 条到 %s' % (len(sel), a.jsonl))


if __name__ == '__main__':
    main()
