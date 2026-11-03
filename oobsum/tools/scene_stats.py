#!/usr/bin/env python3
"""按 Lv1..Lv10 的已观察特征统计 OobSummary 告警（SPEC-0128）。

背景 / Why：真实问题可同时依赖循环、字段和布局；调用层数不等于场景等级。
本工具供规则开发者、业务复核者和领域专家（Who）查看类别分布（What），
在新版本全量 report 后或复核人工分类后使用（When）。只输出标签与计数，
不输出业务源码、函数名、路径或告警正文，适合放进公司结果带回模板。

How：只依赖 Python 3.6+ 标准库，JSONL 按行读，不把全板判断全部装入内存。
clang_check 的 problems JSON 使用标准 JSON 读取器；报告很大时优先使用 WorkDir。
默认只统计 emitted 的判断，并按 report_group 合成物理告警；主类取最高的已观察等级。
--multi 对每个已观察特征分别计数，因此行合计可能超过总告警数。
Lv10 业务契约需要人工研判；旧数据 / 没有证据的记录归 UNCLASSIFIED，不能假装已自动识别。

用法：
  python3 oobsum/tools/scene_stats.py --workdir W
  python3 oobsum/tools/scene_stats.py --workdir W --unit decisions --include-unemitted
  python3 oobsum/tools/scene_stats.py --report /path/allfiles-buffer_overflow_report.json --multi
  python3 oobsum/tools/scene_stats.py --workdir W --annotations /company/scenes.json --json > counts.json

人工标注 JSON：{"判断指纹或warning的group编号": ["LV10"]}，也接受整数列表 [3, 7]。
标注只影响这次统计，不修改判断、源码、告警和报告策略；文件与原始判断留在公司。
"""
import argparse
from collections import Counter
import glob
import json
import os
import re
import sys

LABELS = ('definite', 'possible', 'suspect', 'safe')
NAMES = ['LV%d' % n for n in range(1, 11)] + ['UNCLASSIFIED']
CHECKERS = ('rb.enhance.core.OobSummary', 'rb.enhance.ipa.OobSummary')


def levels(value):
    """只接受明确的等级，不从文件名、函数名或调用深度猜测。"""
    if not isinstance(value, list):
        raise ValueError('场景标注必须是等级列表')
    result = set()
    for item in value:
        match = re.fullmatch(r'(?:LV)?([1-9]|10)', str(item))
        if not match:
            raise ValueError('场景等级必须为 LV1..LV10')
        result.add(int(match.group(1)))
    return result


def work_rows(workdir):
    files = sorted(glob.glob(os.path.join(workdir, 'decisions', '*.jsonl')))
    if not files:
        raise ValueError('WorkDir 中没有 decisions/*.jsonl')
    for path in files:
        with open(path, encoding='utf-8') as stream:
            for line in stream:
                if not line.strip():
                    continue
                row = json.loads(line)
                if row.get('schema') == 'oobsum-decision/1' or 'label' in row:
                    yield row


def report_rows(path):
    if os.path.isdir(path):
        matches = sorted(glob.glob(os.path.join(path, '*report.json')))
        if len(matches) != 1:
            raise ValueError('报告目录必须恰有一个 *report.json，否则请指定文件')
        path = matches[0]
    with open(path, encoding='utf-8') as stream:
        data = json.load(stream)
    if not isinstance(data, dict) or not isinstance(data.get('problems'), list):
        raise ValueError('报告应是包含 problems 数组的对象')
    for problem in data['problems']:
        if not isinstance(problem, dict):
            continue
        text = str(problem.get('reason', ''))
        if not any(problem.get('description') == name or '[%s]' % name in text for name in CHECKERS):
            continue
        label = re.search(r'\[OOB-(DEFINITE|POSSIBLE|SUSPECT|SAFE)(?::[^\]]*)?\]', text)
        scene = set()
        for tag in re.findall(r'\[scene=([^\]]+)\]', text):
            scene.update(int(n) for n in re.findall(r'LV(10|[1-9])\b', tag))
        group = re.search(r'\bgroup=([0-9a-f]{16})\b', text)
        yield {'label': label.group(1).lower() if label else 'unknown', 'emitted': True,
               'scenes': sorted(scene), 'report_group': group.group(1) if group else '',
               'fp': str(problem.get('md5', ''))}


