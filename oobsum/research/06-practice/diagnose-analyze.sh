#!/usr/bin/env bash
# diagnose-analyze.sh —— 跑完 `infer analyze` 后先跑这个：判断成功还是失败，失败时定位原因。
#
# 结论先行：**analyze 报错基本不需要从源码编译 infer 才能定位。**
# 官方预编译包已经带完整的 OCaml 异常栈；真正要做的是分清下面四类：
#   A. 某个过程分析崩了        → --keep-going 跳过它，看看还剩多少
#   B. 内存/磁盘不够            → 降 --jobs、加 swap、换盘
#   C. 单过程超时/CFG 过大      → 调 --timeout / --bo-max-cfg-size
#   D. infer 自身 bug（Assert_failure / Internal error） → 缩到最小复现，记下来绕开
#
# 用法：
#   ./diagnose-analyze.sh <infer-out 目录> [analyze 日志文件]
#
# 例：
#   ./diagnose-analyze.sh result/infer-out analyze.log
set -eu

OUT="${1:?用法: $0 <infer-out 目录> [analyze 日志]}"
LOG="${2:-}"
INFER_BIN="${INFER_BIN:-$(command -v infer || true)}"

hr() { printf '\n════════ %s ════════\n' "$1"; }
have_sqlite() { command -v sqlite3 >/dev/null 2>&1; }

[ -d "$OUT" ] || { echo "找不到目录: $OUT" >&2; exit 2; }

# ---------------------------------------------------------------- 先算结论
N_PROC=""; N_SPEC=""; N_REPORT=""; N_SRC=""
if have_sqlite && [ -f "$OUT/capture.db" ]; then
  N_SRC=$(sqlite3 "$OUT/capture.db" 'SELECT COUNT(*) FROM source_files;' 2>/dev/null || echo "")
  N_PROC=$(sqlite3 "$OUT/capture.db" 'SELECT COUNT(*) FROM procedures;' 2>/dev/null || echo "")
fi
if have_sqlite && [ -f "$OUT/results.db" ]; then
  N_SPEC=$(sqlite3 "$OUT/results.db" 'SELECT COUNT(*) FROM specs;' 2>/dev/null || echo "")
fi
if [ -f "$OUT/report.json" ] && command -v python3 >/dev/null; then
  N_REPORT=$(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))))" "$OUT/report.json" 2>/dev/null || echo "")
fi

CAND="$LOG"
[ -z "$CAND" ] && [ -f "$OUT/logs" ] && CAND="$OUT/logs"
N_FATAL=0; N_INTERNAL=0
if [ -n "$CAND" ] && [ -f "$CAND" ]; then
  N_FATAL=$(grep -c -E "Fatal error|Uncaught exception" "$CAND" 2>/dev/null || true)
  N_INTERNAL=$(grep -c -E "Internal error|Assert_failure" "$CAND" 2>/dev/null || true)
fi

hr "结论"
# 判定依据优先级：specs 条数（要 sqlite3）→ results.db + report.json 存在 → 都没有才算失败。
# 【v2.7 修复】旧版只看 N_SPEC，机器上没装 sqlite3 时 N_SPEC 为空，
# 成功的运行也会被误判成"失败"。
CLEAN=0
[ "${N_FATAL:-0}" -eq 0 ] && [ "${N_INTERNAL:-0}" -eq 0 ] && CLEAN=1

VERDICT="未知"
if [ -n "${N_SPEC:-}" ] && [ "${N_SPEC:-0}" -gt 0 ]; then
  if [ "$CLEAN" -eq 1 ]; then
    VERDICT="成功"
    echo "  ✓ analyze **成功**：写出 $N_SPEC 条摘要，日志里没有致命错误。"
    [ -n "${N_REPORT:-}" ] && echo "    报告条数：$N_REPORT"
  else
    VERDICT="部分成功"
    echo "  ! analyze **部分完成**：写出 $N_SPEC 条摘要，但日志里有"
    echo "    致命错误 ${N_FATAL} 处 / 内部错误 ${N_INTERNAL} 处。"
  fi
elif [ ! -f "$OUT/results.db" ]; then
  VERDICT="失败"
  echo "  ✗ analyze **失败**：results.db 都没生成，没写出任何摘要。"
elif [ -f "$OUT/report.json" ] && [ "$CLEAN" -eq 1 ]; then
  VERDICT="成功"
  echo "  ✓ analyze **看起来是成功的**：results.db 与 report.json 都在，日志干净。"
  [ -n "${N_REPORT:-}" ] && echo "    报告条数：$N_REPORT"
  have_sqlite || echo "    （没装 sqlite3，数不了摘要条数；装上能给更准的判断）"
