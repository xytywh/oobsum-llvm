#!/usr/bin/env bash
# 受限路线第 2 步：用 git clone 拿到 opam 仓库，把"只能 HTTP 下载压缩包"的源改写成
# "能 git clone 的源"，再把少数只能从 raw.githubusercontent 拿的小文件通过 git 取出来、
# 按官方声明的 sha256 塞进 opam 下载缓存（校验依然生效）。
#
# 【v2.6 修复】顺序错了会直接导致 opam init 被跳过：
#   旧版把"预填下载缓存"放在 opam init 之前，缓存目录一建出来，
#   `[ -d "$OPAMROOT" ] || opam init ...` 这个判断就成立，opam init 被整个跳过，
#   随后 `opam update` 对着一个无效 root 报
#   "exists, but does not appear to be a valid opam root"。
#   现在顺序改成：clone → 改写 → opam init → 预填缓存 → update → 自检。
#   并且对"已经踩进坑里"的目录做自动修复（把 download-cache 挪开、init 完再挪回来）。
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"

WORK="${WORK:-$HOME/infer-build}"
export PATH="$WORK/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
export OPAMYES=1 OPAMCOLOR=never
REPO="$WORK/opam-repository"

command -v opam >/dev/null || { echo "找不到 opam，请先跑 ./10-install-opam.sh 并 export PATH=$WORK/bin:\$PATH" >&2; exit 2; }

step_total 8

# ----------------------------------------------------------------------
step "clone opam-repository（约 1 GB / 19000+ 个 opam 文件）"
if [ -d "$REPO/.git" ]; then
  echo "   已存在，跳过 clone：$REPO"
  echo "   （想重新来过就先 rm -rf $REPO）"
else
  # --progress 让 git 在非 tty 下也打进度
  git clone --depth 1 --progress https://github.com/ocaml/opam-repository "$REPO" 2>&1 \
    | sed -u 's/^/   /'
fi
echo "   仓库大小: $(du -sh "$REPO" 2>/dev/null | cut -f1)   opam 文件数: $(find "$REPO/packages" -name opam 2>/dev/null | wc -l)"

# ----------------------------------------------------------------------
step "把 GitHub 压缩包源改写成 git 源"
python3 "$HERE/rewrite_opam_urls.py" "$REPO"

# ----------------------------------------------------------------------
step "把 erratique.ch 的源改写到 GitHub 镜像（fmt/fpath/astring/cmdliner…）"
python3 "$HERE/rewrite_erratique.py" "$REPO"

# ----------------------------------------------------------------------
# 【v3.0】把源改成 git 之后，opam 的内置变量 dev 从 false 变成 true，
# 一些平时走不到的构建分支被激活，个别包会露馅（实测：fpath 的 topkg --dev-pkg 写法）。
# 这不是我们改错了，是上游那条分支本来就没人测过。统一在这里打补丁。
step "修正「源改成 git 后才会暴露」的兼容性问题"
# QUIET_NEXT：脚本 20 自己会在最后 opam update，不用再提示用户手动跑
QUIET_NEXT=1 python3 "$HERE/patch_git_source_quirks.py" "$REPO"

# ----------------------------------------------------------------------
step "opam init（必须在预填缓存之前！见文件头的说明）"
if [ -f "$OPAMROOT/config" ]; then
  echo "   opam root 已存在且有效，跳过 init：$OPAMROOT"
else
  # 自动修复"旧版脚本留下的半成品 root"：里面只有 download-cache，没有 config
  STASH=""
  if [ -d "$OPAMROOT" ]; then
    echo "   !! $OPAMROOT 已存在但不是有效的 opam root"
    if [ -d "$OPAMROOT/download-cache" ]; then
      STASH="$WORK/.download-cache.stash.$$"
      echo "   -> 先把已经预填好的 download-cache 挪到 $STASH，init 完再挪回来"
      mv "$OPAMROOT/download-cache" "$STASH"
    fi
    # 此时目录里若还剩别的东西，说明不是我们造成的，交给用户判断
    if [ -n "$(ls -A "$OPAMROOT" 2>/dev/null)" ]; then
      echo "   !! 目录里还有其它内容，不敢自动删。请人工检查后 rm -rf $OPAMROOT 再重跑：" >&2
      ls -A "$OPAMROOT" | sed 's/^/      /' >&2
      exit 2
    fi
    rmdir "$OPAMROOT"
  fi
  echo "   opam init --bare --disable-sandboxing ..."
  opam init --bare --disable-sandboxing --no-setup -y default "$REPO" 2>&1 | sed -u 's/^/   /'
  if [ -n "$STASH" ]; then
    mkdir -p "$OPAMROOT"
    mv "$STASH" "$OPAMROOT/download-cache"
    echo "   -> download-cache 已挪回"
  fi
fi
[ -f "$OPAMROOT/config" ] || { echo "opam init 没有产出有效的 root，停在这里。" >&2; exit 1; }

# ----------------------------------------------------------------------
# 【v2.7 修复】预填缓存要读 infer 的 lock 文件才知道该预填哪些包，
# 而 infer 源码原本是脚本 30 才 clone 的 —— 顺序又错了一次。
# 现在把 clone 提到这里（它很快，而且 20 自此自洽，不依赖 30）。
#
# 【v3.3】clone 之后**立刻切到调研基线 commit**（见 _baseline.sh 的说明）。
#   必须在这里切、不能等到 50 打补丁前才切：后面每一步都读 infer 的 lock 文件定依赖版本。
step "clone infer 源码并切到调研基线（预填缓存要读它的 opam lock）"
. "$HERE/_baseline.sh"
INFER="${INFER:-$WORK/infer}"
if [ -e "$INFER/.git" ]; then
  echo "   已存在：$INFER"
