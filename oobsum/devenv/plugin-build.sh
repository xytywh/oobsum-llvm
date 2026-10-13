#!/usr/bin/env bash
# 把 checker 源码编成 clang 插件（.so），本地天天用的就是这个：改完几秒钟就能跑，不用重编 clang。
# 用法：./plugin-build.sh <输出.so> <源文件...> [-I 目录 ...]
#   源码里用 #ifdef OOBSUM_PLUGIN_BUILD 提供插件入口（clang_registerCheckers）；公司 xir 里不定义这个宏，走 Checkers.td 注册。
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/llvm-select.sh"
[ $# -ge 2 ] || { echo "用法：$0 <输出.so> <源文件...> [-I 目录 ...]"; exit 1; }
OUT=$1; shift
mkdir -p "$(dirname "$OUT")"
# shellcheck disable=SC2086
"$PLUGIN_CXX" $PLUGIN_CXXFLAGS -DOOBSUM_PLUGIN_BUILD -shared "$@" -o "$OUT"
# 顺手写一份 compile_commands.json（放在 .so 旁边），VS Code / CLion 的 clangd 用它做跳转与补全
python3 - "$OUT" "$PLUGIN_CXX" "$PLUGIN_CXXFLAGS" "$@" <<'PY'
import json, os, shlex, sys
out, cxx, flags, rest = sys.argv[1], sys.argv[2], shlex.split(sys.argv[3]), sys.argv[4:]
srcs = [os.path.abspath(a) for a in rest if a.endswith(('.cpp', '.cc'))]
extra = []
i = 0
while i < len(rest):
    if rest[i] == '-I':
        extra += ['-I', os.path.abspath(rest[i + 1])]; i += 2; continue
    if rest[i].startswith('-') and not rest[i].endswith(('.cpp', '.cc')):
        extra.append(rest[i])
    i += 1
db = [{'directory': os.getcwd(), 'file': f, 'arguments': [cxx] + flags + ['-DOOBSUM_PLUGIN_BUILD'] + extra + ['-c', f]} for f in srcs]
json.dump(db, open(os.path.join(os.path.dirname(os.path.abspath(out)), 'compile_commands.json'), 'w'), indent=1)
PY
echo "built $OUT（LLVM_KIND=$LLVM_KIND，$("$CLANG" --version | head -1)）"