elif [ "$CLEAN" -eq 1 ]; then
  VERDICT="未知"
  echo "  ? 无法判断：results.db 在、日志干净，但没有 report.json。"
  echo "    先跑一下： infer report --results-dir $OUT"
else
  VERDICT="部分成功"
  echo "  ! results.db 在，但日志里有致命错误 ${N_FATAL} 处 / 内部错误 ${N_INTERNAL} 处。"
fi
case "$VERDICT" in
  成功)     echo "  → 直接看下面第 6 节「分析成功后该看什么」。" ;;
  部分成功) echo "  → 看第 3 节的异常栈，再看第 7 节对号入座。" ;;
  *)        echo "  → 看第 3、5 节，再按第 7 节对号入座。" ;;
esac

hr "0. 基本信息"
echo "infer:     ${INFER_BIN:-（PATH 里没有）}"
[ -n "$INFER_BIN" ] && "$INFER_BIN" --version | head -1
echo "结果目录:  $OUT   ($(du -sh "$OUT" 2>/dev/null | cut -f1))"
echo "磁盘剩余:  $(df -h "$OUT" | awk 'NR==2{print $4" / "$2"  已用 "$5}')"
MEM_GB=$(awk '/MemTotal/{printf "%d", $2/1048576}' /proc/meminfo 2>/dev/null || echo 0)
echo "内存:      $(free -h 2>/dev/null | awk 'NR==2{print $7" 可用 / "$2" 总"}' || echo '?')"
NCPU=$(nproc)
echo "CPU:       $NCPU 核"

# 按实际内存/核数给 --jobs 建议：经验值 ~2 GB/并发（bufferoverrun），留一半内存余量
if [ "${MEM_GB:-0}" -gt 0 ]; then
  J_MEM=$(( MEM_GB / 4 ));  [ "$J_MEM" -lt 1 ] && J_MEM=1
  J_SUG=$(( J_MEM < NCPU ? J_MEM : NCPU ))
  echo "建议 --jobs: $J_SUG   （按 ~2GB/并发 估算，并留一半内存余量；纯 CPU 上限是 $NCPU）"
fi

hr "1. capture 结果（analyze 失败不该毁掉 capture）"
if [ -f "$OUT/capture.db" ]; then
  if have_sqlite; then
    echo "  源文件: ${N_SRC:-?}"
    echo "  过程数: ${N_PROC:-?}"
  else
    echo "  capture.db 存在（$(du -h "$OUT/capture.db" | cut -f1)），但没有 sqlite3 可用"
    echo "  装一个： yum install -y sqlite  /  apt-get install -y sqlite3"
  fi
else
  echo "  !! capture.db 缺失——问题在 capture 阶段，不是 analyze"
fi

hr "2. 分析进度"
if [ ! -f "$OUT/results.db" ]; then
  echo "  !! results.db 不存在 —— analyze 在写出任何摘要之前就挂了"
elif have_sqlite; then
  echo "  已写出摘要: ${N_SPEC:-?} 条"
  if [ -n "${N_PROC:-}" ] && [ -n "${N_SPEC:-}" ] && [ "${N_PROC:-0}" -gt 0 ]; then
    pct=$(( N_SPEC * 100 / N_PROC ))
    echo "  覆盖率:     $N_SPEC / $N_PROC 个过程 (${pct}%)"
    echo "  （达不到 100% 是正常的：只有被需要的过程才会按需分析，"
    echo "    未被任何人调用、且自身不产生告警的过程可能根本没被算过）"
  fi
else
  echo "  results.db 存在（$(du -h "$OUT/results.db" | cut -f1)），但没有 sqlite3"
fi

hr "3. 日志里的错误分类"
if [ -n "$CAND" ] && [ -f "$CAND" ]; then
  echo "  日志: $CAND  ($(wc -l < "$CAND") 行)"
  any=0
  for pat in "Internal error" "Assert_failure" "Fatal error" "Uncaught exception" \
             "Out of memory" "No space left" "Timeout" "SIGKILL" "Stack overflow" \
             "Skipped large procedure"; do
    c=$(grep -c "$pat" "$CAND" 2>/dev/null || true)
    if [ "${c:-0}" -gt 0 ]; then printf '  %-28s %s 次\n' "$pat" "$c"; any=1; fi
  done
  [ "$any" -eq 0 ] && echo "  ✓ 上述关键字一个都没出现 —— 日志是干净的"
  if [ "${N_FATAL:-0}" -gt 0 ] || [ "${N_INTERNAL:-0}" -gt 0 ]; then
    echo
    echo "  最先出现的异常栈（前 25 行）："
    ln=$(grep -n -m1 -E "Internal error|Assert_failure|Fatal error|Uncaught exception" "$CAND" | cut -d: -f1)
    sed -n "${ln},$((ln+24))p" "$CAND" | sed 's/^/    /'
  fi
