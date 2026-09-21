#!/usr/bin/env bash
# 受限路线第 3 步：装 OCaml 编译器 + Infer 的全部 opam 依赖。最慢的一步（2 核约 55 分钟）。
set -eu
WORK="${WORK:-$HOME/infer-build}"
INFER="${INFER:-$WORK/infer}"
export PATH="$WORK/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
export OPAMYES=1 OPAMCOLOR=never
J="${J:-$(nproc)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"

# 前置检查：20 没跑成功就别往下走，否则会在半路以各种奇怪的方式失败
[ -f "$OPAMROOT/config" ] || {
  echo "✗ $OPAMROOT 不是有效的 opam root —— 先把 ./20-prepare-opam-repo.sh 跑成功。" >&2
  echo "  （**不要**手工 opam init 去「修」它：裸跑的 init 会在 ~/.opam 建出第二个 root，" >&2
  echo "    而且没带 --disable-sandboxing，缺 bubblewrap 的机器上还会再报一次错。）" >&2
  exit 2
}

# 有人在别的 shell 里裸敲过 opam init 的话，~/.opam 会冒出来。它不是我们的 root。
# 不致命，但会让后续排查一头雾水（两个 root，看到的包完全不同），所以明确说一声。
if [ -d "$HOME/.opam" ] && [ "$OPAMROOT" != "$HOME/.opam" ]; then
  echo "! 注意：$HOME/.opam 存在，但本脚本用的是 $OPAMROOT。"
  echo "  前者多半是某次裸跑 opam init 留下的半成品，确认没用后可 rm -rf 掉，免得混淆。"
fi
echo "这一步最慢：2 核约 55 分钟，8 核约 20 分钟。opam 自己会打每个包的进度。"
echo "中途断了可以直接重跑，opam 会跳过已装好的包。"

# 下载并发：opam 默认按 CPU 核数并发 git fetch。核多（比如 47 核）时
# 几十个 fetch 同时打到 github，很容易被代理限流成 HTTP 403。
# 默认压到 4；跑过 25-prefetch 的话网络都不走了，这个值也就无所谓。
#
# 【v2.9 修复】`--dl-jobs` **不是 `opam install` 的选项**（只有 opam init/update 等有），
#   opam 2.4 会直接报 `unknown option '--dl-jobs'` 并退出。
#   正确做法是走环境变量 OPAMDLJOBS —— 它对所有 opam 子命令都生效。
DLJ="${DLJ:-4}"
export OPAMDLJOBS="$DLJ"
# 同时写进全局配置，这样手工敲 opam 命令时也是同一个并发度（失败不致命）
opam option --global "dl-jobs=$DLJ" >/dev/null 2>&1 || true
if [ -d "$WORK/git-mirror" ]; then
  echo "检测到本地 git 镜像（$WORK/git-mirror），opam 基本不会走网络。"
else
  echo "提示：如果下面大面积报 \"Could not synchronize ... HTTP 403\"，"
  echo "      先跑 ./25-prefetch-git-sources.sh 把源拉到本地，再回来跑本脚本。"
fi
echo "下载并发 OPAMDLJOBS=$DLJ（想再压就 DLJ=1 ./30-install-deps.sh）"


[ -d "$INFER" ] || git clone https://github.com/facebook/infer.git "$INFER"

echo "== 关掉 depext（它会去调 apt，受限环境下只会卡住）"
opam option --global depext=false >/dev/null 2>&1 || true

echo "== 1/3 创建 switch 并装 OCaml 5.4.1+flambda（约 15 分钟）"
opam switch list 2>/dev/null | grep -q infer541 || opam switch create infer541 --empty -y
eval "$(opam env --switch=infer541 --set-switch)"
opam install -y -j"$J" --no-depexts ocaml-variants.5.4.1+options ocaml-option-flambda
ocamlopt -version

echo "== 2/3 pin 仓库内自带的依赖（charon / ppx_show / pyml / camlzip）"
cd "$INFER"
opam pin add --no-action -y ppx_show "$INFER/dependencies/ppx_show"
opam pin add --no-action -y pyml     "$INFER/dependencies/pyml"
opam pin add --no-action -y camlzip  "$INFER/dependencies/camlzip"
# charon 只有 Rust 分析需要，且它的依赖（visitors/unionFind/store）在 gitlab，受限环境常拿不到。
# 能拿到就 pin，拿不到就跳过并在第 40 步关掉 Rust 前端。
if opam pin add --no-action -y charon "$INFER/dependencies/charon" 2>/dev/null &&
   opam pin add --no-action -y name_matcher_parser "$INFER/dependencies/charon" 2>/dev/null; then
  LOCK="$INFER/opam/infer.opam.locked"; echo "  charon 已 pin"
