#!/usr/bin/env bash
# 第 3 步：编 llvm-project 14.0.0 的 clang（Release + 断言），产物在 ~/llvm-project-14/cmake-build-release。
#   和公司的区别只在"断言打开"：本地调试时 cast<>、getAs<> 用错会当场断言，而不是在业务代码上莫名崩溃。
#   想与公司完全一致（不开断言）：ASSERTIONS=OFF ./30-build-llvm.sh（需另用一个构建目录，见下）。
# 可调：
#   JOBS=8              编译并发（默认 = min(核数, 内存GB/2)）
#   LINK_JOBS=2         链接并发（默认 = 内存GB/6，至少 1）
#   ASSERTIONS=OFF      关断言（默认 ON）
#   TARGETS="X86"       LLVM 后端（默认只编 X86：分析 aarch64_be 代码只用到前端，不需要 AArch64 后端）
#   LLVM_BUILD=...      构建目录（默认 ~/llvm-project-14/cmake-build-release）
#   EXTRA_TARGETS=...   额外要编的 ninja 目标（默认 clang llvm-config FileCheck count not）
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
[ -d "$LLVM_SRC/llvm" ] || { echo "先跑 20-fetch-llvm.sh"; exit 1; }

cores=$(nproc)
memgb=$(awk '/MemTotal/ {printf "%d", $2/1024/1024}' /proc/meminfo)
JOBS=${JOBS:-$(( cores < memgb/2 ? cores : memgb/2 ))}; [ "$JOBS" -ge 1 ] || JOBS=1
LINK_JOBS=${LINK_JOBS:-$(( memgb/6 ))}; [ "$LINK_JOBS" -ge 1 ] || LINK_JOBS=1
ASSERTIONS=${ASSERTIONS:-ON}
TARGETS=${TARGETS:-X86}
EXTRA_TARGETS=${EXTRA_TARGETS:-"clang llvm-config FileCheck count not"}
gccmajor=$(gcc -dumpversion | cut -d. -f1)
echo "== $cores 核、${memgb} GB 内存 → 编译 -j$JOBS、链接 $LINK_JOBS 路；gcc $gccmajor；断言 $ASSERTIONS；后端 $TARGETS"
free_gb=$(df -BG --output=avail "$(dirname "$LLVM_BUILD")" | tail -1 | tr -dc 0-9)
[ "$free_gb" -ge 10 ] || echo "警告：所在分区只剩 ${free_gb} GB；Release + 断言的构建目录约 1 GB、ccache 约 0.2 GB，但编译中的临时文件与以后的第二个构建目录要留余量，建议 ≥ 10 GB"

# gcc 13 起 libstdc++ 不再顺带包含 <cstdint>，LLVM 14 有几处头文件编不过；打一个只加 #include 的补丁（提交在 rb-local 上，单独一个提交）
if [ "$gccmajor" -ge 13 ]; then
  P="$HERE/patches/llvm14-gcc13-cstdint.patch"
  if git -C "$LLVM_SRC" apply --check "$P" 2>/dev/null; then
    git -C "$LLVM_SRC" apply "$P"
    git -C "$LLVM_SRC" -c user.name=local -c user.email=local@localhost commit -qam "build: gcc 13 缺 <cstdint> 的补丁（只加 #include，不改逻辑）"
    echo "== 已打 gcc 13 补丁（rb-local 上的一个提交）"
  elif git -C "$LLVM_SRC" apply --reverse --check "$P" 2>/dev/null; then
    echo "== gcc 13 补丁已在"
  else
    echo "警告：gcc 13 补丁既打不上也不在，源码可能不是 14.0.0；继续编，出错再看"
  fi
fi

export CCACHE_DIR=${CCACHE_DIR:-$HOME/.ccache}
ccache -M 30G >/dev/null 2>&1 || true

cmake -G Ninja -S "$LLVM_SRC/llvm" -B "$LLVM_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS="$ASSERTIONS" \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD="$TARGETS" \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_CCACHE_BUILD=ON \
  -DLLVM_PARALLEL_COMPILE_JOBS="$JOBS" \
  -DLLVM_PARALLEL_LINK_JOBS="$LINK_JOBS" \
  -DLLVM_ENABLE_PLUGINS=ON \
  -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_BINDINGS=OFF -DCLANG_ENABLE_ARCMT=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

start=$(date +%s)
# shellcheck disable=SC2086
ninja -C "$LLVM_BUILD" -j"$JOBS" ${NINJA_FLAGS:-} $EXTRA_TARGETS
end=$(date +%s)
echo
echo "== 完成：$(( (end-start)/60 )) 分钟"
"$LLVM_BUILD/bin/clang" --version | head -2
du -sh "$LLVM_BUILD" | awk '{print "== 构建目录 " $1}'
ccache -s 2>/dev/null | grep -Ei 'hit|miss|cache size' | head -4 || true