else
  git clone --progress https://github.com/facebook/infer.git "$INFER" 2>&1 | sed -u 's/^/   /'
  # 上面的管道会吞掉 git 的退出码，所以用结果判断
  [ -e "$INFER/.git" ] || { echo "   ✗ clone infer 失败" >&2; exit 1; }
fi

cur=$(git -C "$INFER" rev-parse HEAD)
want=$(git -C "$INFER" rev-parse --verify -q "$INFER_REF^{commit}" 2>/dev/null || true)
if [ -z "$want" ]; then
  # 本地没有这个 commit（比如 INFER_REF 指向一个更新的 sha），尝试单独 fetch
  echo "   本地没有 $INFER_REF，尝试 git fetch ..."
  git -C "$INFER" fetch -q origin "$INFER_REF" 2>/dev/null || git -C "$INFER" fetch -q origin 2>/dev/null || true
  want=$(git -C "$INFER" rev-parse --verify -q "$INFER_REF^{commit}" 2>/dev/null || \
         git -C "$INFER" rev-parse --verify -q FETCH_HEAD 2>/dev/null || true)
  [ -n "$want" ] || { echo "   ✗ 找不到 commit：$INFER_REF" >&2; exit 1; }
fi

if [ "$cur" = "$want" ]; then
  echo "   ✓ 已在目标 commit：${want:0:12}$([ "$want" = "$INFER_BASELINE" ] && echo '（调研基线）')"
elif [ -z "$(git -C "$INFER" status --porcelain)" ]; then
  # 工作区干净 = 没有任何本地改动会丢，直接切
  git -C "$INFER" -c advice.detachedHead=false checkout -q "$want"
  echo "   ✓ 已切换：${cur:0:12} → ${want:0:12}$([ "$want" = "$INFER_BASELINE" ] && echo '（调研基线）')"
else
  # 工作区有改动（多半是之前跑过 45 摘 Rust 前端、或 50 打过补丁），不替你动
  cat >&2 <<EOF

   ✗ 已有的 infer 源码在 ${cur:0:12}，不是目标 commit ${want:0:12}，而且工作区有本地改动：
$(git -C "$INFER" status --short | head -8 | sed 's/^/        /')
     （通常是之前跑 45 摘掉 Rust 前端、或 50 打进 MiniOobChecker 补丁留下的）

     本脚本不替你动有改动的工作区。三选一：

     a) 切回基线（推荐；改动会收进 stash，不会丢）：
          git -C $INFER stash -u
          git -C $INFER checkout $want
          FROM=20 $HERE/run-all.sh       # 20 之后的步骤都要按新 lock 重跑

     b) 维持现状、就用当前这个 commit：
          INFER_REF=HEAD $HERE/20-prepare-opam-repo.sh

     c) 推倒重来：
          rm -rf $INFER && $HERE/20-prepare-opam-repo.sh
EOF
  exit 1
fi
if [ "$want" != "$INFER_BASELINE" ]; then
  echo "   ! 注意：当前不是调研基线 ${INFER_BASELINE:0:12}（INFER_REF=$INFER_REF）。"
  echo "     MiniOobChecker 补丁和报告里的数据都是在基线上验证的，换 commit 后以实测为准。"
fi
LOCK="$INFER/opam/infer.opam.locked"
if [ -f "$LOCK" ]; then
  echo "   lock 文件: $LOCK（$(grep -c '{=' "$LOCK" 2>/dev/null || echo '?') 个 pin 住的包）"
else
  echo "   !! 找不到 $LOCK —— 下一步会退化成只预填编译器相关的包（不致命）"
fi

# ----------------------------------------------------------------------
step "把 raw.githubusercontent 上的小文件预填进 opam 下载缓存"
OPAMROOT="$OPAMROOT" REPO="$REPO" LOCK="$LOCK" python3 "$HERE/populate_opam_cache.py"

# ----------------------------------------------------------------------
step "opam update + 自检"
heartbeat_start "opam update 中"
opam update default 2>&1 | tail -3 | sed 's/^/   /'
heartbeat_stop

echo
echo "自检："
ok=1
if [ -f "$OPAMROOT/config" ]; then echo "  ✓ opam root 有效：$OPAMROOT"; else echo "  ✗ opam root 无效"; ok=0; fi
n_pkg=$(opam list -A --short 2>/dev/null | wc -l || echo 0)
if [ "$n_pkg" -gt 1000 ]; then echo "  ✓ 仓库可见 $n_pkg 个包"; else echo "  ✗ 仓库只看到 $n_pkg 个包（应该上万）"; ok=0; fi
n_cache=$(find "$OPAMROOT/download-cache" -type f 2>/dev/null | wc -l || echo 0)
echo "  · 下载缓存已预填 $n_cache 个文件"

step_done

if [ "$ok" -eq 1 ]; then
  echo "OK。OPAMROOT=$OPAMROOT"
  echo "下一步： ./30-install-deps.sh   （infer 源码已 clone 到 $INFER，30 会直接用）"
else
  echo "有检查项没过，先别往下走。把上面的输出贴出来。" >&2
  exit 1
fi
