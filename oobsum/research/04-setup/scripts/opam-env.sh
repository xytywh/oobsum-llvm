# opam-env.sh —— **source 它，别执行它**。
#
#   . ./opam-env.sh          # 或 source ./opam-env.sh
#
# 干什么用：本套脚本把 opam root 建在 $WORK/opamroot（默认 ~/infer-build/opamroot），
# **不是** opam 默认的 ~/.opam。每个脚本内部都 export 了 OPAMROOT，所以脚本跑起来没问题；
# 但你在一个干净的交互 shell 里手敲 `opam ...` 时，OPAMROOT 是空的，
# opam 就去看 ~/.opam —— 那个目录从来没被初始化过，于是报：
#
#     [ERROR] Opam has not been initialised, please run `opam init'
#
# 这时**千万不要真的去跑 `opam init`**：那会在 ~/.opam 建出第二个 root，
# 跟你真正的 root 打架；而且裸跑的 init 没带 --disable-sandboxing，
# 在没装 bubblewrap 的机器上还会报 `bwrap: Sandboxing tool bwrap was not found`，
# 把人引向"去装 bubblewrap"这条完全不必要的岔路（本套脚本的 init 一直带着
# --disable-sandboxing，根本不需要 bwrap）。
#
# 正确做法就是 source 本文件。之后 opam / ocamlopt / dune 都指向对的地方。

# 注意：本文件会被交互 shell source，所以**不能**有 set -e / exit。

_oe_work="${WORK:-$HOME/infer-build}"
export WORK="$_oe_work"
export PATH="$_oe_work/bin:$PATH"
export OPAMROOT="${OPAMROOT:-$_oe_work/opamroot}"
export OPAMYES="${OPAMYES:-1}"
export OPAMCOLOR="${OPAMCOLOR:-never}"
export OPAMDLJOBS="${OPAMDLJOBS:-4}"

# 有人不小心裸跑过 opam init 的话，~/.opam 会冒出来。它不是我们的 root，留着只会混淆。
if [ -d "$HOME/.opam" ] && [ "$OPAMROOT" != "$HOME/.opam" ]; then
  echo "  ! 检测到 $HOME/.opam —— 这**不是**本套脚本用的 opam root。"
  echo "    多半是某次裸跑 opam init 建出来的半成品。确认里面没有你要的东西后可以删："
  echo "        rm -rf $HOME/.opam"
  echo "    （本套脚本的 root 在 $OPAMROOT，不受影响。）"
fi

if command -v opam >/dev/null 2>&1; then
  if [ -f "$OPAMROOT/config" ]; then
    # switch 建好了就一并切过去；没建好（还没跑脚本 30）就跳过，不报错
    if opam switch list --short 2>/dev/null | grep -qx infer541; then
      eval "$(opam env --switch=infer541 --set-switch 2>/dev/null)"
      echo "  ✓ OPAMROOT=$OPAMROOT   switch=infer541   opam $(opam --version)"
    else
      echo "  ✓ OPAMROOT=$OPAMROOT   （switch infer541 还没建，跑 ./30-install-deps.sh）"
    fi
  else
    echo "  ! $OPAMROOT 还不是有效的 opam root —— 先把 ./20-prepare-opam-repo.sh 跑成功。"
    echo "    （不要手工 opam init：脚本 20 里的 init 带着 --disable-sandboxing 等必要参数。）"
  fi
else
  echo "  ! PATH 里找不到 opam —— 先跑 ./10-install-opam.sh。"
fi

unset _oe_work
