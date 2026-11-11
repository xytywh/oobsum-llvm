#!/usr/bin/env python3
"""比较OobSummary两轮运行，定位告警差异先出现在哪个阶段（SPEC-0134）。

背景/谁用：维护者在公司重复export/solve/report后，不能仅凭最终报告条数判断规则随机。
比较什么：fir、solved、decisions按相对文件名及流式SHA256；报告按本规则problems多重集合，
忽略数组顺序，保留重复次数，分别比较完整记录和去掉md5的记录。后者仍保留正文/位置/trace等。
如何用：
  python3 compare_runs.py RUN_A RUN_B --through export
  python3 compare_runs.py RUN_A RUN_B --report-left A.json --report-right B.json --json
  python3 compare_runs.py RUN_A RUN_B --details-json LOCAL_DETAILS.json
默认只输出计数，可带回；details文件有本地路径/记录索引，留公司，不含报告正文但也不带回。
退出码：0完整且一致；1发现差异；2缺数据/读入错误/冲突或未完成输入。
边界：只定位差异层，不证明版本/源码/模型/选项相同，不计算漏洞召回率；旧数据已覆盖无法反推。
大报告需解析，大FIR只读字节计算摘要；逐阶段在stderr提示进度，不打印业务名字。仅标准库。
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

RULES = ('rb.enhance.core.OobSummary', 'rb.enhance.ipa.OobSummary')
STAGES = ('export', 'solve', 'report')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def files(root, folder, suffix):
    directory = root / folder
    if not directory.is_dir():
        raise ValueError('required stage directory is missing')
    found = {p.name: digest(p) for p in directory.glob('*' + suffix) if p.is_file()}
    if not found:
        raise ValueError('required stage has no files')
    if folder == 'fir':
        # SPEC-0139：代表虽不变，未选输入集合也可能变化；比较时不能隐去这些版本。
        variants = root / 'fir-variants'
        for p in variants.glob('*/*.json'):
            if p.is_file():
                found['fir-variants/' + p.relative_to(variants).as_posix()] = digest(p)
    return found


def difference(left, right):
    a, b = set(left), set(right)
    return {'left_files': len(a), 'right_files': len(b), 'added': len(b - a), 'removed': len(a - b),
            'changed': sum(left[k] != right[k] for k in a & b)}


def changed(info):
    return any(info[k] for k in ('added', 'removed', 'changed'))


def guard(root, through):
    conflict = (root / 'fir-conflicts').exists() or (root / 'fir-conflicts').is_symlink()
    pending = (root / 'inputs-pending').exists() or (root / 'inputs-pending').is_symlink()
    temporary = sum(1 for _ in (root / 'fir').glob('*.tmp'))
    temporary += sum(1 for _ in (root / 'fir-variants').glob('*/*.tmp'))
    return {'conflict': conflict, 'pending': pending, 'temporary': temporary,
            'invalid': conflict or temporary > 0 or (pending and through != 'export')}


def report_rows(path, details=False):
    with path.open(encoding='utf-8') as stream:
        data = json.load(stream)
    if not isinstance(data, dict) or not isinstance(data.get('problems'), list):
        raise ValueError('report needs a problems array')
    exact, semantic, labels, positions = Counter(), Counter(), Counter(), {}
    selected = 0
    for i, row in enumerate(data['problems']):
        if not isinstance(row, dict) or not any(row.get('description') == rule or '[' + rule + ']' in str(row.get('reason', ''))
                                                for rule in RULES):
            continue
        selected += 1
        text = json.dumps(row, sort_keys=True, ensure_ascii=False, separators=(',', ':'))
        h = hashlib.sha256(text.encode()).hexdigest()
        exact[h] += 1
        reduced = {k: v for k, v in row.items() if k != 'md5'}
        body = json.dumps(reduced, sort_keys=True, ensure_ascii=False, separators=(',', ':'))
        semantic[hashlib.sha256(body.encode()).hexdigest()] += 1
        label = re.search(r'\[OOB-(DEFINITE|POSSIBLE|SUSPECT|SAFE)(?=\]|:)', str(row.get('reason', '')))
        labels[label.group(1) if label else 'UNKNOWN'] += 1
        if details:
            positions.setdefault(h, []).append({'index': i, 'file': row.get('file'), 'line': row.get('line'), 'md5': row.get('md5')})
    return {'exact': exact, 'semantic': semantic, 'selected': selected, 'total': len(data['problems']),
            'labels': dict(sorted(labels.items())), 'positions': positions}


def count_delta(left, right):
    return {'removed': sum((left - right).values()), 'added': sum((right - left).values())}


def compare(a):
    left, right = Path(a.left), Path(a.right)
    result = {'schema': 'oobsum-comparison/1', 'through': a.through, 'stages': {}, 'first_change': None,
              'guards': {'left': guard(left, a.through), 'right': guard(right, a.through)}, 'errors': []}
    details = {'files': {}, 'report': {}}
    directories = [('fir', '.json')]
    if a.through != 'export':
        directories.append(('solved', '.json'))
    if a.through == 'report':
        directories.append(('decisions', '.jsonl'))
    for folder, suffix in directories:
        print('[oobsum:compare] stage=' + folder, file=sys.stderr, flush=True)
        try:
            x, y = files(left, folder, suffix), files(right, folder, suffix)
            info = difference(x, y)
            result['stages'][folder] = info
            if changed(info) and result['first_change'] is None:
                result['first_change'] = folder
            if a.details_json:
                details['files'][folder] = {'added': sorted(set(y) - set(x)), 'removed': sorted(set(x) - set(y)),
                                            'changed': sorted(k for k in set(x) & set(y) if x[k] != y[k])}
        except (OSError, ValueError) as error:
            result['errors'].append({'stage': folder, 'kind': type(error).__name__})
    if a.through == 'report':
        print('[oobsum:compare] stage=report', file=sys.stderr, flush=True)
        try:
            x = report_rows(Path(a.report_left) if a.report_left else left / 'report.json', bool(a.details_json))
            y = report_rows(Path(a.report_right) if a.report_right else right / 'report.json', bool(a.details_json))
            info = {'left_selected': x['selected'], 'right_selected': y['selected'],
                    'left_total': x['total'], 'right_total': y['total'],
                    'left_labels': x['labels'], 'right_labels': y['labels'],
                    'exact_records': count_delta(x['exact'], y['exact']),
                    'without_md5': count_delta(x['semantic'], y['semantic'])}
            info['same_warning_records'] = x['exact'] == y['exact']
            info['same_without_md5'] = x['semantic'] == y['semantic']
            info['only_md5_differs'] = x['exact'] != y['exact'] and x['semantic'] == y['semantic']
            result['stages']['report'] = info
            if not info['same_warning_records'] and result['first_change'] is None:
                result['first_change'] = 'report'
            if a.details_json:
                for name, data, other in [('removed', x, y), ('added', y, x)]:
                    details['report'][name] = [{'occurrences': count, 'records': data['positions'].get(h, [])[:count]}
                                              for h, count in (data['exact'] - other['exact']).items()]
        except (OSError, ValueError) as error:
            result['errors'].append({'stage': 'report', 'kind': type(error).__name__})
    invalid = result['errors'] or any(g['invalid'] for g in result['guards'].values())
    result['complete'] = not bool(invalid)
    result['equal'] = result['complete'] and result['first_change'] is None
    if a.details_json:
        Path(a.details_json).write_text(json.dumps(details, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    return result, 2 if invalid else 0 if result['equal'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('left')
    parser.add_argument('right')
    parser.add_argument('--through', choices=STAGES, default='report')
    parser.add_argument('--report-left')
    parser.add_argument('--report-right')
    parser.add_argument('--details-json', help='仅公司本地的详细索引，不带回')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        result, code = compare(args)
    except OSError as error:
        result, code = {'complete': False, 'error': type(error).__name__}, 2
    if args.json:
        print(json.dumps(result, indent=1, ensure_ascii=False))
    else:
        print('完整=%s 一致=%s 首个变化阶段=%s' % (result.get('complete'), result.get('equal'), result.get('first_change')))
        for name, info in result.get('stages', {}).items():
            print(name + ': ' + json.dumps(info, ensure_ascii=False, sort_keys=True))
        if result.get('errors'):
            print('无法比较阶段：' + json.dumps(result['errors'], ensure_ascii=False))
        print('输入保护：' + json.dumps(result.get('guards', {}), ensure_ascii=False))
    return code


if __name__ == '__main__':
    sys.exit(main())
