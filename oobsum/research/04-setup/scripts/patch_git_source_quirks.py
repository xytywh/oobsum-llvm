#!/usr/bin/env python3
"""把源改成 git 之后才会暴露的兼容性问题，在这里统一打补丁。

背景：opam 有个内置变量 `dev`——**源是 git 检出时为 true，是发布压缩包时为 false**。
我们为了绕开受限网络，把一大批包的源从"压缩包"改写成了"git"，
于是这些包的 `dev` 从 false 变成 true，一些平时走不到的构建分支被激活，
有的就露馅了。这不是我们改错了，是上游那条分支本来就没人测过。

目前收录一条（实测踩到）：

  topkg 的 --dev-pkg 写法
  ------------------------
  上游写的是：
      build: [[ "ocaml" "pkg/pkg.ml" "build" "--dev-pkg=true" {dev} ]]
  含义是"dev 为真时，追加一个字面量参数 --dev-pkg=true"。
  但这些包 git tag 里自带的 pkg/pkg.ml（老 topkg）只认空格分隔的
  `--dev-pkg true`，不认 `--dev-pkg=true`，于是报：

      pkg.ml: [ERROR] key --dev-pkg=true: No value specified.

  改成两个参数即可（语义等价，且 dev=false 时传 "false" 也是合法的）：
      build: [[ "ocaml" "pkg/pkg.ml" "build" "--dev-pkg" "%{dev}%" ]]

  影响范围很小：整个 opam-repository 里只有 fpath 和 notty 用这个写法，
  但 fpath 是 infer 的必需依赖，不修就装不上。

用法：
  python3 patch_git_source_quirks.py <opam-repository 路径>

可反复执行（幂等）。
"""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/infer-build/opam-repository")

# 只有源已经是 git 的包才需要修——压缩包源的 dev=false，那条分支根本走不到
IS_GIT_SRC = re.compile(r'src:\s*"git\+')

# "--dev-pkg=true" {dev}   ->   "--dev-pkg" "%{dev}%"
DEVPKG = re.compile(r'"--dev-pkg=true"\s*\{\s*dev\s*\}')
DEVPKG_FIXED = '"--dev-pkg" "%{dev}%"'


def main():
    pkgdir = os.path.join(ROOT, "packages")
    if not os.path.isdir(pkgdir):
        print(f"✗ 找不到 {pkgdir}", file=sys.stderr)
        return 2

    fixed, already, scanned = [], 0, 0
    for dirpath, _d, filenames in os.walk(pkgdir):
        if "opam" not in filenames:
            continue
        p = os.path.join(dirpath, "opam")
        scanned += 1
        try:
            s = open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        if not IS_GIT_SRC.search(s):
            continue
        if DEVPKG_FIXED in s:
            already += 1
            continue
        if not DEVPKG.search(s):
            continue
        open(p, "w").write(DEVPKG.sub(DEVPKG_FIXED, s))
        fixed.append(os.path.basename(dirpath))

    print(f"   扫描 {scanned} 个 opam 文件")
    if fixed:
        print(f"   ✓ 修正 topkg --dev-pkg 写法：{len(fixed)} 个 —— {', '.join(sorted(fixed))}")
    elif already:
        print(f"   ✓ 已经是修正后的写法（{already} 个），无需改动")
    else:
        print("   ✓ 没有需要修正的包")
    if fixed:
        print("   （改完记得 opam update default，否则 opam 用的还是旧索引）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
