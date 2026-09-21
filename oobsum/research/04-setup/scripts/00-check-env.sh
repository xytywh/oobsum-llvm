#!/usr/bin/env bash
# 先跑这个。它只做检查，不改任何东西，告诉你该走哪条路。
set -u
ok(){ printf '  \033[32m✓\033[0m %s\n' "$1"; }
no(){ printf '  \033[31m✗\033[0m %s\n' "$1"; }
hd(){ printf '\n\033[1m%s\033[0m\n' "$1"; }

hd "0. CA 证书环境变量"
# 实测：shell 里残留 SSL_CERT_FILE=/tmp/_MEIxxxx/certifi/cacert.pem（某个 PyInstaller 打包工具
# 退出后临时目录被清掉），所有 curl 都会报 (77) error setting certificate file ——
# 下面第 3 节的网络探测会全部显示不可达，给出完全错误的结论。
# 本脚本只在**自己的进程里**忽略这些失效变量来做探测，不改你的 shell。
_ca_bad=0
for _v in SSL_CERT_FILE CURL_CA_BUNDLE REQUESTS_CA_BUNDLE; do
  _val="${!_v:-}"
  if [ -n "$_val" ] && [ ! -f "$_val" ]; then
    no "$_v=$_val  ← 指向的文件不存在，curl 会报 (77)"
    unset "$_v"; _ca_bad=1
  elif [ -n "$_val" ]; then
    ok "$_v=$_val"
  fi
done
if [ "$_ca_bad" -eq 1 ]; then
  echo "     本次探测已临时忽略它。脚本 10–50 和 run-all.sh 会自动清理，不用管；"
  echo "     但你**手敲 curl** 之前要先清一下（只影响当前窗口，别的窗口不受影响）："
  echo "         . ./opam-env.sh        # 会顺手清理失效的 CA 变量"
  echo "       或 unset SSL_CERT_FILE"
elif [ -z "${SSL_CERT_FILE:-}${CURL_CA_BUNDLE:-}${REQUESTS_CA_BUNDLE:-}" ]; then
  ok "未设置（curl 用系统默认 CA）"
fi

hd "1. 必备命令"
for c in git curl gcc g++ make cmake autoconf automake pkg-config m4 patch unzip python3; do
  command -v $c >/dev/null && ok "$c" || no "$c  ← 缺，先装"
done
command -v opam >/dev/null && ok "opam $(opam --version 2>/dev/null)" || no "opam  ← 缺（脚本 10 会装）"
command -v rsync >/dev/null && ok "rsync" || no "rsync  ← 缺（脚本 10 会装替身）"
command -v bwrap >/dev/null && ok "bubblewrap" \
  || no "bubblewrap  ← 缺。**不用装**：本套脚本的 opam init 一直带 --disable-sandboxing。
       只有你自己手敲裸的 \`opam init\` 才会被它拦住（报 bwrap not found）——那种情况下
       该做的是 \`. ./opam-env.sh\`，不是装 bubblewrap。"

hd "2. 头文件 / 库"
for h in /usr/include/gmp.h /usr/include/sqlite3.h /usr/include/zlib.h; do
  [ -f $h ] && ok "$h" || no "$h  ← 缺"
done
[ -f /usr/local/include/gmp.h ] && ok "/usr/local/include/gmp.h（本地装的）"

hd "3. 网络可达性（决定走哪条路）"
probe(){ code=$(curl -sSL -o /dev/null -w '%{http_code}' -r 0-100 --max-time 25 "$2" 2>/dev/null); 
         [ "$code" = "200" ] || [ "$code" = "206" ] && ok "$1 (HTTP $code)" || no "$1 (HTTP ${code:-超时})"; }
probe "opam.ocaml.org          " "https://opam.ocaml.org/index.tar.gz"
probe "github releases/download" "https://github.com/ocaml/opam/releases/download/2.4.1/opam-2.4.1-x86_64-linux"
probe "github /archive/ 压缩包  " "https://github.com/ocaml/ocaml/archive/refs/tags/5.4.1.tar.gz"
probe "raw.githubusercontent   " "https://raw.githubusercontent.com/ocaml/opam-repository/master/repo"
probe "erratique.ch            " "https://erratique.ch/software/fmt/releases/fmt-0.11.0.tbz"
probe "gitlab.inria.fr         " "https://gitlab.inria.fr/fpottier/menhir"
printf '  git clone github        '
timeout 90 git clone --depth 1 -q https://github.com/ocaml/ocamlfind.git /tmp/_probe_of >/dev/null 2>&1 \
  && { printf '\033[32m✓\033[0m 可用\n'; rm -rf /tmp/_probe_of; } || printf '\033[31m✗\033[0m 不可用\n'

hd "结论"
if curl -sSL -o /dev/null --max-time 25 https://opam.ocaml.org/index.tar.gz 2>/dev/null; then
  echo "  opam.ocaml.org 可达 → 走【标准路线】：直接 ./build-infer.sh clang，不需要本目录的其它脚本。"
  echo "  （若仍卡住，看 ../01-restricted-network-build.md 的'卡在 initializing opam 怎么办'）"
else
  echo "  opam.ocaml.org 不可达 → 走【受限路线】：依次执行 10 → 20 → 30 → 40 → 50。"
fi
