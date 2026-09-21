#!/usr/bin/env bash
# 第 5 步：把本调研包里的 MiniOobChecker 装进 infer 源码并重新构建。
#
# 【v3.3】三处加固：
#   1. 先核对 infer 源码是否在调研基线上（补丁是在基线上生成和验证的）；
#   2. 打补丁前先 `git apply --check` 试打，失败再退到 `--3way` 三方合并，
#      还不行就明确告诉你是哪几个文件在基线之后被上游改过；
#   3. 可重跑：补丁已经打过就跳过（旧版重跑会在 git apply 这一步直接报错）。
set -eu
WORK="${WORK:-$HOME/infer-build}"
INFER="${INFER:-$WORK/infer}"
PACK="$(cd "$(dirname "$0")/../.." && pwd)"     # 调研包根目录
export PATH="$WORK/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
J="${J:-$(nproc)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"
. "$HERE/_baseline.sh"
PATCH="$PACK/03-MiniOobChecker/patch/0001-register-minioob-checker.patch"

[ -e "$INFER/.git" ] || { echo "✗ 找不到 infer 源码：$INFER，先跑 20。" >&2; exit 2; }
[ -x "$INFER/infer/bin/infer" ] || { echo "✗ infer 还没构建过，先跑 40。" >&2; exit 2; }
eval "$(opam env --switch=infer541 --set-switch)"

step_total 4

# ----------------------------------------------------------------------
step "核对 infer 源码版本"
head=$(git -C "$INFER" rev-parse HEAD)
if [ "$head" = "$INFER_BASELINE" ]; then
  echo "   ✓ 在调研基线 ${INFER_BASELINE:0:12} 上 —— 补丁就是在这个 commit 上生成和验证的"
else
  n=$(git -C "$INFER" rev-list --count "$INFER_BASELINE..$head" 2>/dev/null || echo "?")
  echo "   ! infer 源码在 ${head:0:12}，比调研基线 ${INFER_BASELINE:0:12} 多 $n 个 commit。"
  # 补丁动到的文件里，有哪些在基线之后被上游改过 —— 这正是补丁可能打不上的原因
  files=$(grep '^+++ b/' "$PATCH" | sed 's|^+++ b/||')
  if ! git -C "$INFER" cat-file -e "$INFER_BASELINE^{commit}" 2>/dev/null; then
    echo "     （本地没有基线 commit —— 多半是浅克隆 —— 无法比较补丁文件是否被上游改过，直接试打）"
    touched="?"
  else
    touched=$(git -C "$INFER" diff --name-only "$INFER_BASELINE" "$head" -- $files 2>/dev/null || true)
  fi
  if [ "$touched" = "?" ]; then
    :
  elif [ -z "$touched" ]; then
    echo "     补丁涉及的 $(echo "$files" | wc -w) 个文件在这之间**都没被改过**，补丁可以安全打上。"
  else
    echo "     补丁涉及的文件里，这些在基线之后被上游改过（补丁可能打不上）："
    echo "$touched" | sed 's/^/        /'
  fi
fi

# ----------------------------------------------------------------------
step "拷入 MiniOobChecker 源码 + 打注册补丁"
cp "$PACK/03-MiniOobChecker/src/"MiniOob*.ml "$PACK/03-MiniOobChecker/src/"MiniOob*.mli \
   "$INFER/infer/src/labs/"
echo "   ✓ 4 个源文件已拷到 infer/src/labs/"
cd "$INFER"
pfiles=$(grep '^+++ b/' "$PATCH" | sed 's|^+++ b/||')
ntot=$(echo $pfiles | wc -w)
# 判断"已经打过"：补丁给 9 个文件各加了至少一行带 mini_oob / MiniOob 的代码。
# 不能只靠 git apply --reverse --check —— 经 --3way 合并打上的补丁，反向校验会因上下文漂移失败。
nmark=0
for f in $pfiles; do grep -qiE 'mini_?oob' "$f" 2>/dev/null && nmark=$((nmark+1)); done
if [ "$nmark" -eq "$ntot" ] || git apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "   ✓ 补丁之前已经打过（$ntot 个文件都已含 MiniOob 注册），跳过"
elif [ "$nmark" -gt 0 ]; then
  echo "   ✗ 补丁只打了一半：$ntot 个文件里只有 $nmark 个含 MiniOob 注册。" >&2
  echo "     多半是上次合并中途失败。回滚这些文件后重跑本脚本：" >&2
  echo "         git -C $INFER checkout HEAD -- $(echo $pfiles)" >&2
  exit 1
elif git apply --check "$PATCH" 2>/dev/null; then
  git apply "$PATCH"
  echo "   ✓ 补丁已打上"
else
  # 直接打不上。先把报错留下来（--3way 失败后工作区会变，那时再 --check 就看不出原因了）
  errs=$(git apply --check "$PATCH" 2>&1 | head -10)
  three_ok=0
  # 只在补丁涉及的文件本身干净时才尝试 --3way：
  #   --3way 冲突时会留下半截合并和冲突标记，需要回滚；文件干净才能放心回滚、不丢任何东西。
  if git diff --quiet HEAD -- $pfiles 2>/dev/null; then
    if git apply --3way "$PATCH" >/dev/null 2>&1; then
      three_ok=1
    else
      git checkout -q HEAD -- $pfiles      # 回滚半截合并
    fi
  fi
  if [ "$three_ok" -eq 1 ]; then
    echo "   ✓ 直接打不上，但 --3way 三方合并成功了（上游改了补丁周边的上下文，但不冲突）"
    echo "     建议 git -C $INFER diff 看一眼合并结果"
  else
  echo "$errs" | sed 's/^/      /' >&2
  cat >&2 <<EOF

   ✗ 补丁打不上，--3way 也合并不了。
     根因几乎一定是：infer 源码不在调研基线上，上游在基线之后改了补丁涉及的文件。

     推荐做法 —— 回到调研基线重建（改动收进 stash，不会丢）：
         git -C $INFER stash -u
         git -C $INFER checkout $INFER_BASELINE
         FROM=20 $HERE/run-all.sh

     想留在当前 commit，就得按 03-MiniOobChecker/README.md「在其他 commit 上安装」
     一节手工移植这 9 个文件的注册改动（都是往列表里加一行，共 37 行）。

     （工作区已回滚到打补丁之前的状态，没有留下半截合并。）
EOF
  exit 1
  fi
fi

# ----------------------------------------------------------------------
step "重新构建 infer（增量，只编改动的部分）"
make -j"$J" BUILD_MODE=default infer
./infer/bin/infer --version | head -1 | sed 's/^/   /'

# ----------------------------------------------------------------------
step "跑 demo"
cd "$PACK/03-MiniOobChecker/demo"
rm -rf out
"$INFER/infer/bin/infer" run --mini-oob -o out -- gcc -c demo.c
echo
echo "预期能看到 [MiniOOB] Analyze read / Generate Summary / Load Summary / Apply Summary，"
echo "并在 demo.c:5 报出 MINI_OOB。对不上的话看 03-MiniOobChecker/README.md。"

step_done
