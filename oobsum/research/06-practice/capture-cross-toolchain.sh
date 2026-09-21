#!/usr/bin/env bash
# capture-cross-toolchain.sh
#
# 用 infer capture 处理跨编译工具链（aarch64_be + ilp32 RTOS）的 compile_commands.json。
# 配套文档：03-cross-toolchain-capture-fix.md
#
# 本脚本实现了文档中的【方案 A】：保留 cdb 不动，给 infer 加一组覆盖参数，
# 绕过 infer 自带 clang-21 不支持 aarch64_be-ilp32 ABI、以及 cdb 中 -isystem / C++ 头路径
# 指向已卸载的 b003 工具链这两个问题。
#
# 用法：
#   ./capture-cross-toolchain.sh <compile_commands.json> [output-dir]
#
# 例：
#   ./capture-cross-toolchain.sh \
#       /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json
#
# 输出：
#   - <output-dir>/infer-out/           infer 的结果目录
#   - <output-dir>/capture.log          完整 capture 日志（stdout+stderr）
#   - <output-dir>/capture.summary.txt  失败统计摘要（错误计数 + 去重样例）
set -eu

# ----------------------------------------------------------------------
# 0. 参数与路径
# ----------------------------------------------------------------------
CDB="${1:-}"
OUT_DIR="${2:-./infer-out-cross}"

if [[ -z "$CDB" ]]; then
  cat >&2 <<EOF
用法: $0 <compile_commands.json> [output-dir]
例:   $0 /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json
EOF
  exit 2
fi

if [[ ! -f "$CDB" ]]; then
  echo "错误: compile_commands.json 不存在: $CDB" >&2
  exit 2
fi

# 解析为绝对路径
CDB="$(cd "$(dirname "$CDB")" && pwd)/$(basename "$CDB")"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

# infer 二进制：优先用 PATH 里的，找不到回退到调研包内置版本
INFER_BIN="${INFER_BIN:-}"
if [[ -z "$INFER_BIN" ]]; then
  if command -v infer >/dev/null 2>&1; then
    INFER_BIN="$(command -v infer)"
  else
    INFER_BIN="/usr1/BXPlugin/xir/FunctionSummary/infer-linux-x86_64-v1.3.0/bin/infer"
  fi
fi
if [[ ! -x "$INFER_BIN" ]]; then
  echo "错误: 找不到可执行的 infer: $INFER_BIN" >&2
  exit 2
fi

# BiSheng ilp32 sysroot
SYSROOT="/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek_llvm/sdkilp32/"
if [[ ! -d "$SYSROOT" ]]; then
  echo "警告: sysroot 不存在: $SYSROOT" >&2
  echo "      如果工具链版本不同，请修改脚本顶部的 SYSROOT 变量" >&2
fi

# 失效路径前缀（需要被 infer 的 clang 内部头替换掉的 -isystem 前缀）
STALE_PATH_REGEX="/opt/buildtools/bisheng_embedded_cpu"

# infer 自带 libc++/v1 头路径（提供 <algorithm> <map> <set> <cstddef> <cstdint> <string> 等）
# 实测 --clang-libcxx-include-to-override-regex 不会把 -I c++/14.3.0 重定向到 c++/v1，
# 改用 --Xclang "-I <libcxx>/v1" 显式注入，对所有文件（含 C 文件，无副作用）生效。
INFER_LIBCXX_INC="/usr1/BXPlugin/xir/FunctionSummary/infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install/include/c++/v1"
if [[ ! -d "$INFER_LIBCXX_INC" ]]; then
  echo "警告: infer 自带 libc++/v1 不存在: $INFER_LIBCXX_INC" >&2
  echo "      C++ 标准头将无法找到，3651 个 C++ 文件会全部 capture 失败" >&2
fi

# ----------------------------------------------------------------------
# 1. cdb 自检
# ----------------------------------------------------------------------
echo "==> cdb:           $CDB"
echo "==> 输出目录:       $OUT_DIR"
echo "==> infer:         $INFER_BIN ($("$INFER_BIN" --version 2>&1 | head -1))"
echo "==> sysroot:       $SYSROOT"
echo

cdb_size=$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$CDB")
echo "==> cdb 记录数:     $cdb_size"
if [[ "$cdb_size" -eq 0 ]]; then
  echo "错误: cdb 为空" >&2
  exit 2
fi

# ----------------------------------------------------------------------
# 2. 清理旧产物，避免 infer 拒绝覆盖
# ----------------------------------------------------------------------
RESULTS_DIR="$OUT_DIR/infer-out"
LOG_FILE="$OUT_DIR/capture.log"
SUMMARY_FILE="$OUT_DIR/capture.summary.txt"

