#!/usr/bin/env bash
# 受限路线一把梭：00 → 10 → 20 →（25 条件）→ 30 → 35 → 40 → 50。
#
# 用法（干净窗口，从零开始）：
#   cd <调研包>/04-setup/scripts
#   ./run-all.sh
#
# 可调的环境变量（都有默认值，不设也能跑）：
#   WORK=$HOME/infer-build     所有产物的落地目录
#   J=$(nproc)                 编译并行度（内存 < 8 GB 时调小）
#   DLJ=4                      opam 下载并发（核多时压低可避开代理限流）
#   FROM=30                    从某一步开始（00/10/20/25/30/35/40/50），断点续跑用
#   PREFETCH=1                 跑条件步骤 25（默认不跑）
#   SKIP_CHECKER=1             只构建 infer 本体，不装 MiniOobChecker
#   INFER_REF=<sha|tag|main>   不用调研基线，换别的 infer commit（见 _baseline.sh）
#
# 行为：
#   · 开跑前先打印完整的执行计划：哪些步骤会跑、哪些跳过、为什么、各要多久；
#   · 每一步打 [第几步/共几步] 标题，并告诉你这一步单独执行的命令；
#   · 每一步的完整输出同时写进日志目录；
#   · **任何一步失败立即停下**，告诉你是哪一步、怎么单独重跑它、修好后怎么接着跑；
#   · 结束（或失败）时打印一张汇总表：每步的结果与耗时。
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
export WORK="${WORK:-$HOME/infer-build}"
export J="${J:-$(nproc)}"
export DLJ="${DLJ:-4}"
FROM="${FROM:-00}"

# _progress.sh 提供 _fmt_dur，并在这里完成一次 **CA 证书环境变量自检**：
# SSL_CERT_FILE / CURL_CA_BUNDLE / REQUESTS_CA_BUNDLE 指向不存在的文件时自动 unset
# （实测：残留的 /tmp/_MEIxxxx/certifi/cacert.pem 会让 curl 报 (77)）。
# unset 只作用于本进程及其子进程，不影响你别的窗口。
. "$HERE/_progress.sh"
. "$HERE/_baseline.sh"

# ---------------------------------------------------------------- 步骤表
#   编号 | 脚本 | 标题 | 预计耗时 | 失败是否致命
STEPS=(
  "00|00-check-env.sh|环境体检（只看不改）|~1 分钟|no"
  "10|10-install-opam.sh|装 opam 本体 + GMP 头文件 + rsync 替身|~2 分钟|yes"
  "20|20-prepare-opam-repo.sh|准备 opam 仓库 + clone infer 并切到调研基线|~10 分钟|yes"
  "25|25-prefetch-git-sources.sh|预取 git 源到本地镜像（条件步骤）|10–30 分钟|no"
  "30|30-install-deps.sh|装 OCaml 5.4.1+flambda 和全部依赖（★最慢）|20–55 分钟|yes"
  "35|35-fetch-prebuilt.sh|取官方预编译包（借它的 clang）|2–5 分钟|yes"
  "40|40-build-infer.sh|构建 infer 本体|~13 分钟|yes"
  "50|50-install-minioobchecker.sh|装 MiniOobChecker 并跑 demo|~5 分钟|yes"
)

case "$FROM" in
  00|10|20|25|30|35|40|50) : ;;
  *) echo "✗ FROM 只能是 00/10/20/25/30/35/40/50，收到的是 '$FROM'" >&2; exit 2 ;;
esac

# 每一步：要跑还是跳过、跳过的原因
declare -a S_ID S_SCRIPT S_TITLE S_EST S_FATAL S_PLAN S_WHY S_RESULT S_DUR
N_RUN=0
for i in "${!STEPS[@]}"; do
  IFS='|' read -r id script title est fatal <<<"${STEPS[$i]}"
  S_ID[$i]=$id; S_SCRIPT[$i]=$script; S_TITLE[$i]=$title; S_EST[$i]=$est; S_FATAL[$i]=$fatal
  S_RESULT[$i]=""; S_DUR[$i]=""
  if [ "${id#0}" -lt "${FROM#0}" ]; then
    S_PLAN[$i]=skip; S_WHY[$i]="FROM=$FROM，之前的步骤不跑"
  elif [ "$id" = 25 ] && [ -z "${PREFETCH:-}" ]; then
    S_PLAN[$i]=skip; S_WHY[$i]="条件步骤；30 大面积报 HTTP 403 时再用 PREFETCH=1 打开"
  elif [ "$id" = 50 ] && [ -n "${SKIP_CHECKER:-}" ]; then
    S_PLAN[$i]=skip; S_WHY[$i]="SKIP_CHECKER=1"
  else
    S_PLAN[$i]=run; S_WHY[$i]=""; N_RUN=$((N_RUN+1))
  fi
done

LOGDIR="$WORK/logs/run-all-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGDIR"

# ---------------------------------------------------------------- 打印计划
bold(){ printf '\033[1m%s\033[0m' "$1"; }
# pad <文本> <显示宽度>：按终端显示宽度补空格。
# 中文在终端里占两列，而 bash 的 printf %-Ns 按字节算宽度，直接用会错位。
pad() {
  python3 -c '
import sys, unicodedata as u
s, n = sys.argv[1], int(sys.argv[2])
w = sum(2 if u.east_asian_width(c) in "WF" else 1 for c in s)
sys.stdout.write(s + " " * max(1, n - w))' "$1" "$2" 2>/dev/null || printf '%s ' "$1"
}
echo
bold "Infer 受限网络构建 —— 执行计划"; echo
echo "  WORK=$WORK   J=$J   DLJ=$DLJ   FROM=$FROM"
if [ "$INFER_REF" = "$INFER_BASELINE" ]; then
  echo "  infer 源码：调研基线 ${INFER_BASELINE:0:12}（MiniOobChecker 补丁就是在它上面验证的）"
