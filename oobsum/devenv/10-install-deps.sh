#!/usr/bin/env bash
# 第 1 步：装依赖（WSL 里的 Ubuntu 执行；需要 sudo）。
#   - 编 llvm-project 14 用：gcc/g++、cmake、ninja、lld（链接快、省内存）、ccache（切分支 / 重配后少编）
#   - 第一天就能用的"快速通道"：Ubuntu 自带的 clang-14 / libclang-14-dev / llvm-14-dev
#     Ubuntu 22.04 上它们正好是 14.0.0（与公司一致）；24.04 上是 14.0.6（同一大版本，API 相同）
#   - Python 侧：jsonschema（replay.py 的结构检查）
set -euo pipefail
. /etc/os-release
echo "== Ubuntu $VERSION_ID"
case "$VERSION_ID" in
  22.04) ;;
  24.04) echo "注意：24.04 默认 gcc 13，从源码编 LLVM 14 需要补几处 #include <cstdint>（30-build-llvm.sh 会自动打补丁）" ;;
  *)     echo "注意：只在 22.04 / 24.04 上验证过，其它版本请自行判断" ;;
esac
sudo apt-get update
sudo apt-get install -y \
  build-essential git cmake ninja-build ccache lld python3 python3-pip python3-jsonschema \
  zlib1g-dev unzip xz-utils time \
  clang-14 libclang-14-dev llvm-14-dev llvm-14-tools
echo
echo "== 版本"
gcc --version | head -1
cmake --version | head -1
echo "ninja $(ninja --version)"
ld.lld --version | head -1
/usr/lib/llvm-14/bin/clang --version | head -1