else
  echo "  charon 不可用 → 生成一份去掉 Rust 依赖链的 lock 文件"
  LOCK="$WORK/infer-noRust.opam.locked"
  python3 - "$INFER/opam/infer.opam.locked" "$LOCK" <<'PY'
import re,sys
src,dst=sys.argv[1],sys.argv[2]
drop={'charon','name_matcher_parser','visitors','unionFind','store'}
out=[]
for line in open(src):
    m=re.match(r'\s*"([A-Za-z0-9_.+-]+)"\s*\{=\s*"([^"]+)"\}',line)
    if m and m.group(1) in drop: continue
    out.append(line)
open(dst,'w').write(''.join(out))
print("  已生成",dst)
PY
fi

echo "== 3/3 安装依赖（约 40 分钟，152 个包）"

# 【v2.9 新增】先 dry-run。几秒钟，能在开跑 40 分钟之前抓到：
#   · 参数写错（比如 v2.8 那个不存在的 --dl-jobs，直接让整轮白跑）
#   · 依赖解不开（缺包、版本冲突）
# 确认是 dry-run 特有的问题可以用 SKIP_DRYRUN=1 跳过。
if [ -z "${SKIP_DRYRUN:-}" ]; then
  echo "   先 dry-run 试算（几秒）..."
  if ! opam install --dry-run -y -j"$J" --no-depexts --deps-only "$LOCK" \
        >"$WORK/.deps-dryrun.log" 2>&1; then
    echo
    echo "   ✗ dry-run 就没过，真装肯定也不行。错误如下：" >&2
    tail -20 "$WORK/.deps-dryrun.log" | sed 's/^/      /' >&2
    echo >&2
    echo "   完整日志：$WORK/.deps-dryrun.log" >&2
    echo "   确认是 dry-run 特有的问题可以： SKIP_DRYRUN=1 ./30-install-deps.sh" >&2
    exit 1
  fi
  echo "   ✓ dry-run 通过，开始真装"
fi

set +e
opam install -y -j"$J" --no-depexts --deps-only "$LOCK" 2>&1 | tee "$WORK/.deps-install.log"
rc=${PIPESTATUS[0]}
set -e
if [ "$rc" -ne 0 ]; then
  n403=$(grep -c "Could not synchronize" "$WORK/.deps-install.log" || true)
  ndev=$(grep -c "dev-pkg=true: No value specified" "$WORK/.deps-install.log" || true)
  echo
  echo "✗ 装依赖失败（退出码 $rc）"
  if [ "${ndev:-0}" -gt 0 ]; then
    cat >&2 <<EOF

  日志里有 "--dev-pkg=true: No value specified"。
  这是**把源改成 git 之后才会出现**的问题：opam 的内置变量 dev 从 false 变成 true，
  激活了上游那条平时走不到、也没人测过的构建分支（老 topkg 不认 --dev-pkg=true 这种写法）。

  三条命令修好（注意第二行：**不 source 这个环境，裸敲 opam 会报
  "Opam has not been initialised"**，因为本套脚本的 root 不在 ~/.opam）：

      python3 $HERE/patch_git_source_quirks.py "$WORK/opam-repository"
      . $HERE/opam-env.sh && opam update default
      $HERE/30-install-deps.sh

  （v3.0 起脚本 20 会自动做这件事；你这个环境是在那之前建的，所以要手动补一次。）
EOF
  fi
  if [ "${n403:-0}" -gt 0 ]; then
    cat >&2 <<EOF

  日志里有 $n403 处 "Could not synchronize ... git fetch"。
  这是**取源失败**，不是编译失败。按顺序试：

    1) 确认手工 clone 其中一个能成功：
         git clone --depth 1 https://github.com/janestreet/ppx_base /tmp/_t && rm -rf /tmp/_t
       能成功 → 说明是 opam 那边的问题，走第 2 步；
       也失败 → 先解决 git 到 github 的连通性（代理是否对任意仓库放行）。

    2) 把源全部预取到本地，让 opam 不再碰网络：
         ./25-prefetch-git-sources.sh
         ./30-install-deps.sh

    3) 还不行就再压并发： DLJ=1 ./30-install-deps.sh

  完整日志：$WORK/.deps-install.log
EOF
  fi
  exit "$rc"
fi
echo "DEPS_OK"

step_done