if [[ -d "$RESULTS_DIR" ]]; then
  echo "==> 清理旧 infer-out: $RESULTS_DIR"
  rm -rf "$RESULTS_DIR"
fi
rm -f "$LOG_FILE" "$SUMMARY_FILE"

# ----------------------------------------------------------------------
# 3. 跑 infer capture（方案 A 参数全在这）
# ----------------------------------------------------------------------
# 参数解释见配套文档「方案 A」表格：
#   --Xclang "--target=..."          让自带 clang 定义 __aarch64__
#   --Xclang "-mbig-endian"          大端
#   --Xclang "-U__LP64__"            撤销自带 clang 默认的 LP64（ilp32 不能是 LP64）
#   --Xclang "-D__ILP32__=1"         手动定义 ILP32（自带 clang 不支持该 ABI）
#   --Xclang "--sysroot=..."         ilp32 sysroot，提供 bits/wordsize-32.h 等
#   --Xclang "-I .../c++/v1"         显式注入 infer 自带 libc++/v1，提供 C++ 标准头
#                                   （实测 --clang-libcxx-include-to-override-regex
#                                    不会把 cdb 里的 -I c++/14.3.0 重定向到 c++/v1，
#                                    改用 --Xclang -I 显式注入）
#   --clang-isystem-to-override-regex  失效 -isystem → infer 自带 clang 内部头
#                                      （提供 stddef.h / stdint.h 等，c++/v1 的
#                                       #include_next <stddef.h> 也靠它补全）
#   --keep-going                     单文件失败不中断
#   --progress-bar-style plain       实时进度条（百分比 + 已完成/总数 + 耗时），
#                                    用 `tee` 同时输出到终端和日志，长任务不再"黑盒"
echo "==> 开始 capture，日志: $LOG_FILE"
echo "==> 进度会实时显示在下方（infer 原生进度条）"
echo

set +e
"$INFER_BIN" capture \
  --compilation-database "$CDB" \
  --results-dir "$RESULTS_DIR" \
  --keep-going \
  --progress-bar-style plain \
  --Xclang "--target=aarch64_be-target-linux-gnu_ilp32" \
  --Xclang "-mbig-endian" \
  --Xclang "-U__LP64__" \
  --Xclang "-D__ILP32__=1" \
  --Xclang "--sysroot=$SYSROOT" \
  --Xclang "-I$INFER_LIBCXX_INC" \
  --clang-isystem-to-override-regex "$STALE_PATH_REGEX" \
  2>&1 | tee "$LOG_FILE"
RC=${PIPESTATUS[0]}
set -e

echo
echo "==> infer capture 退出码: $RC"
echo

