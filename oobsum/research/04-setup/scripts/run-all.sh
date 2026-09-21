#!/usr/bin/env bash
# 受限路线一把梭：00 → 10 → 20 →（25 条件）→ 30 → 预编译包 → 40 → 50。
#
# 用法（干净窗口，从零开始）：
#   cd <调研包>/04-setup/scripts
#   ./run-all.sh
#
# 可调的环境变量（都有默认值，不设也能跑）：
#   WORK=$HOME/infer-build     所有产物的落地目录
#   J=$(nproc)                 编译并行度
#   DLJ=4                      opam 下载并发（核多时压低可避开代理限流）
#   SKIP_CHECKER=1             只构建 infer 本体，不装 MiniOobChecker
#   FROM=30                    从某一步开始（00/10/20/25/30/40/50），断点续跑用
#
# 关于中断：每个子脚本本身都可重跑、会跳过已完成的部分。
# 所以任何一步挂了，修完之后直接 ./run-all.sh 再跑一遍即可，不用从头来。
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
export WORK="${WORK:-$HOME/infer-build}"
export J="${J:-$(nproc)}"
export DLJ="${DLJ:-4}"
FROM="${FROM:-00}"
INFER="$WORK/infer"

# _progress.sh 只为了拿 _fmt_dur / step 这些；run-all 自己管步骤号
. "$HERE/_progress.sh"

case "$FROM" in
  00|10|20|25|30|40|50) : ;;
  *) echo "✗ FROM 只能是 00/10/20/25/30/40/50，收到的是 '$FROM'" >&2; exit 2 ;;
esac

want() {   # want <步号> —— FROM 之前的步骤跳过
  [ "${1#0}" -ge "${FROM#0}" ] 2>/dev/null || return 1
}

banner() {
  printf '\n\033[1m════════ %s ════════\033[0m\n' "$1"
}

fail() {
  echo >&2
  echo "✗ 在「$1」这一步失败了。" >&2
  echo "  修完之后从这一步继续：  FROM=$2 ./run-all.sh" >&2
  echo "  排查看： ../01-restricted-network-build.md 的「常见故障与对策」" >&2
  exit 1
}

echo "WORK=$WORK   J=$J   DLJ=$DLJ   FROM=$FROM"
echo "全程 2 核约 75 分钟、8 核约 35 分钟；磁盘预留 ≥ 20 GB。"

# ---------------------------------------------------------------- 00
if want 00; then
  banner "00 环境体检（只看不改）"
  "$HERE/00-check-env.sh" || true      # 体检有 ✗ 是正常的，不中断
fi

# ---------------------------------------------------------------- 10
if want 10; then
  banner "10 装 opam 本体 + GMP 头文件 + rsync 替身"
  "$HERE/10-install-opam.sh" || fail "10-install-opam.sh" 10
fi

# 从这里开始所有步骤都需要 PATH / OPAMROOT
. "$HERE/opam-env.sh"

# ---------------------------------------------------------------- 20
if want 20; then
  banner "20 准备 opam 仓库（clone + 改写源 + 补丁 + init + 预填缓存）"
  "$HERE/20-prepare-opam-repo.sh" || fail "20-prepare-opam-repo.sh" 20
fi

# ---------------------------------------------------------------- 25（条件步骤）
# 25 默认**不跑**。它要串行 clone 一两百个仓库，本身很慢，
# 只有当 30 大面积报 HTTP 403（而手工 git clone 是通的）时才值得。
# 想强制跑： PREFETCH=1 ./run-all.sh
if want 25 && [ -n "${PREFETCH:-}" ]; then
  banner "25 预取 git 源到本地镜像（PREFETCH=1 显式要求）"
  if "$HERE/25-prefetch-git-sources.sh"; then
    echo "   ✓ 预取完成"
  else
    echo "   ! 预取没跑成功——继续走 30，看 30 是不是真的需要它。"
  fi
elif want 25; then
  echo
  echo "（跳过 25-prefetch-git-sources.sh —— 条件步骤，只在 30 报 HTTP 403 时才需要。"
  echo "  真遇上了就： PREFETCH=1 FROM=25 ./run-all.sh）"
fi

# ---------------------------------------------------------------- 30
if want 30; then
  banner "30 装 OCaml 5.4.1+flambda 和全部 opam 依赖（★最慢）"
  if ! "$HERE/30-install-deps.sh"; then
    if grep -q "Could not synchronize" "$WORK/.deps-install.log" 2>/dev/null; then
      echo >&2
      echo "  日志里有 \"Could not synchronize\" —— 这是取源被拦，不是编译失败。" >&2
      echo "  下一招是把源预取到本地，让 opam 完全不碰网络：" >&2
      echo "      PREFETCH=1 FROM=25 ./run-all.sh" >&2
      echo "  还不行再压并发：  DLJ=1 PREFETCH=1 FROM=25 ./run-all.sh" >&2
    fi
    fail "30-install-deps.sh" 30
  fi
fi

# ---------------------------------------------------------------- 预编译包
if want 40; then
  banner "40a 取官方预编译包（借它的 clang，省掉数小时 LLVM 自举）"
  PB="$WORK/infer-linux-x86_64-v1.3.0"
  URL=https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
  if [ -d "$PB/lib/infer/facebook-clang-plugins/clang/install" ]; then
    echo "   已存在，跳过下载：$PB"
  else
    [ -s "$WORK/infer-bin.tar.xz" ] || {
      echo "   下载 $URL"
      heartbeat_start "下载中（约 200 MB）"
      curl -fL --retry 3 --retry-delay 5 -o "$WORK/infer-bin.tar.xz" "$URL" || {
        heartbeat_stop
        cat >&2 <<EOF

   ✗ 预编译包下不下来。这不是死路，但两条路都得选一条：
     a) 找台能上网的机器下好，拷到 $WORK/infer-bin.tar.xz，然后 FROM=40 ./run-all.sh
     b) 不用预编译包，从源码自举 LLVM/Clang —— 会多花几个小时：
          unset PREBUILT; $HERE/40-build-infer.sh
EOF
        exit 1
      }
      heartbeat_stop
    }
    echo "   解压到 $WORK"
    tar xf "$WORK/infer-bin.tar.xz" -C "$WORK"
  fi
  export PREBUILT="$PB"

  banner "40b 构建 infer 本体"
  "$HERE/40-build-infer.sh" || fail "40-build-infer.sh" 40
fi

# ---------------------------------------------------------------- 50
if want 50 && [ -z "${SKIP_CHECKER:-}" ]; then
  banner "50 装 MiniOobChecker 并跑 demo"
  "$HERE/50-install-minioobchecker.sh" || fail "50-install-minioobchecker.sh" 50
fi

banner "全部完成"
cat <<EOF
自建的 infer 在： $INFER/infer/bin/infer

把它加进 PATH（每个新窗口都要）：
    export PATH=$INFER/infer/bin:\$PATH
    infer --version

想手敲 opam 命令时（每个新窗口都要）：
    . $HERE/opam-env.sh
EOF
step_done
