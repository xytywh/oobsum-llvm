#!/usr/bin/env bash
# 第 4 步：冒烟测试——本地环境能不能开始写 checker。六项全 PASS 就可以开工。
# 用法：./40-smoke.sh               自动选：有源码构建就用它，否则用 Ubuntu 的 clang-14
#       LLVM_KIND=apt ./40-smoke.sh  指定用 Ubuntu 的 clang-14
#       LLVM_KIND=src ./40-smoke.sh  指定用 ~/llvm-project-14/cmake-build-release
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/llvm-select.sh" || exit 1
KIT=$(cd "$HERE/.." && pwd)                       # 08-summary2-design
PROBE=$KIT/probes/i0-integration
OUT=${OUT:-$HOME/oobsum-smoke/$LLVM_KIND}
rm -rf "$OUT"; mkdir -p "$OUT/work"
pass=0 fail=0
ok()  { echo "  PASS $*"; pass=$((pass+1)); }
bad() { echo "  FAIL $*"; fail=$((fail+1)); }
echo "== LLVM_KIND=$LLVM_KIND  clang=$CLANG"

echo "[1/6] clang 版本"
v=$("$CLANG" --version | head -1); echo "  $v"
case "$v" in *"clang version 14."*) ok "大版本 14" ;; *) bad "不是 clang 14" ;; esac

echo "[2/6] 上游自带的 checker 能跑（ArrayBoundV2 报一条越界）"
printf 'void f(void) { int a[4]; a[4] = 1; }\n' > "$OUT/oob.c"
"$CLANG" -cc1 -analyze -analyzer-checker=core,alpha.security.ArrayBoundV2 "$OUT/oob.c" > "$OUT/oob.txt" 2>&1
if grep -q "Out of bound memory access" "$OUT/oob.txt"; then
  ok "ArrayBoundV2 报出越界"; else bad "ArrayBoundV2 没报"; fi

echo "[3/6] 把 I0 探针编成插件（同一份源码在公司 xir 里走 Checkers.td 注册）"
if "$HERE/plugin-build.sh" "$OUT/OobSummaryProbe.so" "$PROBE/OobSummaryProbeChecker.cpp" "$PROBE/OobSummary/ProbeSubdirUnit.cpp" -I "$PROBE" > "$OUT/build.log" 2>&1; then
  ok "插件编译通过（$(du -h "$OUT/OobSummaryProbe.so" | cut -f1)）"; else bad "插件编译失败，见 $OUT/build.log"; tail -20 "$OUT/build.log"; fi

echo "[4/6] clang -cc1 -load 加载插件，跑探针用例，与上游基线比对"
TC=$PROBE/TestCase/OobSummaryProbeChecker
: > "$OUT/r1.txt"
for f in "$TC"/src/*.c; do
  "$HERE/cc1-run.sh" -load "$OUT/OobSummaryProbe.so" -checker=rb.enhance.core.OobSummaryProbe \
    -analyzer-config ipa=none -analyzer-config rb.enhance.core.OobSummaryProbe:EmitProbeReport=true \
    -analyzer-config "rb.enhance.core.OobSummaryProbe:WorkDir=$OUT/work" \
    -I "$TC/include" "$f" >> "$OUT/r1.txt" 2>&1
done
# 基线是在上游 14.0.6 上跑的；版本号、路径、按路径算的哈希不比
norm() { sed -E "s#$PROBE/##g; s#$OUT/##g; s/clang=14\.0\.[0-9]+/clang=14.0.x/; s/[0-9a-f]{16}/<hash>/g" "$1"; }
if diff <(norm "$PROBE/expected-upstream14.txt") <(norm "$OUT/r1.txt") > "$OUT/r1.diff"; then
  ok "输出与 expected-upstream14.txt 一致（$(grep -c '^\[OSP\]' "$OUT/r1.txt") 行 [OSP]、$(grep -c 'warning: OobSummary probe' "$OUT/r1.txt") 条探针告警）"
else bad "与基线不同，见 $OUT/r1.diff"; head -20 "$OUT/r1.diff"; fi

echo "[5/6] 目标 ABI：上游 clang 14 的 aarch64 ILP32 与本地近似"
il=$("$HERE/cc1-run.sh" -load "$OUT/OobSummaryProbe.so" -checker=rb.enhance.core.OobSummaryProbe -analyzer-config ipa=none \
      -I "$TC/include" "$TC/src/TC_OobSummaryProbeChecker_a.c" 2>&1 | grep '^\[OSP\] tu' | grep -o 'triple=.*endian=[a-z]*')
il2=$(TRIPLE=aarch64_be-linux-gnu_ilp32 "$HERE/cc1-run.sh" -load "$OUT/OobSummaryProbe.so" -checker=rb.enhance.core.OobSummaryProbe \
      -analyzer-config ipa=none -I "$TC/include" "$TC/src/TC_OobSummaryProbeChecker_a.c" 2>&1 | grep '^\[OSP\] tu' | grep -o 'triple=.*endian=[a-z]*')
il3=$(TRIPLE=armebv7-linux-gnueabi "$HERE/cc1-run.sh" -load "$OUT/OobSummaryProbe.so" -checker=rb.enhance.core.OobSummaryProbe \
      -analyzer-config ipa=none -I "$TC/include" "$TC/src/TC_OobSummaryProbeChecker_a.c" 2>&1 | grep '^\[OSP\] tu' | grep -o 'triple=.*endian=[a-z]*')
echo "  默认              $il"
echo "  aarch64_be ilp32  $il2   ← 上游 14 当成 LP64，指针仍是 8"
echo "  armebv7（近似）   $il3"
case "$il3" in *"ptr=4 long=4 sizet=4 endian=big"*) ok "armebv7 给出 4 字节指针 + 大端，可作本地近似" ;; *) bad "armebv7 结果不对" ;; esac

echo "[6/6] 摘要回放器（只要 Python 3）"
if python3 "$KIT/cases/summaries/replay.py" > "$OUT/replay.txt" 2>&1 && grep -q "0 处不一致" "$OUT/replay.txt"; then
  ok "$(grep '②' "$OUT/replay.txt" | cut -c1-60)…"; else bad "replay.py 未通过，见 $OUT/replay.txt"; fi
find "$KIT/cases/summaries" -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null

echo
echo "== 结果：PASS $pass / FAIL $fail（输出在 $OUT）"
[ "$fail" -eq 0 ]
