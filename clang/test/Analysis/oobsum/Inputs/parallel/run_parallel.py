# 模拟 clang_check（SPEC-0118）：N 个 cc1 进程同时分析同一个文件、写同一个 WorkDir（cdb 里同一文件出现多次时就是这样），
# 跑完检查：退出码、每个目录里的文件都能解析、没有留下 .tmp。
#   python3 run_parallel.py <clang> <WorkDir> <源文件> <进程数>
import glob, json, os, subprocess, sys

clang, wd, src, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
cmd = [clang, '-cc1', '-analyze', '-analyzer-checker=rb.enhance.ipa.OobSummary',
       '-analyzer-config', 'rb.enhance.ipa.OobSummary:WorkDir=' + wd,
       '-analyzer-config', 'rb.enhance.ipa.OobSummary:Dump=summary', src]
procs = [subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for _ in range(n)]
codes = [p.wait() for p in procs]
print('%d 个进程退出码%s' % (n, '都是 0' if not any(codes) else '：%s' % codes))
parts = []
for sub, pat in [('decisions', '*.jsonl'), ('fir', '*.json'), ('dump', '**/*.json')]:
    files = glob.glob(os.path.join(wd, sub, pat), recursive=True)
    ok = 0
    for f in files:
        try:
            text = open(f, encoding='utf-8').read()
            if f.endswith('.jsonl'):
                [json.loads(l) for l in text.splitlines() if l.strip()]
            else:
                json.loads(text)
            ok += 1
        except ValueError:
            pass
    parts.append('%s %s' % (sub, '全部可解析' if files and ok == len(files) else '有坏文件（%d / %d）' % (ok, len(files))))
tmps = glob.glob(os.path.join(wd, '**', '*.tmp'), recursive=True)
print('；'.join(parts) + '；.tmp %d 个' % len(tmps))