# ----------------------------------------------------------------------
# 4. 失败统计摘要
# ----------------------------------------------------------------------
echo "==> 生成失败摘要: $SUMMARY_FILE"
{
  echo "infer capture 失败摘要"
  echo "====================="
  echo "时间:           $(date '+%Y-%m-%d %H:%M:%S')"
  echo "cdb:            $CDB"
  echo "记录数:         $cdb_size"
  echo "infer:          $INFER_BIN"
  echo "退出码:         $RC"
  echo "完整日志:       $LOG_FILE"
  echo

  if [[ -d "$RESULTS_DIR" ]]; then
    echo "infer-out 大小: $(du -sh "$RESULTS_DIR" | cut -f1)"
    if [[ -f "$RESULTS_DIR/capture.db" ]]; then
      echo "capture.db:     $(du -sh "$RESULTS_DIR/capture.db" | cut -f1)"
    else
      echo "capture.db:     【缺失】"
    fi
  else
    echo "infer-out:      【未生成】"
  fi
  echo

  # 【本包补充】真正该看的是 capture.db 里落了多少源文件，
  # 而不是日志里 error 的条数——后者只是下限。
  #
  # 【重要修正】cdb 记录数 ≠ 唯一源文件数。同一 .cpp 可能以多组 flag
  # 出现在 cdb 中（例如不同 build target），infer capture 按源文件去重，
  # 只落一条进 source_files。因此正确分母是 cdb 里的唯一源文件数，
  # 不是 cdb 记录数。用记录数做分母会把"重复条目"误报成"失败"。
  if [[ -f "$RESULTS_DIR/capture.db" ]] && command -v sqlite3 >/dev/null 2>&1; then
    captured=$(sqlite3 "$RESULTS_DIR/capture.db" "SELECT COUNT(*) FROM source_files;" 2>/dev/null || echo "?")
    procs=$(sqlite3 "$RESULTS_DIR/capture.db" "SELECT COUNT(*) FROM procedures;" 2>/dev/null || echo "?")
    # 统计 cdb 里的唯一源文件数（去重后的真正分母）
    unique_srcs=$(python3 -c "
import json,os,sys
cdb=json.load(open(sys.argv[1]))
srcs=set(os.path.normpath(e.get('file','')) for e in cdb if e.get('file'))
print(len(srcs))
" "$CDB")
    echo "capture 覆盖率"
    echo "--------------"
    echo "cdb 记录数:              $cdb_size"
    echo "cdb 唯一源文件数:        $unique_srcs   ← 正确分母"
    echo "capture.db 源文件数:     $captured"
    echo "capture.db 过程数:       $procs"
    if [[ "$captured" != "?" && "$unique_srcs" -gt 0 ]]; then
      python3 -c "
n=int('$captured'); total=int('$unique_srcs'); dup=int('$cdb_size')-total
print(f'成功 capture（按唯一源文件）: {n} / {total} ({n/total*100:.1f}%)')
print(f'cdb 重复条目（非失败）:       {dup}')
miss=max(total-n,0)
if miss>0:
    print(f'真正未落库:                   {miss} ({miss/total*100:.1f}%)')
"
    fi
    echo
  fi

  echo "错误计数"
  echo "--------"
  fatal_lines=$(grep -c 'fatal error:' "$LOG_FILE" || true)
  err_lines=$(grep -c 'error:' "$LOG_FILE" || true)
  warn_lines=$(grep -c 'warning:' "$LOG_FILE" || true)
  fe_lines=$(grep -c 'Frontend error:' "$LOG_FILE" || true)
  printf "fatal error 行数:          %d\n" "$fatal_lines"
  printf "error 行数（含 fatal）:    %d\n" "$err_lines"
  printf "warning 行数:              %d\n" "$warn_lines"
  printf "Frontend error（infer 崩溃）: %d\n" "$fe_lines"
  echo

  # fatal error 涉及的独立源文件数（更直观：多少个文件因此完全没进 capture.db）
  # 【注意】"too many errors emitted" 只是 clang 对单文件错误数超限的终止提示，
  # --keep-going 下 infer 仍会用已恢复的 AST 落库，文件不一定缺失。
  # Frontend error 是 infer 前端插件在某几个方法上 assert 崩溃，按方法跳过，
  # 不影响同文件其他方法，文件本身仍会落库。
  fatal_files=$(grep 'fatal error:' "$LOG_FILE" \
    | grep -oE '^/[^:]+\.(c|cpp|cc|cxx|C)' \
    | sort -u | wc -l)
  fe_files=$(grep -E 'Aborting translation of method .* in file' "$LOG_FILE" \
    | grep -oE "file '[^']+'" | sort -u | wc -l)
  echo "受影响源文件数（下限，不代表完全失败）"
  echo "----------------------------------------"
  printf "fatal error 涉及源文件:    %d\n" "$fatal_files"
  printf "Frontend error 涉及源文件: %d\n" "$fe_files"
  echo "（这些文件大多仍已部分落库，只是个别方法被跳过）"
  echo

  echo "前 20 条去重 fatal error"
  echo "------------------------"
  grep 'fatal error:' "$LOG_FILE" \
    | sed -E "s|^[^:]+:[0-9]+:[0-9]+:||" \
    | sort -u \
    | head -20
  echo

  echo "前 20 条去重 error（非 fatal）"
  echo "------------------------------"
  grep 'error:' "$LOG_FILE" \
    | grep -v 'fatal error:' \
    | sed -E "s|^[^:]+:[0-9]+:[0-9]+:||" \
    | sort -u \
    | head -20
} > "$SUMMARY_FILE"

cat "$SUMMARY_FILE"

echo
echo "==> 完成。下一步："
echo "    1. 检查 $SUMMARY_FILE 顶部 capture.db 是否生成、覆盖率是多少"
echo "    2. 若 fatal error 数量仍很多，把摘要里的样例贴出来继续定位"
echo "    3. 若 capture 成功，可执行："
echo "       $INFER_BIN analyze --results-dir $RESULTS_DIR --keep-going"
echo "    4. 【建议】先跑一遍能力探针，确认目标宏改写没影响分析本身："
echo "       cd ../07-capability-boundary && ./run-boundary.sh tp default    # 预期 18/22"
