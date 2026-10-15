#!/usr/bin/env python3
"""LinExpr 的金标准（oobsum/specs/SPEC-0010 §5）：用 replay.py（参考实现）把 cases/summaries 里 104 份摘要（去掉 doc）中的
所有表达式与条件解析、按规范形式打印，写成 lin-golden.txt，C++ 的 LinExprTest 逐行比对。

用法：
  python3 -I gen_lin_golden.py            # 重新生成 lin-golden.txt（摘要有意改动时）
  python3 -I gen_lin_golden.py --check S  # 打印 replay.py 对一条表达式 / 条件的结果
格式（每行一条，制表符分隔）：
  E <输入> <规范打印>
  C <输入> <各 L<=0 原子的 ShowLe0，以 " ; " 连接>
只依赖 Python 3 标准库。
"""
import glob, importlib.util, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARIES = os.path.join(HERE, '..', '..', '..', 'cases', 'summaries')
spec = importlib.util.spec_from_file_location('replay', os.path.join(SUMMARIES, 'replay.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

CMP = re.compile(r'<=|>=|==|<|(?<!-)>')
LOOKS = re.compile(r'[0-9+*()]|^-|(?<!^)-(?!>)|^p\d|ext\(')


def one(s):
    if CMP.search(s):
        atoms = replay.C(s)
        return 'C', ' ; '.join(replay.show_atom_le0(a) for a in atoms)
    return 'E', replay.show(replay.P(s))


def strings(o):
    if isinstance(o, dict):
        for v in o.values():
            yield from strings(v)
    elif isinstance(o, list):
        for v in o:
            yield from strings(v)
    elif isinstance(o, str):
        yield o


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--check':
        print('%s\t%s' % one(sys.argv[2]))
        return
    rows = set()
    for path in sorted(glob.glob(os.path.join(SUMMARIES, '*', '*.json'))):
        if os.path.basename(path) == 'case.json':
            continue
        for s in strings(replay.strip_doc(json.load(open(path, encoding='utf-8')))):
            if not LOOKS.search(s) or '@' in s or s.startswith('*') or '\t' in s:
                continue
            try:
                kind, out = one(s)
            except Exception:
                continue  # 不是表达式（对象名、位置……）
            rows.add((kind, s, out))
    with open(os.path.join(HERE, 'lin-golden.txt'), 'w', encoding='utf-8') as f:
        f.write('# 由 gen_lin_golden.py 从 cases/summaries 生成（replay.py 的规范打印），不要手改\n')
        for kind, s, out in sorted(rows):
            f.write('%s\t%s\t%s\n' % (kind, s, out))
    print('lin-golden.txt：%d 条（E %d，C %d）' % (len(rows), sum(1 for r in rows if r[0] == 'E'),
                                                sum(1 for r in rows if r[0] == 'C')))


if __name__ == '__main__':
    main()