else
  echo "  没给日志文件。重跑时请这样保存："
  echo "    infer analyze --results-dir $OUT --keep-going 2>&1 | tee analyze.log"
fi

hr "4. infer-out 里的线索文件"
for f in logs toplevel.log environment.json .infer_runstate.json report.json; do
  [ -e "$OUT/$f" ] && echo "  有 $f  ($(du -h "$OUT/$f" 2>/dev/null | cut -f1))"
done
[ -d "$OUT/captured" ] && echo "  captured/ 下 $(ls "$OUT/captured" 2>/dev/null | wc -l) 个条目"

hr "5. OOM 证据（只看与 infer 有关的）"
# 【v2.7 修复】旧版用 "out of memory" 做关键字，会把机器上**别的进程**被 OOM 杀掉的
# 记录也算进来（实测把 codeagent 被杀的 5 行报成了 infer 的问题），属于假警报。
# 现在只匹配进程名里含 infer/clang/BuckD 的行；其它进程的 OOM 单独提示，不下结论。
if command -v dmesg >/dev/null 2>&1; then
  mine=$(dmesg 2>/dev/null | grep -iE "Kill(ed)? process [0-9]+ \((infer|clang|InferAnalyze|InferClang)" | tail -5 || true)
  other=$(dmesg 2>/dev/null | grep -icE "out of memory" || true)
  if [ -n "$mine" ]; then
    echo "  !! infer 相关进程被 OOM killer 杀过："
    printf '%s\n' "$mine" | sed 's/^/    /'
  else
    echo "  ✓ 没有 infer 相关进程被 OOM 杀掉的记录"
    [ "${other:-0}" -gt 0 ] && \
      echo "  （dmesg 里有 $other 行其它进程的 OOM 记录，和本次 analyze 无关，忽略）"
  fi
else
  echo "  没有 dmesg"
fi

hr "6. 分析成功后该看什么"
if [ "$VERDICT" = "失败" ]; then
  echo "  （本次 analyze 没成功，先看第 7 节）"
else
  echo "  ① 告警总数与类型分布"
  if [ -f "$OUT/report.json" ] && command -v python3 >/dev/null; then
    python3 - "$OUT/report.json" <<'PY' | sed 's/^/     /'
import json, sys, collections
r = json.load(open(sys.argv[1]))
c = collections.Counter(x["bug_type"] for x in r)
print(f"总计 {len(r)} 条")
for k, v in c.most_common(15):
    print(f"  {v:6d}  {k}")
PY
  else
    echo "     （没有 report.json，先跑 infer report --results-dir $OUT）"
  fi
  echo
  echo "  ② 有多少过程「根本没看」（静默漏报的来源）"
  if [ -f "$OUT/logs" ]; then
    sk=$(grep -c "Skipped large procedure" "$OUT/logs" 2>/dev/null || true)
    echo "     Skipped large procedure: ${sk:-0} 个"
    [ "${sk:-0}" -gt 0 ] && echo "     → 这些函数的 CFG 超过 --bo-max-cfg-size（默认 15000），完全没分析"
  fi
  echo
  echo "  ③ 跨过程深度分布（挑 ≥3 层的样例做 Summary 2.0 验收语料用；v3.6 起不再决定做不做）"
  _kit="$(cd "$(dirname "$0")/.." && pwd)"
  echo "     python3 $_kit/08-summary2-design/probes/t0-3/t03_depth.py infer $OUT/report.json --csv depth.csv"
  echo
  echo "  ④ 把这次运行的全部可观测点 dump 出来"
  echo "     ./observe.sh $OUT"
fi

hr "7. 失败时对号入座"
cat <<'EOF'
  ① 有 Internal error / Assert_failure 栈
     → infer 自身在某个过程上崩了。加 --keep-going 跳过它，跑完看还剩多少告警。
       **这种情况自己编译 infer 也修不好**，除非你打算改 OCaml 源码。

  ② 第 5 节确认 infer 进程被 OOM 杀掉
     → 降并发。infer 的内存峰值大致与 --jobs 成正比，bufferoverrun 约 2GB/并发。
       用第 0 节给的「建议 --jobs」。

  ③ 有 No space left
     → infer-out 会长到几个 GB。换个大盘：--results-dir /data/infer-out

  ④ 什么错都没有，就是跑很久没动静
     → 不是挂了，是慢。加 --progress-bar-style plain 看进度。

  ⑤ 无论哪种，先只开一个 checker
     → --bufferoverrun-only。全开会把 17 个 payload 都算一遍，慢好几倍，
       而且对「数组越界」这个目标毫无帮助。

  ⑥ 想缩小范围定位：切一个子集重跑
     → 裁剪 compile_commands.json 只留一个目录，重新 capture。
EOF
