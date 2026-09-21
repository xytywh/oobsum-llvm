#!/usr/bin/env bash
# 受限路线第 4 步：构建 infer 本体。
# 关键提速：复用官方预编译包里已经编译好的 clang，跳过从源码自举 LLVM/Clang 的数小时。
set -eu
WORK="${WORK:-$HOME/infer-build}"
INFER="${INFER:-$WORK/infer}"
PREBUILT="${PREBUILT:-}"        # 指向解压后的 infer-linux-x86_64-v1.3.0 目录
# 【v3.3】没设 PREBUILT 时，自动用脚本 35 解压到的默认位置
if [ -z "$PREBUILT" ] && [ -d "$WORK/infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install" ]; then
  PREBUILT="$WORK/infer-linux-x86_64-v1.3.0"
  echo "自动找到预编译包：$PREBUILT"
fi
export PATH="$WORK/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
export OPAMYES=1
J="${J:-$(nproc)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"
[ -f "$OPAMROOT/config" ] || { echo "✗ opam root 无效，先跑 20 和 30。" >&2; exit 2; }
echo "构建 infer：复用预编译 clang 时 2 核约 70 分钟；不复用要多几个小时。"

eval "$(opam env --switch=infer541 --set-switch)"

if [ -z "$PREBUILT" ]; then
  echo "提示：没设 PREBUILT，将从源码编译 LLVM/Clang（数小时）。"
  echo "     强烈建议先取官方预编译包（它会解压到 40 自动能找到的位置）："
  echo "       ./35-fetch-prebuilt.sh"
  read -r -p "     继续从源码编译 LLVM/Clang？[y/N] " a; [ "$a" = y ] || exit 1
else
  echo "== 复用预编译 clang"
  cd "$INFER/facebook-clang-plugins/clang"
  ln -sfn "$PREBUILT/lib/infer/facebook-clang-plugins/clang/install" install
  SHASUM="sha256sum" ./setup.sh -r
  SHASUM="sha256sum" ./setup.sh -c && echo "  setup.sh 认为 clang 已安装 ✓"
fi

cd "$INFER"
# charon 装不上时，需要把 Rust 前端从 dune 构建里摘掉（--disable-rust-analyzers 不够，
# 因为 infer/src/integration/dune.in 无条件依赖 charon 库）
if ! opam list --installed charon >/dev/null 2>&1; then
  echo "== charon 未安装 → 摘掉 Rust 前端"
  bash "$(dirname "$0")/45-strip-rust-frontend.sh" "$INFER"
fi

echo "== autogen + configure"
./autogen.sh
./configure --disable-rust-analyzers --disable-java-analyzers --disable-erlang-analyzers \
            --disable-hack-analyzers --disable-python-analyzers --disable-swift-analyzers

echo "== make（2 核约 13 分钟）"
make -j"$J" BUILD_MODE=default infer
./infer/bin/infer --version
echo
echo "完成。把它加进 PATH： export PATH=$INFER/infer/bin:\$PATH"

step_done
