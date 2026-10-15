#!/usr/bin/env python3
"""引擎对照（oobsum/specs/SPEC-0015 §2.4）：用本仓库的 clang 跑用例源码，取 Dump=summary 产出的摘要（函数内解释算出的局部事实），
交给 replay.py 的 derive（参考实现）从局部事实推到底，最终告警必须与 cases/summaries/alerts.json 逐条相同（消息、调用链、指纹），
并打印对 @expect 的结论。这是 06- T-06 在真实引擎输出上的检查。

规范化（引擎与手写金标准的写法差异，不影响语义）：
  键   F@<文件 id>:名 → F@<定义文件名>:名（金标准用文件名；文件 id 依赖本机的绝对路径，不进金标准）
  类型 u8 / u16 / u32 / i8 / i32 → UINT8 / UINT16 / UINT32 / CHAR / INT32（replay.py 的类型表认这些名字）

用法：
  python3 oobsum/tools/engine_check.py [--clang 路径] [-v] 用例编号…        # 例：LV1-01 LV1-02 LV1-07
退出码：所有用例的告警都与 alerts.json 相同为 0。只依赖 Python 3 标准库。
"""
import sys
sys.dont_write_bytecode = True  # 不在 cases/summaries 里留 __pycache__
import argparse, importlib.util, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CASES = os.path.join(REPO, 'oobsum', 'cases')
SUMMARIES = os.path.join(CASES, 'summaries')
CHECKER = 'rb.enhance.ipa.OobSummary'
TYPES = {'u8': 'UINT8', 'u16': 'UINT16', 'u32': 'UINT32', 'i8': 'CHAR', 'i32': 'INT32'}

spec = importlib.util.spec_from_file_location('replay', os.path.join(SUMMARIES, 'replay.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def run_engine(clang, files, triple):
    """每个源文件跑一次 cc1（Dump=summary+keys），返回 (摘要列表, 文件 id → 文件名)。"""
    res = subprocess.run([clang, '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    sums, names = [], {}
    for f in files:
        args = [clang, '-cc1', '-triple', triple, '-analyze', '-resource-dir', res, '-internal-isystem',
                os.path.join(res, 'include'), '-analyzer-checker=' + CHECKER,
                '-analyzer-config', CHECKER + ':Dump=summary+keys']
        if triple.startswith(('arm', 'thumb', 'aarch64')):
            args.append('-fno-signed-char')
        r = subprocess.run(args + [f], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit('cc1 失败：%s\n%s' % (f, r.stderr[-2000:]))
        for line in r.stderr.splitlines():
            if line.startswith('[oobsum:keys] file '):
                _, _, fid, path = line.split(' ', 3)
                names[fid] = os.path.basename(path)
            elif line.startswith('[oobsum:summary] '):
                sums.append(json.loads(line[len('[oobsum:summary] '):]))
    return sums, names


def normalize(s, names):
    def key(k):
        m = re.match(r'^F@([0-9a-f]{16}):(.*)$', k)
        return 'F@%s:%s' % (names.get(m.group(1), m.group(1)), m.group(2)) if m else k

    def ty(t):
        star = t.endswith('*')
        base = t.rstrip('*')
        return TYPES.get(base, base) + ('*' if star else '') if base in TYPES else t

    s = json.loads(json.dumps(s))
    s['key'] = key(s['key'])
    for p in s['params']:
        p['type'] = ty(p['type'])
    for d in (s.get('fields') or {}).values():
        if 'type' in d:
            d['type'] = ty(d['type'])
    for r in s['reqs']:
        r['access']['key'] = key(r['access']['key'])
    return s


def check(cid, clang, triple, verbose):
    meta = json.load(open(os.path.join(SUMMARIES, cid, 'case.json'), encoding='utf-8'))
    # 跨 TU 用例是一个目录（case.json 只列带 @id 的那个文件）：跑目录里所有的 .c
    files = []
    for f in meta['files']:
        path = os.path.join(CASES, f)
        parent = os.path.dirname(path)
        if not re.match(r'^lv\d+$', os.path.basename(parent)):
            files += [os.path.join(parent, n) for n in sorted(os.listdir(parent)) if n.endswith('.c')]
        else:
            files.append(path)
    files = sorted(set(files))
    sums, names = run_engine(clang, files, triple)
    tmp = tempfile.mkdtemp(prefix='oobsum-engine-')
    try:
        os.makedirs(os.path.join(tmp, cid))
        seen = set()
        for s in sums:
            if s['fn'] in seen:
                continue  # 头文件里的 static inline 各 TU 一份，取第一份（D-08）
            seen.add(s['fn'])
            json.dump(normalize(s, names), open(os.path.join(tmp, cid, s['fn'] + '.json'), 'w', encoding='utf-8'))
        old = replay.SUMS
        replay.SUMS = tmp
        try:
            alerts, probs, verdicts, stat = replay.derive(cid)
        finally:
            replay.SUMS = old
    finally:
        shutil.rmtree(tmp)
    golden = [a for a in json.load(open(os.path.join(SUMMARIES, 'alerts.json'), encoding='utf-8')) if a.get('case') == cid]
    got = sorted(json.dumps(a, sort_keys=True, ensure_ascii=False) for a in alerts)
    want = sorted(json.dumps(a, sort_keys=True, ensure_ascii=False) for a in golden)
    same = got == want
    print('%-8s %s  引擎函数 %d 个；推出上传 %d、判定 %d、入口账本 %d；告警 %d 条 / 金标准 %d 条' % (
        cid, '一致' if same else '不一致', len(seen), stat['up'], stat['rep'], stat['entry'], len(alerts), len(golden)))
    for e, g, ok in verdicts:
        print('    期望 %s %s pa=%s → %s %s' % (e['kind'], e['entry'], e['pa'], g, '✓' if ok else '✗'))
    if not same or verbose:
        for a in got:
            if a not in want:
                print('    引擎多出：' + a)
        for a in want:
            if a not in got:
                print('    金标准有、引擎没有：' + a)
    if verbose:
        for p in probs:
            print('    （推到底的说明，摘要只含局部事实时可忽略）' + p.replace('\n', ' '))
    return same


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cases', nargs='+')
    ap.add_argument('--clang', default=os.path.join(REPO, 'cmake-build-release', 'bin', 'clang'))
    ap.add_argument('--triple', default='armebv7-linux-gnueabi')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    ok = [check(cid, a.clang, a.triple, a.v) for cid in a.cases]
    print('合计 %d 个用例，告警一致 %d 个' % (len(ok), sum(ok)))
    sys.exit(0 if all(ok) else 1)


if __name__ == '__main__':
    main()
