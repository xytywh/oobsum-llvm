#!/usr/bin/env python3
"""Pre-seed opam's download cache with files it is not allowed to download.

raw.githubusercontent.com answers 403 here, but `git fetch` of the exact sha the
URL names is allowed.  opam checks `$OPAMROOT/download-cache/<kind>/<xx>/<hash>`
before going to the network, so materialising the file out of git and filing it
under its advertised sha256 makes the fetch succeed with the original opam
metadata untouched (checksums still verified -- the git blob is byte-identical).
"""
import hashlib
import os
import re
import subprocess
import sys

REPO = os.environ.get("REPO", os.path.expanduser("~/infer-build/opam-repository"))
CACHE = os.path.join(os.environ.get("OPAMROOT", os.path.expanduser("~/infer-build/opamroot")), "download-cache")
GITDIRS = os.environ.get("GITDIRS", os.path.expanduser("~/infer-build/gitsrc"))
os.makedirs(GITDIRS, exist_ok=True)

# extra-source "name" { src: "https://raw.githubusercontent.com/o/r/sha/path"
#                       checksum: [ "sha256=..." "md5=..." ] }
BLOCK = re.compile(
    r'"https://(?:raw\.githubusercontent\.com|github\.com)/([^/"]+)/([^/"]+)/'
    r'(?:raw/)?(?:refs/heads/|refs/tags/)?([^/"]+)/([^"]+)"'
    r'(?P<rest>.{0,400}?)(?=\n\s*(?:extra-source|url|depends|build|install|synopsis|\Z))',
    re.S)
SHA256 = re.compile(r'"sha256=([0-9a-f]{64})"')


def sh(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)


def git_blob(owner, repo, ref, path):
    gd = os.path.join(GITDIRS, f"{owner}__{repo}")
    if not os.path.isdir(os.path.join(gd, ".git")):
        os.makedirs(gd, exist_ok=True)
        sh("git init -q .", cwd=gd)
        sh(f"git remote add origin https://github.com/{owner}/{repo}.git", cwd=gd)
    sh(f"git fetch -q --depth 1 origin {ref}", cwd=gd)
    for rev in (ref, f"origin/{ref}", "FETCH_HEAD"):
        r = subprocess.run(f"git show {rev}:{path}", cwd=gd, shell=True, capture_output=True)
        if r.returncode == 0:
            return r.stdout
    return None


def wanted_package_dirs():
    names = {}
    lock = os.environ.get("LOCK", os.path.expanduser("~/infer-build/infer/opam/infer.opam.locked"))
    # lock 文件来自 infer 源码（opam/infer.opam.locked），它决定"该预填哪些包"。
    # 拿不到时不要崩——退化成只预填下面那串编译器相关的包，后面装依赖时
    # 缺的会在网络上重试，真正拿不到再单独处理。
    if os.path.isfile(lock):
        with open(lock) as fh:
            for line in fh:
                m = re.match(r'\s*"([A-Za-z0-9_.+-]+)"\s*\{=\s*"([^"]+)"\}', line)
                if m:
                    names.setdefault(m.group(1), set()).add(m.group(2))
    else:
        print(f"  !! 找不到 lock 文件：{lock}")
        print(f"     （它在 infer 源码里：<infer>/opam/infer.opam.locked）")
        print(f"     退化为只预填编译器相关的包。若后续 30-install-deps.sh 在某个包上")
        print(f"     卡住，先确认 infer 源码已 clone，再重跑本脚本。")
    for n in ["ocaml-config", "ocaml-compiler", "ocaml-base-compiler", "ocaml-variants",
              "ocaml", "ocaml-options-vanilla", "ocaml-option-flambda", "dune", "ocamlfind", "seq"]:
        names.setdefault(n, set())
    out = []
    for name, versions in names.items():
        pdir = os.path.join(REPO, "packages", name)
        if not os.path.isdir(pdir):
            continue
        for sd in os.listdir(pdir):
            if versions and not any(sd.endswith("." + v) for v in versions):
                # still include compiler-ish metadata packages whose version we don't pin
                if name not in ("ocaml-config", "ocaml-compiler", "ocaml-variants",
                                "ocaml-base-compiler", "ocaml", "seq", "ocamlfind"):
                    continue
            out.append(os.path.join(pdir, sd, "opam"))
    return out


import time

stored = missed = 0
_files = [x for x in wanted_package_dirs() if os.path.isfile(x)]
_total = len(_files)
_t0 = time.time()
print(f"  待处理 {_total} 个 opam 文件（每个可能触发一次 git fetch，慢是正常的）")
for _i, p in enumerate(_files, 1):
    if _i % 10 == 0 or _i == _total:
        _el = time.time() - _t0
        _rate = _i / _el if _el > 0 else 0
        _eta = (_total - _i) / _rate if _rate > 0 else 0
        print(f"    进度 {_i}/{_total} ({_i*100//max(_total,1)}%)  已入缓存 {stored}  "
              f"未解析 {missed}  用时 {_el:.0f}s  预计还需 {_eta:.0f}s", flush=True)
    src = open(p, encoding="utf-8", errors="replace").read()
    if "raw.githubusercontent.com" not in src:
        continue
    for m in BLOCK.finditer(src):
        owner, repo, ref, path = m.group(1), m.group(2), m.group(3), m.group(4)
        sha_m = SHA256.search(m.group("rest"))
        blob = git_blob(owner, repo, ref, path)
        if blob is None:
            missed += 1
            print(f"  !! no blob {owner}/{repo}@{ref}:{path}")
            continue
        actual = hashlib.sha256(blob).hexdigest()
        if sha_m and sha_m.group(1) != actual:
            print(f"  !! sha mismatch for {path}: opam says {sha_m.group(1)[:12]}, git gives {actual[:12]}")
            missed += 1
            continue
        d = os.path.join(CACHE, "sha256", actual[:2])
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, actual), "wb") as f:
            f.write(blob)
        stored += 1
print(f"seeded {stored} files into {CACHE} ({missed} unresolved)")
if missed:
    print(f"  · {missed} 个没解析出来——绝大多数是 opam 里已经失效的历史引用，"
          f"只要后面 30-install-deps.sh 不卡在具体某个包上就不用管。")
