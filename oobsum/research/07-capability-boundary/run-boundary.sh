#!/usr/bin/env bash
# run-boundary.sh —— 复现《01-capability-boundary-fp-fn.md》里的全部数字
#
# 用法：
#   ./run-boundary.sh              # 跑全部四组语料 × 两档配置
#   ./run-boundary.sh tp           # 只跑某一组：tp / tn / fp / probe
#   ./run-boundary.sh tp default   # 只跑某一组 + 某一档：default / high / pulse
#
# 需要 PATH 里有 infer（v1.3.0 官方预编译即可，不需要自建）。
# 每个 .c 单独跑一次 infer run，结果写到 /tmp/bo_<组>_<档>_<用例>/，
# 统计口径为 report.json 的条目数（不是终端输出里含 "error:" 的行数——
# 那会把汇总表也算进去，数字翻倍）。
set -u

command -v infer >/dev/null 2>&1 || { echo "找不到 infer，请先把它加进 PATH" >&2; exit 2; }
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here/corpus" || { echo "找不到 corpus/ 目录" >&2; exit 2; }

HIGH_FLAGS=(
  --bo-debug 4
  --enable-issue-type BUFFER_OVERRUN_L4
  --enable-issue-type BUFFER_OVERRUN_L5
  --enable-issue-type BUFFER_OVERRUN_U5
  --enable-issue-type INTEGER_OVERFLOW_L5
  --enable-issue-type INTEGER_OVERFLOW_U5
)

count_json() {
  python3 - "$1" <<'PY'
import json, sys, collections
try:
    r = json.load(open(sys.argv[1]))
except Exception:
    r = []
c = collections.Counter(x["bug_type"] for x in r)
print(len(r), ",".join(f"{k}x{v}" for k, v in sorted(c.items())) or "-")
PY
}

run_group() {
  local d="$1" label="$2"; shift 2
  echo "######## $d  [$label] ########"
  local tot=0 files_with=0 total_files=0
  for f in "$d"/*.c; do
    local b out n types
    b="$(basename "$f" .c)"
    out="/tmp/bo_${d}_${label}_${b}"
    rm -rf "$out"
    infer run -o "$out" "$@" -- clang -c "$f" >/dev/null 2>&1
    read -r n types < <(count_json "$out/report.json")
    tot=$((tot + n)); total_files=$((total_files + 1))
    [ "$n" -gt 0 ] && files_with=$((files_with + 1))
    printf "  %-26s %2d  %s\n" "$b" "$n" "$types"
  done
  echo "  ---- 合计 $tot 条告警，涉及 $files_with / $total_files 个文件 ----"
  echo
}

groups=("${1:-}")
[ -z "${groups[0]}" ] && groups=(tp tn fp probe)
modes=("${2:-}")
[ -z "${modes[0]}" ] && modes=(default high)

for g in "${groups[@]}"; do
  [ -d "$g" ] || { echo "没有这一组: $g" >&2; continue; }
  for m in "${modes[@]}"; do
    case "$m" in
      default) run_group "$g" default --bufferoverrun-only ;;
      high)    run_group "$g" high    --bufferoverrun-only "${HIGH_FLAGS[@]}" ;;
      pulse)   run_group "$g" pulse   --pulse-only ;;
      *)       echo "未知档位: $m（可选 default / high / pulse）" >&2 ;;
    esac
  done
done

cat <<'EOF'
对照《01-capability-boundary-fp-fn.md》的预期：

  tp    默认 18/22 检出   高灵敏 19/22 检出
  tn    默认  0 条误报    高灵敏 9/15 个文件有误报
  fp    默认  0 条误报    高灵敏 4/10 个文件有误报
  probe 默认  4/13 检出   —— 这一组是结构体字段数组的专项探针，
                             4 个能检出的分别是 v6 / v7 / v11 / v12，
                             其余 9 个（局部/静态/全局 struct 的字段数组）全部漏报

数字对不上先别慌，看 infer --version 是不是 v1.3.0；跨大版本这些档位会变。
EOF
