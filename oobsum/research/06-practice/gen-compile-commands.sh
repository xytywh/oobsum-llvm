#!/usr/bin/env bash
# demo 工程的 compile_commands.json 里 directory 字段是占位符（因为它必须是绝对路径），
# 跑之前用这个脚本替换成你解压后的真实路径。
#
# 注意：directory 必须是一个【真实存在的目录】——infer 会 chdir 进去再执行编译命令。
# demo 里 directory 指向 <工程>/build（模拟 CMake 的构建目录，所以命令里是
# -I../include 和 ../src/x.c 这样的相对路径），所以本脚本会顺手把 build/ 建出来。
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
fail=0

for d in demo-project demo-project-plain; do
  root="$here/$d"
  [ -d "$root" ] || { echo "跳过（不存在）: $root"; continue; }

  # ① directory 必须存在，否则 infer 会报 chdir(...): No such file or directory
  mkdir -p "$root/build"

  # ② 把占位符（或上一次写入的旧绝对路径）改写成当前真实路径
  python3 - "$root/compile_commands.json" "$root" <<'PY'
import json, sys
p, root = sys.argv[1], sys.argv[2]
d = json.load(open(p))
for c in d:
    c["directory"] = root + "/build"
json.dump(d, open(p, "w"), indent=2)
PY

  # ③ 自检：按 directory + file 解析每条记录，确认源文件真的能找到
  python3 - "$root/compile_commands.json" <<'PY' || fail=1
import json, os, sys
p = sys.argv[1]
bad = 0
for c in json.load(open(p)):
    d = c["directory"]
    if not os.path.isdir(d):
        print(f"  [X] directory 不存在: {d}"); bad += 1; continue
    f = os.path.normpath(os.path.join(d, c["file"]))
    if not os.path.isfile(f):
        print(f"  [X] 源文件解析不到: {c['file']} -> {f}"); bad += 1
sys.exit(1 if bad else 0)
PY

  echo "已就绪: $root"
  echo "        directory -> $root/build   (目录已创建)"
done

if [ "$fail" -ne 0 ]; then
  echo
  echo "自检未通过，先别急着跑 infer。" >&2
  exit 1
fi

cat <<EOF

自检通过。接下来：

  cd $here/demo-project
  infer capture --compilation-database compile_commands.json
  infer analyze --bufferoverrun-only

  预期：src/main.c:5: error: Buffer Overrun L1  Offset: 17 Size: 10 by call to \`mid_fetch\`.
EOF
