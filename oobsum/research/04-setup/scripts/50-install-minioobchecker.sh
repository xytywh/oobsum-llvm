#!/usr/bin/env bash
# 第 5 步：把本调研包里的 MiniOobChecker 装进 infer 源码并重新构建。
set -eu
WORK="${WORK:-$HOME/infer-build}"
INFER="${INFER:-$WORK/infer}"
PACK="$(cd "$(dirname "$0")/../.." && pwd)"     # 调研包根目录
export PATH="$WORK/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
eval "$(opam env --switch=infer541 --set-switch)"
J="${J:-$(nproc)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"
[ -d "$INFER" ] || { echo "✗ 找不到 infer 源码：$INFER，先跑 30。" >&2; exit 2; }


cp "$PACK/03-MiniOobChecker/src/"MiniOob*.ml "$PACK/03-MiniOobChecker/src/"MiniOob*.mli \
   "$INFER/infer/src/labs/"
cd "$INFER"
git apply "$PACK/03-MiniOobChecker/patch/0001-register-minioob-checker.patch"
make -j"$J" BUILD_MODE=default infer
./infer/bin/infer --version

cd "$PACK/03-MiniOobChecker/demo"
"$INFER/infer/bin/infer" run --mini-oob -o out -- gcc -c demo.c
echo
echo "预期能看到 [MiniOOB] Analyze read / Generate Summary / Load Summary / Apply Summary，"
echo "并在 demo.c:5 报出 MINI_OOB。对不上的话看 03-MiniOobChecker/README.md。"

step_done
