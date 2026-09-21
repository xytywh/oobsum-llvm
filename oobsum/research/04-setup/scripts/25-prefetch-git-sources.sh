#!/usr/bin/env bash
# 受限路线第 2.5 步：把 opam 将要 git fetch 的源，**我们自己先 clone 到本地**，
# 再把 opam 仓库里的 URL 改写成 file:// 指向本地镜像。
# 之后 opam 装依赖时一次网络都不走。
#
# 什么时候需要它？
#   脚本 30 装依赖时大面积报
#     [ERROR] Could not synchronize .../<pkg> from "git+https://github.com/o/r#tag":
#             "git fetch -q" exited with code 128 "RPC failed; HTTP 403"
#   而你手工 `git clone` 同一个仓库是**能成功**的。
#
# 为什么会这样（三个常见原因，本脚本对前两个都有效）：
#   ① opam 默认并发下载，几十个 git fetch 同时打到 github，被代理限流 → 403。
#      本脚本**串行**拉取，并带重试与退避。
#   ② opam 调 git 时的环境/配置与你交互 shell 不同（代理写在别的 scope、
#      或 opam 清了某些变量），导致它那边没走代理。
#      本脚本在**你自己的 shell 里**拉取，环境完全一致。
#   ③ 代理按仓库白名单放行。这种情况本脚本也救不了——它会把拉不到的仓库列出来，
#      那时只能让网管加白名单，或换"直接用官方预编译 infer"的路线。
#
# 用法：
#   ./25-prefetch-git-sources.sh              # 用 infer 的 lock 文件决定拉哪些
#   RETRIES=5 ./25-prefetch-git-sources.sh    # 网络差时加大重试
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"

WORK="${WORK:-$HOME/infer-build}"
INFER="${INFER:-$WORK/infer}"
REPO="${REPO:-$WORK/opam-repository}"
MIRROR="${MIRROR:-$WORK/git-mirror}"
LOCK="${LOCK:-$INFER/opam/infer.opam.locked}"
RETRIES="${RETRIES:-3}"
export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"
export PATH="$WORK/bin:$PATH"

[ -d "$REPO/packages" ] || { echo "✗ 找不到 opam 仓库：$REPO —— 先跑 ./20-prepare-opam-repo.sh" >&2; exit 2; }
[ -f "$LOCK" ] || { echo "✗ 找不到 lock 文件：$LOCK —— 先跑 ./20-prepare-opam-repo.sh（它会 clone infer）" >&2; exit 2; }
mkdir -p "$MIRROR"

step_total 4

# ----------------------------------------------------------------------
step "先确认 git 到 github 到底通不通"
probe_ok=0
for r in janestreet/ppx_base ocaml/num dbuenzli/mtime; do
  if git ls-remote --heads "https://github.com/$r" >/dev/null 2>&1; then
    echo "   ✓ git ls-remote https://github.com/$r"
    probe_ok=$((probe_ok+1))
  else
    echo "   ✗ git ls-remote https://github.com/$r  失败"
  fi
done
if [ "$probe_ok" -eq 0 ]; then
  cat >&2 <<'EOF'
   ✗ 三个探针仓库一个都连不上。本脚本帮不了你，先解决 git 到 github 的连通性：
       git config --list --show-origin | grep -iE 'proxy|insteadof'
       env | grep -i proxy
     确认代理对**任意** github 仓库都放行（不只是 opam-repository / facebook-infer）。
EOF
  exit 3
fi
echo "   探针 $probe_ok/3 通过 —— 继续"

# ----------------------------------------------------------------------
step "从 lock 文件解析出要拉哪些 git 源"
LIST="$WORK/.prefetch-list.tsv"
python3 - "$LOCK" "$REPO" "$LIST" <<'PY'
import os, re, sys
lock, repo, out = sys.argv[1], sys.argv[2], sys.argv[3]
pkgs = []
for line in open(lock):
    m = re.match(r'\s*"([A-Za-z0-9_.+-]+)"\s*\{=\s*"([^"]+)"\}', line)
    if m:
        pkgs.append((m.group(1), m.group(2)))

SRC = re.compile(r'src:\s*"git\+(https://github\.com/[^"#]+?)(?:\.git)?(?:#([^"]+))?"')
rows, nosrc = [], 0
for name, ver in pkgs:
    p = os.path.join(repo, "packages", name, f"{name}.{ver}", "opam")
    if not os.path.isfile(p):
        nosrc += 1
        continue
    s = open(p, encoding="utf-8", errors="replace").read()
    m = SRC.search(s)
    if not m:
        continue
    rows.append((name, ver, m.group(1), m.group(2) or "HEAD", p))