else
  echo "  infer 源码：INFER_REF=$INFER_REF  ⚠ 不是调研基线 ${INFER_BASELINE:0:12}"
fi
echo
k=0
for i in "${!STEPS[@]}"; do
  if [ "${S_PLAN[$i]}" = run ]; then
    k=$((k+1))
    printf '  [%d/%d]  %s  %s%s\n' "$k" "$N_RUN" "${S_ID[$i]}" "$(pad "${S_TITLE[$i]}" 46)" "${S_EST[$i]}"
  else
    printf '   ——    %s  %s跳过：%s\n' "${S_ID[$i]}" "$(pad "${S_TITLE[$i]}" 46)" "${S_WHY[$i]}"
  fi
done
echo
echo "  共 $N_RUN 步。全程 2 核约 75 分钟、8 核约 35 分钟；磁盘预留 ≥ 20 GB。"
echo "  每步的完整输出在： $LOGDIR/"
echo "  任何一步失败都会立即停下，并告诉你怎么单独重跑那一步。"

# ---------------------------------------------------------------- 汇总表
summary() {
  echo
  bold "════════ 汇总 ════════"; echo
  for i in "${!STEPS[@]}"; do
    local r="${S_RESULT[$i]}" mark
    case "$r" in
      ok)    mark="✓ 成功" ;;
      warn)  mark="! 失败（非致命，已继续）" ;;
      fail)  mark="✗ 失败" ;;
      "")    if [ "${S_PLAN[$i]}" = skip ]; then mark="— 跳过"; else mark="· 未执行"; fi ;;
    esac
    printf '  %s  %s%s%s\n' "${S_ID[$i]}" "$(pad "${S_TITLE[$i]}" 46)" "$(pad "$mark" 26)" "${S_DUR[$i]}"
  done
  echo "  日志： $LOGDIR/"
}

# ---------------------------------------------------------------- 逐步执行
k=0
for i in "${!STEPS[@]}"; do
  [ "${S_PLAN[$i]}" = run ] || continue
  k=$((k+1))
  id=${S_ID[$i]}; script=${S_SCRIPT[$i]}; title=${S_TITLE[$i]}
  log="$LOGDIR/${script%.sh}.log"

  echo
  printf '\033[1m════════ [%d/%d] %s  %s ════════\033[0m\n' "$k" "$N_RUN" "$id" "$title"
  echo "  单独执行这一步： cd $HERE && ./$script"
  echo "  日志： $log"
  echo

  t0=$(date +%s)
  set +e
  "$HERE/$script" 2>&1 | tee "$log"
  rc=${PIPESTATUS[0]}
  set -e
  S_DUR[$i]=$(_fmt_dur $(( $(date +%s) - t0 )))

  if [ "$rc" -eq 0 ]; then
    S_RESULT[$i]=ok
    continue
  fi

  if [ "${S_FATAL[$i]}" = no ]; then
    S_RESULT[$i]=warn
    echo
    echo "  ! 第 $k/$N_RUN 步（$id）返回 $rc，这一步失败不影响后续，继续。"
    [ "$id" = 00 ] && echo "    （体检有 ✗ 很正常，它只是告诉你环境里缺什么、后面的脚本会怎么处理。）"
    continue
  fi

  # ------------------------------------------------ 致命失败：停下
  S_RESULT[$i]=fail
  echo
  printf '\033[1;31m✗ 第 %d/%d 步失败：%s  %s（退出码 %d，用时 %s）\033[0m\n' \
         "$k" "$N_RUN" "$id" "$title" "$rc" "${S_DUR[$i]}" >&2
  cat >&2 <<EOF

  后续步骤已全部停止。建议这样处理：

    1) 先单独执行这一步，看清楚报错（上面的输出和日志里都有）：
         cd $HERE
         export WORK=$WORK J=$J DLJ=$DLJ
         . ./opam-env.sh
         ./$script

    2) 修好之后，从这一步接着跑完剩下的：
         FROM=$id ./run-all.sh

    3) 对照排障表：04-setup/01-restricted-network-build.md「常见故障与对策」

  这一步的完整日志： $log
EOF
  # 特定失败的针对性提示
  if [ "$id" = 30 ] && grep -q "Could not synchronize" "$log" 2>/dev/null; then
    cat >&2 <<EOF

  ▸ 日志里有 "Could not synchronize"：这是**取源被拦**，不是编译失败。
    先把源预取到本地，让 opam 完全不碰网络：
         PREFETCH=1 FROM=25 ./run-all.sh
    还不行再压并发：
         DLJ=1 PREFETCH=1 FROM=25 ./run-all.sh
EOF
  fi
  if [ "$id" = 35 ]; then
    cat >&2 <<EOF

  ▸ 预编译包下不下来时，可以在别的机器下好拷过来：
         PREBUILT_TARBALL=/path/to/infer-linux-x86_64-v1.3.0.tar.xz ./35-fetch-prebuilt.sh
         FROM=40 ./run-all.sh
EOF
  fi
  summary >&2
  exit "$rc"
done

# ---------------------------------------------------------------- 全部完成
summary
INFER_BIN="$WORK/infer/infer/bin/infer"
echo
bold "全部完成"; echo
if [ -x "$INFER_BIN" ]; then
  echo "  $("$INFER_BIN" --version 2>/dev/null | head -1)"
  echo "  $INFER_BIN"
fi
cat <<EOF

  在任何窗口里用它（每个新窗口都要）：
      export PATH=$WORK/infer/infer/bin:\$PATH
      infer --version

  想手敲 opam 命令时（每个新窗口都要）：
      . $HERE/opam-env.sh
EOF
