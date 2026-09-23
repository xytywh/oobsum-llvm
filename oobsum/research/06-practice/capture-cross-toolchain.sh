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

# C++ 标准头来源：改用 gcc-14.3.0 自带 libstdc++（而非 infer 自带 libc++/v1）。
# 原因：实测 --clang-libcxx-include-to-override-regex 不会把 cdb 里的
# -I c++/14.3.0 重定向到 c++/v1；而源码本身按 libstdc++ 编写，直接复用
# gcc 自带 libstdc++ 头路径更贴合原工具链，无需额外改写。
# 下方三个 -I 依次为：主头目录、平台相关头（32 位 ABI）、向后兼容头。
GCC_CXX_INC_BASE="/opt/buildtools/gcc-14.3.0/include/c++/14.3.0"
if [[ ! -d "$GCC_CXX_INC_BASE" ]]; then
  echo "警告: gcc libstdc++ 头目录不存在: $GCC_CXX_INC_BASE" >&2
  echo "      C++ 标准头将无法找到，C++ 文件会 capture 失败" >&2
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
# 参数解释见配套文档「方案 A」表格（v3.9 修订）：
#   --Xclang "--target=aarch64_be-linux-gnu"  aarch64 大端 target（不带 _ilp32 后缀，
#                                             因 clang-21 不识别 aarch64_be ilp32 ABI，
#                                             只会被当作 aarch64_be 64 位处理）
#   --Xclang "-mbig-endian"           大端
#   --Xclang "-m32"                   切换到 32 位指针（__SIZEOF_POINTER__=4）
#                                     【根因修复】clang-21 不支持 aarch64_be ilp32 ABI，
#                                     --target=aarch64_be-...-ilp32 仍按 64 位指针处理，
#                                     导致 reinterpret_cast<UINT32>(ptr) 报硬错误
#                                     "cast from pointer to smaller type"，无法用 -W 降级。
#                                     -m32 让 clang 切换到 ILP32 数据模型（指针 4 字节），
#                                     自动定义 __ILP32__ 并撤销 __LP64__，
#                                     一次解决 cast / initializer-constant / LIRAF_OFFSET
#                                     三大类硬错误（约 2300+ 条 error）。
#                                     副作用：__aarch64__ 不再定义，改为 __arm__；
#                                     实测 hert_crsp 源码不依赖 __aarch64__，无影响。
#
#                                     【与真 ILP32 的差异及取舍】
#                                     原始工具链 aarch64_be-linux-gnuilp32-gcc 用
#                                     -mabi=ilp32，底层仍是 aarch64 架构。
#                                     -m32 与真 ILP32 有 3 处宏差异：
#                                       __SIZE_TYPE__:   unsigned int  vs long unsigned int
#                                       __PTRDIFF_TYPE__: int          vs long int
#                                       __aarch64__:    undefined     vs defined（__arm__ 反之）
#                                     三者大小一致（4 字节），仅类型身份 / 架构宏不同。
#                                     曾尝试用 -U/-D 纠正 __SIZE_TYPE__ 等，但 -m32 下
#                                     clang 的 operator new / size_t 内建签名已锁定为
#                                     unsigned int，手动改宏会导致 bss_new.h 等头文件中
#                                     "operator new takes type size_t (unsigned int)" 报错，
#                                     得不偿失。对 infer capture（AST 提取 + CFG 构建）而言，
#                                     类型身份差异不影响分析结果，故保留 -m32 原样不做纠正。
#                                     sysroot 头文件（bits/wordsize.h 等）用 __arm__ 和
#                                     __aarch64__（不带 LP64）均走 __MHWORDSIZE=32 分支，
#                                     不会选错路径。
#
#   --Xclang "-D__AARCH64EB__"        补定义大端宏。sysroot 的 gnu/stubs-32.h 用
#                                     __AARCH64EB__ 选择 stubs-ilp32_be.h（存在），
#                                     否则会选 stubs-ilp32.h（不存在）。
#   --Xclang "-D__uint128_t=..."      伪定义 128 位整数类型。-m32 切换到 32 位 ARM
#                                     后 __uint128_t 内建类型不可用，但 sysroot 的
#                                     asm/sigcontext.h / sys/user-32.h 仍引用它。
#                                     用 unsigned long long 替身足以让 AST 解析通过
#                                     （对静态分析 capture 无实质影响）。
#   --Xclang "--sysroot=..."          ilp32 sysroot，提供 bits/wordsize-32.h 等
#   --Xclang -I.../c++/14.3.0                          libstdc++ 主头目录
#                                                      （提供 <algorithm> <map> <set>
#                                                       <cstddef> <cstdint> <string> 等）
#   --Xclang -I.../c++/14.3.0/x86_64-pc-linux-gnu/32  平台相关头（32 位 ABI，
#                                                      提供 bits/c++config.h 等）
#   --Xclang -I.../c++/14.3.0/backward                 向后兼容头
#                                                      （提供 <backward/strtok_r.h> 等
#                                                       老式 GNU 扩展）
#   --clang-isystem-to-override-regex  失效 -isystem → infer 自带 clang 内部头
#                                      （提供 stddef.h / stdint.h 等，c++/v1 的
#                                       #include_next <stddef.h> 也靠它补全）
#   --keep-going                     单文件失败不中断
#   --progress-bar-style plain       非 tty 下只打印两行开场信息；逐文件的百分比 /
#                                    速率 / ETA 由本脚本的后台进度监控（查 capture.db）打印
echo "==> 开始 capture，日志: $LOG_FILE"
echo "==> 进度会实时显示在下方"
echo

