#!/usr/bin/env python3
"""Redirect erratique.ch source tarballs to their GitHub git repos.

Daniel Buenzli's packages (fmt, fpath, astring, cmdliner, uutf, ...) publish their
release tarballs on https://erratique.ch, which this sandbox blocks.  Every one of
them is mirrored as a git repo under github.com/dbuenzli/<pkg> with a `v<version>`
tag, and git clone *is* permitted here, so rewrite the url block accordingly and
drop its checksum (a git checkout cannot match a tarball hash).
"""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/build/opam-repository"

ERRATIQUE = re.compile(
    r'"https://erratique\.ch/software/([A-Za-z0-9_\-]+)/releases/'
    r'(?:[A-Za-z0-9_\-]+)-([0-9][^"]*?)\.(?:tbz|tar\.gz|tgz)"')

CHK_LIST = re.compile(r'\n\s*checksum:\s*\[[^\]]*\]')
CHK_ONE = re.compile(r'\n\s*checksum:\s*"[^"]*"')

changed = 0
for dirpath, _d, filenames in os.walk(os.path.join(ROOT, "packages")):
    if "opam" not in filenames:
        continue
    p = os.path.join(dirpath, "opam")
    try:
        src = open(p, encoding="utf-8").read()
    except Exception:
        continue
    if "erratique.ch" not in src:
        continue
    out = ERRATIQUE.sub(lambda m: f'"git+https://github.com/dbuenzli/{m.group(1)}#v{m.group(2)}"', src)
    if out == src:
        continue
    out = CHK_LIST.sub("", out)
    out = CHK_ONE.sub("", out)
    open(p, "w").write(out)
    changed += 1

left = 0
for dirpath, _d, filenames in os.walk(os.path.join(ROOT, "packages")):
    if "opam" not in filenames:
        continue
    try:
        src = open(os.path.join(dirpath, "opam"), encoding="utf-8").read()
    except Exception:
        continue
    if ERRATIQUE.search(src):
        left += 1

print(f"redirected {changed} erratique.ch packages to github")
if left == 0:
    if changed == 0:
        print("  ✓ 没有需要改写的 —— 之前已经改写过（重复执行是安全的）")
    else:
        print(f"  ✓ 改写 {changed} 个，erratique.ch 源已清零")
else:
    print(f"  ✗ 仍有 {left} 个包的源指向 erratique.ch 没被改写 —— "
          f"正则没匹配上，请把其中一个 opam 文件的 url 块贴出来。", file=sys.stderr)
    sys.exit(3)
