#!/usr/bin/env bash
# 第 2 步：取 llvm-project 14.0.0 源码到 ~/llvm-project-14（与公司 /usr1/llvm-project-14 同名同层次）。
#   默认：git clone --depth 1 --branch llvmorg-14.0.0（约 1.6 GB，含 .git）
#   GitHub 慢 / 不通时：先在浏览器里下载 llvm-project-14.0.0.src.tar.xz（GitHub Releases），然后
#     LLVM_TARBALL=/mnt/c/Users/<你>/Downloads/llvm-project-14.0.0.src.tar.xz ./20-fetch-llvm.sh
#   或者换镜像：LLVM_GIT=<镜像地址> ./20-fetch-llvm.sh
# 无论哪种方式，最后都在 rb-local 分支上：你的本地修改都提交在这个分支，git diff llvmorg-14.0.0 就是全部改动。
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
if [ -e "$LLVM_SRC" ]; then
  echo "$LLVM_SRC 已存在，跳过下载"
else
  if [ -n "${LLVM_TARBALL:-}" ]; then
    got=$(sha256sum "$LLVM_TARBALL" | cut -d' ' -f1)
    if [ "$got" = "$LLVM_TARBALL_SHA256" ]; then echo "sha256 核对通过：$got"
    else echo "sha256 不符：$got（应为 $LLVM_TARBALL_SHA256）——下载不完整或不是 14.0.0 的源码包"; exit 1; fi
    mkdir -p "$LLVM_SRC"
    tar -xf "$LLVM_TARBALL" -C "$LLVM_SRC" --strip-components=1
    git -C "$LLVM_SRC" init -q
    git -C "$LLVM_SRC" add -A
    git -C "$LLVM_SRC" -c user.name=local -c user.email=local@localhost commit -qm "llvm-project 14.0.0 (from $(basename "$LLVM_TARBALL"))"
    git -C "$LLVM_SRC" tag "$LLVM_TAG"
  else
    git clone --depth 1 --branch "$LLVM_TAG" "$LLVM_GIT" "$LLVM_SRC"
  fi
fi
cd "$LLVM_SRC"
git config core.autocrlf false          # 源码一律 LF，不让 Windows 那边的设置插手
git switch -c rb-local 2>/dev/null || git switch rb-local
echo "== 源码：$LLVM_SRC（分支 $(git branch --show-current)）"
tagc=$(git rev-parse -q --verify "refs/tags/$LLVM_TAG^{commit}" || true)
if [ -n "${LLVM_TARBALL:-}" ]; then
  echo "   来自 GitHub Releases 的源码包（sha256 已核对）"
elif [ "$tagc" = "$LLVM_TAG_COMMIT" ]; then
  echo "   $LLVM_TAG = $tagc（与 GitHub 上游一致）"
else
  echo "   警告：$LLVM_TAG 指向 ${tagc:-<无>}，上游应为 $LLVM_TAG_COMMIT（镜像不同步？）"
fi
grep -m1 'set(LLVM_VERSION_MAJOR' llvm/CMakeLists.txt
grep -m1 'set(LLVM_VERSION_MINOR' llvm/CMakeLists.txt
grep -m1 'set(LLVM_VERSION_PATCH' llvm/CMakeLists.txt