# ------------------------------------------------------------------
# 进度监控：infer 的进度条检查 isatty(stderr)，通过 tee 管道后
# stderr 不再是 tty，进度条就不输出了。解决方案：
#  1. stderr 直接到终端（不经过 tee），保留 infer 原生进度条
#  2. stdout 写入日志文件
#  3. 后台启动一个监控进程，定期查询 capture.db 已落库源文件数，
#     输出到 stderr 给用户实时看到进度
#
# 【v3.9 修正，实测】infer capture 的全部输出（进度行、clang 诊断、
# "did not run successfully" 的失败命令转储）都走 stderr，stdout 一行也没有：
#     infer capture --compilation-database cdb.json --results-dir out \
#       --keep-going --progress-bar-style plain >so.txt 2>se.txt
#     wc -l so.txt   # 0
#     wc -l se.txt   # 45（含 error: 与失败命令）
# 所以只把 stdout 写进日志会得到一个空的 capture.log，第 4 步的统计全是 0，
# 看起来像"零错误"。下面恢复 2>&1 | tee，进度由上面的后台监控提供
# （--progress-bar-style plain 在非 tty 下只打印 "Capturing using
#  compilation database..." / "Starting translating N files" 两行，
#  没有逐文件进度，损失可以忽略）。
# ------------------------------------------------------------------

# 后台进度监控：每 10 秒查询 capture.db 已落库的源文件数
progress_monitor() {
  local db="$1"
  local total="$2"
  local start_time="$SECONDS"
  while true; do
    if [[ -f "$db" ]] && command -v sqlite3 >/dev/null 2>&1; then
      local done
      done=$(sqlite3 "$db" "SELECT COUNT(*) FROM source_files;" 2>/dev/null || echo 0)
      if [[ "$done" -gt 0 ]]; then
        local elapsed=$(( SECONDS - start_time ))
        local pct=0
        if [[ "$total" -gt 0 ]]; then
          pct=$(( done * 100 / total ))
        fi
        # 计算速率和预估剩余时间
        local rate=0
        local eta="--"
        if [[ "$elapsed" -gt 0 ]]; then
          rate=$(( done / elapsed ))
          if [[ "$rate" -gt 0 ]]; then
            local remain=$(( (total - done) / rate ))
            eta="${remain}s"
          fi
        fi
        printf '\r  [进度] %d / %d (%d%%)  速率: %d files/s  已用: %ds  预计剩余: %s   ' \
               "$done" "$total" "$pct" "$rate" "$elapsed" "$eta" >&2
      fi
    fi
    sleep 10
  done
}

