# 选用哪一套 LLVM 14（被 plugin-build.sh / cc1-run.sh / 40-smoke.sh source）。
#   LLVM_KIND=src  用自己从源码编的（~/llvm-project-14/cmake-build-release，默认）
#   LLVM_KIND=apt  用 Ubuntu 的 clang-14 包（/usr/lib/llvm-14；22.04 上是 14.0.0）
# 导出：LLVM_KIND CLANG LLVM_CONFIG PLUGIN_CXX PLUGIN_CXXFLAGS
HERE_SEL=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE_SEL/env.sh"
if [ -z "${LLVM_KIND:-}" ]; then
  if [ -x "$LLVM_BUILD/bin/clang" ]; then LLVM_KIND=src; else LLVM_KIND=apt; fi
fi
case "$LLVM_KIND" in
  src)
    P=$LLVM_BUILD
    CLANG=$P/bin/clang
    LLVM_CONFIG=$P/bin/llvm-config
    PLUGIN_CXX=${PLUGIN_CXX:-c++}            # 与编 LLVM 的是同一个 g++
    # 源码树的 clang 头文件在两处：源码里的 + 构建目录里生成的（Checkers.inc 等）
    EXTRA_INC="-I$LLVM_SRC/clang/include -I$P/tools/clang/include"
    ;;
  apt)
    P=/usr/lib/llvm-14
    CLANG=$P/bin/clang
    LLVM_CONFIG=$P/bin/llvm-config
    PLUGIN_CXX=${PLUGIN_CXX:-$P/bin/clang++}
    EXTRA_INC=""
    ;;
  *) echo "LLVM_KIND 只能是 src 或 apt" >&2; return 1 2>/dev/null || exit 1 ;;
esac
[ -x "$CLANG" ] || { echo "找不到 $CLANG（LLVM_KIND=$LLVM_KIND）" >&2; return 1 2>/dev/null || exit 1; }
# 插件与 clang 必须用同一套头文件与配置（断言开关会改变 ABI 检查符号）；-std=c++14、无 RTTI、无异常与 LLVM 14 一致
PLUGIN_CXXFLAGS="-std=c++14 -fno-rtti -fno-exceptions -fPIC -O1 -g $( "$LLVM_CONFIG" --cxxflags | sed 's/-std=c++[0-9a-z]*//; s/-fno-exceptions//; s/-O[0-9s]//g; s/-g[0-9]*//g' ) $EXTRA_INC"
export LLVM_KIND CLANG LLVM_CONFIG PLUGIN_CXX PLUGIN_CXXFLAGS
