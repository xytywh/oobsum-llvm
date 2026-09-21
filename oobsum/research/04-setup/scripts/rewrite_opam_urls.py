#!/usr/bin/env python3
"""Make opam-repository usable under this sandbox's egress policy.

Permitted here: `git clone/fetch` against github.com, plus
https://github.com/<o>/<r>/releases/download/...  Everything else answers 403
(codeload, /archive/ tarballs, raw.githubusercontent, opam.ocaml.org, apt, ftp).

Two independent rewrites, applied block by block so they do not interfere:

  1. `url { src: <github archive tarball> }`  ->  `git+https://github.com/o/r#tag`
     The checksum of that *url block only* is dropped, because a git checkout
     cannot reproduce a tarball hash.

  2. `extra-source "x" { src: <raw.githubusercontent/o/r/sha/path> }` -> file://
     Handled by resolve_raw_sources.py; checksums are KEPT there, because the
     file materialised out of git at that exact sha is byte-identical, and opam
     insists on a checksum for a file:// extra-source.
"""
import os
import re
import sys

ROOT = sys.argv[1]

ARCHIVE_PATTERNS = [
    (re.compile(r'"https://github\.com/([^/"]+)/([^/"]+)/archive/refs/tags/([^"]+?)\.(?:tar\.gz|tar\.bz2|tgz|zip)"'),
     r'"git+https://github.com/\1/\2#\3"'),
    (re.compile(r'"https://github\.com/([^/"]+)/([^/"]+)/archive/([^"/]+?)\.(?:tar\.gz|tar\.bz2|tgz|zip)"'),
     r'"git+https://github.com/\1/\2#\3"'),
    (re.compile(r'"https://codeload\.github\.com/([^/"]+)/([^/"]+)/tar\.gz/refs/tags/([^"]+)"'),
     r'"git+https://github.com/\1/\2#\3"'),
    (re.compile(r'"https://codeload\.github\.com/([^/"]+)/([^/"]+)/tar\.gz/([^"]+)"'),
     r'"git+https://github.com/\1/\2#\3"'),
]

CHK_LIST = re.compile(r'\n\s*checksum:\s*\[[^\]]*\]')
CHK_ONE = re.compile(r'\n\s*checksum:\s*"[^"]*"')


def find_url_block(text):
    """Return (start, end) of the top-level `url { ... }` block, or None."""
    m = re.search(r'^url\s*\{', text, re.M)
    if not m:
        return None
    depth = 0
    i = m.end() - 1
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return (m.start(), i + 1)
        i += 1
    return None


import time

def host_histogram(root):
    """跑之前先看看仓库里的源到底长什么样——0 改写时这张表就是诊断依据。"""
    import collections
    c = collections.Counter()
    url_rx = re.compile(r'src:\s*"([^"]+)"')
    n = 0
    for dirpath, _d, filenames in os.walk(os.path.join(root, "packages")):
        if "opam" not in filenames:
            continue
        try:
            src = open(os.path.join(dirpath, "opam"), encoding="utf-8").read()
        except Exception:
            continue
        n += 1
        # 只看顶层 url { } 块——这正是本脚本会改写的范围。
        # （extra-source 块里的 src 由 populate_opam_cache.py 负责，不能混在一起统计，
        #   否则会出现"改写完了统计还剩几十个"的假警报。）
        span = find_url_block(src)
        if not span:
            continue
        src = src[span[0]:span[1]]
        for m in url_rx.finditer(src):
            u = m.group(1)
            host = u.split("/")[2] if "://" in u else "?"
            if u.startswith("git+"):
                c["git+ (已经是 git 源)"] += 1
            elif host == "github.com" and "/archive/" in u:
                c["github /archive/ 压缩包 ← 本脚本要改写的"] += 1
            elif host == "codeload.github.com":
                c["codeload.github.com ← 本脚本要改写的"] += 1
            elif host == "github.com" and "/releases/download/" in u:
                c["github releases/download"] += 1
            elif "erratique.ch" in host:
                c["erratique.ch ← 由 rewrite_erratique.py 改写"] += 1
            else:
                c[f"其它: {host}"] += 1
    return n, c


scanned_all, hist = host_histogram(ROOT)
target = sum(v for k, v in hist.items() if "本脚本要改写的" in k)
print(f"  仓库共 {scanned_all} 个 opam 文件；源的分布（前 12 类）：")
for k, v in hist.most_common(12):
    print(f"    {v:6d}  {k}")
print(f"  本脚本的目标：{target} 个压缩包源")

changed = scanned = 0
t0 = time.time()
files = []
for dirpath, _dirnames, filenames in os.walk(os.path.join(ROOT, "packages")):
    if "opam" in filenames:
        files.append(os.path.join(dirpath, "opam"))
total = len(files)
for i, p in enumerate(files, 1):
    scanned += 1
    if i % 2000 == 0 or i == total:
        el = time.time() - t0
        rate = i / el if el > 0 else 0
        eta = (total - i) / rate if rate > 0 else 0
        print(f"    进度 {i}/{total} ({i*100//total}%)  已改写 {changed}  "
              f"用时 {el:.0f}s  预计还需 {eta:.0f}s", flush=True)
    try:
        src = open(p, encoding="utf-8").read()
    except Exception:
        continue
    span = find_url_block(src)
    if not span:
        continue
    head, block, tail = src[:span[0]], src[span[0]:span[1]], src[span[1]:]
    new_block = block
    for rx, rep in ARCHIVE_PATTERNS:
        new_block = rx.sub(rep, new_block)
    if new_block == block:
        continue
    new_block = CHK_LIST.sub("", new_block)
    new_block = CHK_ONE.sub("", new_block)
    open(p, "w").write(head + new_block + tail)
    changed += 1

print(f"rewrote url blocks in {changed} / {scanned} opam files under {ROOT}")

# ---- 结果自检：0 改写不一定是错的（可能是重复执行），但"0 改写且还有一堆压缩包源"一定是错的 ----
_, hist_after = host_histogram(ROOT)
left = sum(v for k, v in hist_after.items() if "本脚本要改写的" in k)
TOLERANCE = 50   # 少量边角形态（.tar.xz、tag 里带斜杠等）改不动是正常的，不算失败

if left <= TOLERANCE:
    if changed == 0:
        print(f"  ✓ 没有需要改写的 —— 之前已经改写过（重复执行是安全的），"
              f"或仓库里本来就都是 git 源。剩余 {left} 个边角形态，可忽略")
    else:
        print(f"  ✓ 改写 {changed} 个；剩余 {left} 个边角形态（≤{TOLERANCE}），可忽略")
elif changed == 0:
    print(f"  ✗ 一个都没改写，但仓库里还有 {left} 个 github 压缩包源 —— "
          f"正则没匹配上。请把上面那张分布表贴出来。", file=sys.stderr)
    sys.exit(3)
else:
    print(f"  ! 改写 {changed} 个，仍剩 {left} 个 github 压缩包源没处理，"
          f"后面装依赖时可能会卡在这些包上")
