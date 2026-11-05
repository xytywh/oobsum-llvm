"""SPEC-0131 / T-06：核验自有member_arg.c的重复访问日志，与展示顺序无关。

背景：static访问函数键含文件路径哈希，换仓库路径会改变指纹及同位置日志排序。
用法：python3 check_log.py member-arg.test.tmp.decisions.log [--self-test]
输入为Dump=decisions完整stderr；只看Entry_Msg的成员/外层两条SUSPECT。
核验报出状态、原因与dup_of引用，不固定指纹值或先后。--self-test用真实记录验证反序仍通过，
丢失记录、重复记录、错误标签/报出状态、损坏引用必须失败。只依赖Python标准库；不参与规则判定。
"""
import argparse
import copy
import json
import re


def verify(rows):
    entries = [r for r in rows if r.get('fn') == 'F:Entry_Msg']
    assert len(entries) == 2, 'Entry_Msg: expected exactly two decisions, got %d' % len(entries)
    by_object = {r.get('obj'): r for r in entries}
    assert set(by_object) == {'p0->aucBody', '*p0'}, 'expected member and parent objects'
    member, parent = by_object['p0->aucBody'], by_object['*p0']
    for row in entries:
        assert row['schema'] == 'oobsum-decision/1'
        assert row['label'] == 'suspect', 'expected SUSPECT for both obligations'
        assert row['kind'] == 'W' and row['depth'] == 1 and row['eff_depth'] == 0
        assert isinstance(row['bound'], str) and row['bound']
        assert re.fullmatch(r'[0-9a-f]{16}', row['fp']), 'expected a fingerprint, not a fixed value'
    assert member['reason'] == 'type_range_only' and member['emitted'] is True
    assert not member.get('dup_of') and not member.get('not_emitted_because')
    assert parent['reason'] == 'entry_unproven' and parent['emitted'] is False
    assert parent['not_emitted_because'] == 'duplicate_access'
    assert parent['dup_of'] == member['fp'], 'parent dup_of must reference the emitted member'
    assert parent['fp'] != member['fp']
    for field in ('site', 'access_key', 'chain'):
        assert member[field] and parent[field] == member[field], 'same access: mismatch in ' + field


def self_test(rows):
    verify(list(reversed(rows)))
    for defect in ('missing', 'extra', 'dup_of', 'label', 'emitted'):
        damaged = copy.deepcopy(rows)
        member = next(r for r in damaged if r.get('obj') == 'p0->aucBody' and r.get('fn') == 'F:Entry_Msg')
        parent = next(r for r in damaged if r.get('obj') == '*p0' and r.get('fn') == 'F:Entry_Msg')
        if defect == 'missing':
            damaged.remove(parent)
        elif defect == 'extra':
            damaged.append(copy.deepcopy(parent))
        elif defect == 'dup_of':
            first = '0' if member['fp'][0] != '0' else '1'
            parent['dup_of'] = first + member['fp'][1:]
        elif defect == 'label':
            member['label'] = 'safe'
        else:
            parent['emitted'] = True
        try:
            verify(damaged)
        except AssertionError:
            continue
        raise AssertionError('validator accepted damaged log: ' + defect)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    prefix = '[oobsum:decisions] '
    with open(args.log, encoding='utf-8') as stream:
        rows = [json.loads(line[len(prefix):]) for line in stream if line.startswith(prefix)]
    verify(rows)
    if args.self_test:
        self_test(rows)
    print('member-arg: two SUSPECT decisions and duplicate link verified (order independent)')


if __name__ == '__main__':
    main()