def summarize(rows, unit='warnings', multi=False, include_unemitted=False, annotations=None):
    """只保留每组的计数属性；不同判断合并时，标签取最严重的，场景取并集。"""
    annotations = annotations or {}
    groups = {}
    judgments = 0
    for number, row in enumerate(rows):
        if not include_unemitted and not row.get('emitted'):
            continue
        judgments += 1
        label = str(row.get('label', 'unknown')).lower()
        group = row.get('report_group')
        key = ('group', group) if unit == 'warnings' and group else ('row', number)
        scene = levels(row.get('scenes', []))
        for identifier in (row.get('fp'), group):
            if identifier in annotations:
                scene = levels(annotations[identifier])
        item = groups.setdefault(key, {'label': label, 'scenes': set()})
        rank = lambda name: LABELS.index(name) if name in LABELS else len(LABELS)
        if rank(label) < rank(item['label']):
            item['label'] = label
        item['scenes'].update(scene)
    counts = {name: Counter() for name in NAMES}
    labels_count = Counter()
    for item in groups.values():
        labels_count[item['label']] += 1
        chosen = item['scenes'] if multi else {max(item['scenes'])} if item['scenes'] else set()
        for name in ['LV%d' % n for n in sorted(chosen)] or ['UNCLASSIFIED']:
            counts[name][item['label']] += 1
    return {'schema': 'oobsum-scenes/1', 'method': 'observed_features', 'unit': unit,
            'multi': multi, 'judgments': judgments, 'units': len(groups), 'labels': dict(labels_count),
            'scenes': {name: dict(counts[name]) for name in NAMES}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--workdir', help='判断日志所在 WorkDir')
    source.add_argument('--report', help='clang_check 报告 JSON 或只有一个报告的目录')
    parser.add_argument('--unit', choices=('warnings', 'decisions'), default='warnings')
    parser.add_argument('--multi', action='store_true', help='按每个已观察特征分别计数')
    parser.add_argument('--include-unemitted', action='store_true', help='包括未报判断；无告警组的记录各算一个单位')
    parser.add_argument('--annotations', help='公司侧人工标注 JSON（指纹 / 分组编号到等级列表）')
    parser.add_argument('--json', action='store_true', help='向 stdout 输出纯计数 JSON')
    args = parser.parse_args()
    try:
        annotations = {}
        if args.annotations:
            with open(args.annotations, encoding='utf-8') as stream:
                annotations = json.load(stream)
            if not isinstance(annotations, dict):
                raise ValueError('人工标注必须是对象')
            for value in annotations.values():
                levels(value)
        rows = work_rows(args.workdir) if args.workdir else report_rows(args.report)
        result = summarize(rows, args.unit, args.multi, args.include_unemitted, annotations)
    except (OSError, ValueError, TypeError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True, indent=1))
    else:
        print('场景统计：单位 %s，原判断 %d，统计单位 %d；%s' % (
            result['unit'], result['judgments'], result['units'], '多标签计数' if args.multi else '最高已观察等级为主类'))
        print('%-14s %10s %10s %10s %10s %10s' % ('scene', 'DEFINITE', 'POSSIBLE', 'SUSPECT', 'SAFE', 'total'))
        for name in NAMES:
            values = result['scenes'][name]
            print('%-14s' % name + ''.join('%11d' % values.get(label, 0) for label in LABELS) +
                  '%11d' % sum(values.values()))
        if result['labels'].get('unknown'):
            print('未解析严重程度的记录：%d（已计入场景行合计，JSON labels.unknown 可核查）' % result['labels']['unknown'])
        print('方法：observed_features；业务契约不自动判为 LV10，旧数据 / 缺证据列未分类；不输出业务名字。')


if __name__ == '__main__':
    main()
