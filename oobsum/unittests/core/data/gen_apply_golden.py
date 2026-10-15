#!/usr/bin/env python3
"""core/Apply 的金标准（oobsum/specs/SPEC-0012 §5）：replay.py 在去掉 doc 的机器版摘要上逐调用点核对的输出
（replay.py --dir <机器版> <用例> -v 里从第一个调用点到空行之间的行，即 Dump=apply 的格式），写成 apply-golden.txt；
C++ 的 ApplyTest 用 CheckCase 逐行比对。标题（来自 case.json）与 @expect 的对照不在其中。

用法：python3 -I gen_apply_golden.py
只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True  # 不在 cases/summaries 里留 __pycache__
import importlib.util, json, os, shutil, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARIES = os.path.join(HERE, '..', '..', '..', 'cases', 'summaries')
spec = importlib.util.spec_from_file_location('replay', os.path.join(SUMMARIES, 'replay.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def main():
    tmp = tempfile.mkdtemp(prefix='oobsum-mach-')
    try:
        for cid in replay.all_cases():
            os.makedirs(os.path.join(tmp, cid))
            src = os.path.join(SUMMARIES, cid)
            for f in sorted(os.listdir(src)):
                if f.endswith('.json') and f != 'case.json':
                    s = replay.strip_doc(json.load(open(os.path.join(src, f), encoding='utf-8')))
                    json.dump(s, open(os.path.join(tmp, cid, f), 'w', encoding='utf-8'), ensure_ascii=False)
        replay.SUMS = tmp
        out = ['# 由 gen_apply_golden.py 从 replay.py（机器版摘要）的逐调用点核对生成，不要手改']
        total = 0
        for cid in replay.all_cases():
            lines, errors = replay.replay(cid)[:2]
            if errors:
                raise SystemExit('%s 回放有不一致：%s' % (cid, errors))
            body = lines[1:lines.index('')]
            out.append('## ' + cid)
            out.extend(body)
            total += len(body)
        open(os.path.join(HERE, 'apply-golden.txt'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
        print('apply-golden.txt：%d 个用例、%d 行' % (len(replay.all_cases()), total))
    finally:
        shutil.rmtree(tmp)


if __name__ == '__main__':
    main()
