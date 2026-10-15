#!/usr/bin/env bash
# core/ 的单元测试 oobsum-core-tests（oobsum/specs/SPEC-0004）：首次配置到 cmake-build-release/oobsum-core-tests/，之后增量编译，再运行。
# 用法：oobsum/devenv/core-tests.sh [名字子串]      例：oobsum/devenv/core-tests.sh Policy
# 需要已构建的 cmake-build-release/（用里面的 LLVMSupport 与 LLVMConfig.cmake）。
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${OOBSUM_BUILD:-$REPO/cmake-build-release}
OUT=$BUILD/oobsum-core-tests
if [ ! -f "$OUT/build.ninja" ]; then
  launcher=()
  command -v ccache > /dev/null && launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  cmake -G Ninja -S "$REPO/oobsum/unittests" -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR="$BUILD/lib/cmake/llvm" "${launcher[@]}" > "$OUT.configure.log" 2>&1 \
    || { cat "$OUT.configure.log"; exit 1; }
fi
ninja -C "$OUT" > "$OUT.build.log" 2>&1 || { cat "$OUT.build.log"; exit 1; }
exec "$OUT/oobsum-core-tests" "$@"
