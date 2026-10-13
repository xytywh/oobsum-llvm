#!/usr/bin/env bash
# 沙箱复现：用上游 LLVM 14.0.6（Ubuntu 的 clang-14 / libclang-14-dev）把探针编成插件，在测试用例上跑一遍。
# 你在 fork 里不需要它——fork 里按 README 的"五处落点"编进 xir、用 bxen 跑。
# 用法：./sandbox_build.sh [输出目录]        依赖：apt install clang-14 libclang-14-dev llvm-14-dev
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/_sandbox}
LLVM=${LLVM14:-/usr/lib/llvm-14}
mkdir -p "$OUT/work"

"$LLVM/bin/clang++" -std=c++14 -shared -fPIC -fno-rtti -O1 -DOOBSUM_PLUGIN_BUILD \
    $("$LLVM/bin/llvm-config" --cxxflags | sed 's/-std=c++[0-9]*//') \
    "$HERE/OobSummaryProbeChecker.cpp" "$HERE/OobSummary/ProbeSubdirUnit.cpp" -I "$HERE" -o "$OUT/OobSummaryProbe.so"

RES=$("$LLVM/bin/clang" -print-resource-dir)
TC=$HERE/TestCase/OobSummaryProbeChecker
for f in "$TC"/src/*.c; do
    "$LLVM/bin/clang" -cc1 -triple x86_64-pc-linux-gnu -analyze -load "$OUT/OobSummaryProbe.so" \
        -resource-dir "$RES" -internal-isystem "$RES/include" -internal-externc-isystem /usr/include \
        -I "$TC/include" \
        -analyzer-checker=rb.enhance.core.OobSummaryProbe \
        -analyzer-config rb.enhance.core.OobSummaryProbe:EmitProbeReport=true \
        -analyzer-config "rb.enhance.core.OobSummaryProbe:WorkDir=$OUT/work" \
        -analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-display-progress \
        -analyzer-output=text "$f" 2>&1 | sed "s#$HERE/##g; s#$OUT/##g"
done