# 启动进度监控（唯一源文件数作为分母）
MONITOR_PID=""
if [[ "$cdb_size" -gt 0 ]] && command -v sqlite3 >/dev/null 2>&1; then
  # 用唯一源文件数做分母（cdb 可能含重复条目）
  unique_srcs=$(python3 -c "
import json,os,sys
cdb=json.load(open(sys.argv[1]))
srcs=set(os.path.normpath(e.get('file','')) for e in cdb if e.get('file'))
print(len(srcs))
" "$CDB")
  progress_monitor "$RESULTS_DIR/capture.db" "$unique_srcs" &
  MONITOR_PID=$!
  # v3.9：脚本异常退出（set -e 触发、Ctrl-C）时也要收掉后台监控，
  # 否则 while true 的监控进程会留在后台一直跑。
  trap 'kill "$MONITOR_PID" 2>/dev/null || true' EXIT INT TERM
fi

set +e
# stdout 与 stderr 一起进日志（见上面【v3.9 修正】：infer 的诊断全在 stderr，
# stdout 为空；只收 stdout 会得到空日志和全 0 的统计）。
# tee 同时显示到终端；退出码取管道第一段（infer 本身），不是 tee 的。
"$INFER_BIN" capture \
  --compilation-database "$CDB" \
  --results-dir "$RESULTS_DIR" \
  --keep-going \
  --progress-bar-style plain \
  --Xclang "--target=aarch64_be-linux-gnu" \
  --Xclang "-mbig-endian" \
  --Xclang "-m32" \
  --Xclang "-D__AARCH64EB__" \
  --Xclang "-D__uint128_t=unsigned long long" \
  --Xclang "--sysroot=$SYSROOT" \
  --Xclang "-I$GCC_CXX_INC_BASE" \
  --Xclang "-I$GCC_CXX_INC_BASE/x86_64-pc-linux-gnu/32" \
  --Xclang "-I$GCC_CXX_INC_BASE/backward" \
  --clang-isystem-to-override-regex "$STALE_PATH_REGEX" \
  2>&1 | tee "$LOG_FILE"
RC=${PIPESTATUS[0]}
set -e

# 停止进度监控
if [[ -n "$MONITOR_PID" ]]; then
  kill "$MONITOR_PID" 2>/dev/null || true
  wait "$MONITOR_PID" 2>/dev/null || true
  printf '\r%*s\r' 120 '' >&2  # 清除进度行
fi

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
  clang_fail=$(grep -c 'did not run successfully' "$LOG_FILE" || true)
  printf "fatal error 行数:             %d\n" "$fatal_lines"
  printf "error 行数（含 fatal）:       %d\n" "$err_lines"
  printf "warning 行数:                 %d\n" "$warn_lines"
  printf "Frontend error（infer 崩溃）: %d\n" "$fe_lines"
  printf "clang 命令失败数:             %d\n" "$clang_fail"
  echo

  # clang 命令失败涉及的源文件数
  # infer 日志中每个失败命令块包含 "-x" "c++"/"c" "<source_path>"，
  # 从中提取源文件路径。这是 clang 返回非零的文件数（有 error 但仍可能落库）。
  # 【注意】"too many errors emitted" 是 clang 超 error-limit 的终止提示，
  # 不带文件路径前缀，因此不能用 ^/path: 模式统计。
  # --keep-going 下 infer 仍会用已恢复的 AST 落库，文件不一定缺失。
  # Frontend error 是 infer 前端插件在某几个方法上 assert 崩溃，按方法跳过，
  # 不影响同文件其他方法，文件本身仍会落库。
  clang_fail_files=$(python3 -c "
import re,sys
log=open(sys.argv[1],encoding='utf-8',errors='replace').read()
# 按 'did not run successfully' 分段，每段提取属于该失败命令的源文件
pat=re.compile(r'\"-x\" \"(?:c\+\+|c|cxx|cc|C)\"\s+\"([^\"]+)\"')
blocks=log.split('did not run successfully')
fail_files=set()
for block in blocks[1:]:
    matches=pat.findall(block)
    if matches:
        fail_files.add(matches[0])
print(len(fail_files))
" "$LOG_FILE")
  fe_files=$(grep -E 'Aborting translation of method .* in file' "$LOG_FILE" \
    | grep -oE "file '[^']+'" | sort -u | wc -l)
  echo "受影响源文件数（下限，不代表完全失败）"
  echo "----------------------------------------"
  printf "clang 命令失败涉及源文件:    %d\n" "$clang_fail_files"
  printf "Frontend error 涉及源文件:   %d\n" "$fe_files"
  echo "（这些文件大多仍已部分落库，只是个别方法被跳过）"
  echo

  echo "前 20 条去重 fatal error"
  echo "------------------------"
  grep 'fatal error:' "$LOG_FILE" \
    | grep -E '^/[^:]+:[0-9]+:[0-9]+:' \
    | sed -E "s|^[^:]+:[0-9]+:[0-9]+:||" \
    | sort -u \
    | head -20
  # 如果没有带路径的 fatal error（如 "too many errors" 不带路径），直接去重输出
  if ! grep 'fatal error:' "$LOG_FILE" | grep -qE '^/[^:]+:[0-9]+:[0-9]+:'; then
    grep 'fatal error:' "$LOG_FILE" | sort -u | head -20
  fi
  echo

  echo "前 20 条去重 error（非 fatal）"
  echo "------------------------------"
  grep 'error:' "$LOG_FILE" \
    | grep -v 'fatal error:' \
    | grep -v 'Frontend error' \
    | grep -v 'did not run successfully' \
    | grep -v 'Error: the following' \
    | grep -E '^/[^:]+:[0-9]+:[0-9]+:' \
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
