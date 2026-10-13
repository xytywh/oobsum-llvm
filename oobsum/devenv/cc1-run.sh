#!/usr/bin/env bash
# 用 clang -cc1 直接跑分析（本地代替 bxen）。插件可选；checker 名与公司一致（rb.enhance.*）。
# 用法：./cc1-run.sh [-load 插件.so ...] -checker=<名字>[,<名字>] <文件.c> [其它 cc1 参数，如 -I 目录、-analyzer-config k=v]
#   TRIPLE=x86_64-pc-linux-gnu（默认）
#   TRIPLE=armebv7-linux-gnueabi   本地近似公司的"4 字节指针 + 大端"（上游 clang 14 不支持 aarch64 ILP32，见 09- 文档）
# 默认带上与 bxen 相同的 -analyzer-max-loop 3；rb.enhance.core.* 规则自己再加 -analyzer-config ipa=none
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/llvm-select.sh"
TRIPLE=${TRIPLE:-x86_64-pc-linux-gnu}
RES=$("$CLANG" -print-resource-dir)
args=() checker=""
while [ $# -gt 0 ]; do
  case "$1" in
    -load) args+=(-load "$2"); shift 2 ;;
    -checker=*) checker=${1#-checker=}; shift ;;
    *) args+=("$1"); shift ;;
  esac
done
[ -n "$checker" ] || { echo "缺 -checker=<名字>"; exit 1; }
sysinc=()
[ "$TRIPLE" = "x86_64-pc-linux-gnu" ] && sysinc=(-internal-externc-isystem /usr/include/x86_64-linux-gnu -internal-externc-isystem /usr/include)
exec "$CLANG" -cc1 -triple "$TRIPLE" -analyze -resource-dir "$RES" -internal-isystem "$RES/include" "${sysinc[@]}" \
  -analyzer-checker="$checker" -analyzer-max-loop 3 -analyzer-output=text "${args[@]}"
