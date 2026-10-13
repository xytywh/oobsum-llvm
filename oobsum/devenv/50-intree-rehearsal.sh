#!/usr/bin/env bash
# 第 5 步（可选，带去公司之前做一次）：在本地 llvm-project-14 里按"公司的注册方式"把 checker 编进 clang 本体——
#   Checkers.td 包块里加条目、CMakeLists 登记源文件、ento::register…（不定义 OOBSUM_PLUGIN_BUILD），
#   然后不用 -load 直接跑。插件模式验证的是"代码对不对"，这一步验证的是"注册对不对"（td 选项、注册函数名、子目录源文件）。
# 在单独的分支 rb-rehearsal 上做；增量编译只重编 StaticAnalyzer 的 checker 库并重链 clang。
# 用法：./50-intree-rehearsal.sh [包目录]      默认包目录 = I0 探针（probes/i0-integration）
#       ./50-intree-rehearsal.sh --undo       回到 rb-local 分支、删掉 rb-rehearsal、重编 clang
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
KIT=$(cd "$HERE/.." && pwd)
cd "$LLVM_SRC"
if [ "${1:-}" = "--undo" ]; then
  git switch -q rb-local && git branch -D rb-rehearsal
  ninja -C "$LLVM_BUILD" clang
  echo "== 已撤销：回到 rb-local，clang 已重编"
  exit 0
fi
DROP=$(cd "${1:-$KIT/probes/i0-integration}" && pwd)
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "$LLVM_SRC 有未提交的改动，先提交或还原"; exit 1; }
if git rev-parse -q --verify rb-rehearsal >/dev/null; then
  git switch -q rb-rehearsal
else
  git switch -q -c rb-rehearsal rb-local
fi
python3 "$HERE/land.py" --drop "$DROP" --into llvm --root "$LLVM_SRC" --apply
git add -A clang/lib/StaticAnalyzer/Checkers clang/include/clang/StaticAnalyzer/Checkers/Checkers.td
git -c user.name=local -c user.email=local@localhost commit -qm "rehearsal: land $(basename "$DROP") in-tree" || echo "（没有新改动）"
start=$(date +%s)
ninja -C "$LLVM_BUILD" clang
echo "== 增量编译 $(( $(date +%s) - start )) 秒"

# 在本体里能看到这个 checker 吗？（不 -load）
name=$(sed -n 's/^def \([A-Za-z0-9_]*\) *: *Checker<"\([^"]*\)">.*/\2/p' "$DROP/$(sed -n 's/^td=//p' "$DROP/drop.manifest")" | head -1)
pkg=$(sed -n 's/^package=//p' "$DROP/drop.manifest")
full="rb.enhance.$pkg.$name"
if "$LLVM_BUILD/bin/clang" -cc1 -analyzer-checker-help 2>/dev/null | grep -q "$full"; then
  echo "  PASS $full 已编进 clang（-analyzer-checker-help 里有）"
else
  echo "  FAIL -analyzer-checker-help 里没有 $full"; exit 1
fi
# 探针：同样的用例、同样的基线，只是不 -load
if [ "$(basename "$DROP")" = "i0-integration" ]; then
  OUT=$HOME/oobsum-smoke/intree; rm -rf "$OUT"; mkdir -p "$OUT/work"
  TC=$DROP/TestCase/OobSummaryProbeChecker
  for f in "$TC"/src/*.c; do
    LLVM_KIND=src "$HERE/cc1-run.sh" -checker="$full" -analyzer-config ipa=none \
      -analyzer-config "$full:EmitProbeReport=true" -analyzer-config "$full:WorkDir=$OUT/work" -I "$TC/include" "$f" >> "$OUT/r1.txt" 2>&1
  done
  norm() { sed -E "s#$DROP/##g; s#$OUT/##g; s/clang=14\.0\.[0-9]+/clang=14.0.x/; s/[0-9a-f]{16}/<hash>/g" "$1"; }
  if diff <(norm "$DROP/expected-upstream14.txt") <(norm "$OUT/r1.txt") > "$OUT/r1.diff"; then
    echo "  PASS 本体里的探针输出与插件模式、与基线一致"
  else
    echo "  FAIL 与基线不同，见 $OUT/r1.diff"; head -20 "$OUT/r1.diff"; exit 1
  fi
fi
echo "== 预演通过。撤销：$0 --undo"