with open(out, "w") as fh:
    for r in rows:
        fh.write("\t".join(r) + "\n")
print(f"   lock 里 {len(pkgs)} 个包；其中 {len(rows)} 个要从 github git 拉源"
      f"（{nosrc} 个在仓库里找不到对应版本，跳过）")
PY
TOTAL=$(wc -l < "$LIST")

# ----------------------------------------------------------------------
step "串行 clone 到本地镜像（$TOTAL 个，带重试；这一步最花时间）"
i=0; okc=0; skipc=0; failc=0
FAILED="$WORK/.prefetch-failed.txt"; : > "$FAILED"
while IFS=$'\t' read -r name ver url ref opamfile; do
  i=$((i+1))
  slug=$(printf '%s' "${url#https://github.com/}" | tr '/' '_')
  dest="$MIRROR/${slug}__${ref//\//_}"
  printf '   [%3d/%3d] %-32s %s#%s ... ' "$i" "$TOTAL" "$name.$ver" "${url#https://github.com/}" "$ref"
  if [ -d "$dest/.git" ]; then
    echo "已有镜像，跳过"; skipc=$((skipc+1)); continue
  fi
  t0=$(date +%s); got=0
  for attempt in $(seq 1 "$RETRIES"); do
    rm -rf "$dest"
    if [ "$ref" = "HEAD" ]; then
      git clone --depth 1 -q "$url" "$dest" 2>/dev/null && { got=1; break; }
    else
      git clone --depth 1 -q --branch "$ref" "$url" "$dest" 2>/dev/null && { got=1; break; }
      # ref 可能是 commit sha，--branch 不吃；退化成全量 clone
      git clone -q "$url" "$dest" 2>/dev/null && \
        git -C "$dest" checkout -q "$ref" 2>/dev/null && { got=1; break; }
    fi
    [ "$attempt" -lt "$RETRIES" ] && sleep $((attempt * 3))
  done
  if [ "$got" -eq 1 ]; then
    echo "✓ ($(( $(date +%s) - t0 ))s)"; okc=$((okc+1))
  else
    echo "✗ 重试 $RETRIES 次仍失败"; failc=$((failc+1))
    printf '%s\t%s\t%s#%s\n' "$name" "$ver" "$url" "$ref" >> "$FAILED"
    rm -rf "$dest"
  fi
done < "$LIST"
echo "   成功 $okc / 已有 $skipc / 失败 $failc"

# ----------------------------------------------------------------------
step "把 opam 仓库里的 URL 改写成本地 file:// 镜像"
python3 - "$LIST" "$MIRROR" <<'PY'
import os, re, sys
listf, mirror = sys.argv[1], sys.argv[2]
n = 0
for line in open(listf):
    name, ver, url, ref, opamfile = line.rstrip("\n").split("\t")
    slug = url[len("https://github.com/"):].replace("/", "_")
    dest = os.path.join(mirror, f"{slug}__{ref.replace('/', '_')}")
    if not os.path.isdir(os.path.join(dest, ".git")):
        continue                      # 没拉到的保持原样，让 opam 自己再试一次
    s = open(opamfile, encoding="utf-8", errors="replace").read()
    new = re.sub(r'src:\s*"git\+https://github\.com/[^"]+"',
                 f'src: "git+file://{dest}#{ref}"', s, count=1)
    if new != s:
        open(opamfile, "w").write(new)
        n += 1
print(f"   改写了 {n} 个 opam 文件，源全部指向 {mirror}")
PY

echo
if command -v opam >/dev/null 2>&1 && [ -f "$OPAMROOT/config" ]; then
  echo "   opam update default ..."
  opam update default 2>&1 | tail -2 | sed 's/^/   /'
else
  echo "   （没找到 opam 或 opam root，跳过 update。跑 30 之前先确认 20 已跑成功；"
  echo "     想手敲 opam 命令的话先 source 环境：  . $HERE/opam-env.sh"
  echo "     ——裸敲会报 Opam has not been initialised，那时**不要**去 opam init）"
fi

step_done

if [ "$failc" -gt 0 ]; then
  echo "有 $failc 个仓库没拉下来，清单在 $FAILED："
  head -20 "$FAILED" | sed 's/^/  /'
  echo
  echo "这些包 opam 仍会尝试联网。如果 30 卡在它们身上，把 $FAILED 贴出来。"
  echo "常见原因：代理按仓库白名单放行 —— 那就只能加白名单，或改用官方预编译 infer。"
else
  echo "全部就位。下一步： ./30-install-deps.sh"
  echo "（opam 装依赖时不会再碰网络，403 问题到此为止）"
fi
