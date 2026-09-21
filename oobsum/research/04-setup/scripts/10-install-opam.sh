#!/usr/bin/env bash
# 受限路线第 1 步：装 opam 本体、GMP 头文件、rsync 替身。全部只用 git clone 和 GitHub Releases。
#
# 【v2.6】并入使用方在真实环境里的修复：清理失效的 SSL_CERT_FILE / CURL_CA_BUNDLE。
# 现象是 curl 报 "error setting certificate file"——环境里残留了
# PyInstaller 的临时 CA 路径（/tmp/_MEIxxxx/certifi/cacert.pem），那个目录早没了。
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"

WORK="${WORK:-$HOME/infer-build}"
mkdir -p "$WORK/bin"
export PATH="$WORK/bin:$PATH"

# --- CA 证书环境变量体检 --------------------------------------------------
# 指向不存在的文件时 curl 会直接失败，且报错信息跟网络问题长得很像，很难定位。
# 优先换成系统 CA 包；都找不到就 unset，让 curl 走自己的默认值。
if [ -n "${SSL_CERT_FILE:-}" ] && [ ! -f "$SSL_CERT_FILE" ]; then
  echo "!! SSL_CERT_FILE 指向不存在的文件：$SSL_CERT_FILE"
  if [ -f /etc/pki/tls/certs/ca-bundle.crt ]; then
    export SSL_CERT_FILE=/etc/pki/tls/certs/ca-bundle.crt
    echo "   -> 改用 $SSL_CERT_FILE"
  elif [ -f /etc/ssl/certs/ca-certificates.crt ]; then
    export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
    echo "   -> 改用 $SSL_CERT_FILE"
  else
    unset SSL_CERT_FILE
    echo "   -> 找不到系统 CA 包，已 unset，让 curl 用默认值"
  fi
fi
if [ -n "${CURL_CA_BUNDLE:-}" ] && [ ! -f "$CURL_CA_BUNDLE" ]; then
  echo "!! CURL_CA_BUNDLE 指向不存在的文件，已 unset：$CURL_CA_BUNDLE"
  unset CURL_CA_BUNDLE
fi

step_total 3

# ----------------------------------------------------------------------
step "opam 本体（GitHub Releases，受限环境通常放行这个路径）"
if [ -x "$WORK/bin/opam" ]; then
  echo "   已存在，跳过下载"
else
  curl -fSL --progress-bar -o "$WORK/bin/opam" \
    https://github.com/ocaml/opam/releases/download/2.4.1/opam-2.4.1-x86_64-linux
  chmod +x "$WORK/bin/opam"
fi
echo -n "   opam 版本: "; "$WORK/bin/opam" --version

# ----------------------------------------------------------------------
step "GMP 头文件（zarith 依赖它；apt 不可用时走 git 镜像）"
if [ -f /usr/include/gmp.h ] || [ -f /usr/local/include/gmp.h ]; then
  echo "   gmp.h 已存在，跳过"
else
  [ -d "$WORK/gmp" ] || {
    echo "   clone gmp-mirror/gmp-6.3 ..."
    git clone --depth 1 --progress https://github.com/gmp-mirror/gmp-6.3 "$WORK/gmp" 2>&1 | sed -u 's/^/   /'
  }
  cd "$WORK/gmp"
  [ -f configure ] || ( ./.bootstrap 2>/dev/null || autoreconf -i )
  echo "   configure（只为生成 gmp.h，不编译整个库）..."
  SED=sed CC=gcc ./configure --prefix=/usr/local --enable-shared --disable-static >/dev/null
  sudo cp gmp.h /usr/local/include/gmp.h 2>/dev/null || cp gmp.h /usr/local/include/gmp.h
  # 系统若已有 libgmp.so.10（GMP 6.x）直接建软链，比编译整个库快得多
  if [ ! -e /usr/lib/x86_64-linux-gnu/libgmp.so ] && [ -e /usr/lib/x86_64-linux-gnu/libgmp.so.10 ]; then
    sudo ln -sf /usr/lib/x86_64-linux-gnu/libgmp.so.10 /usr/lib/x86_64-linux-gnu/libgmp.so 2>/dev/null \
      || ln -sf /usr/lib/x86_64-linux-gnu/libgmp.so.10 /usr/lib/x86_64-linux-gnu/libgmp.so
    (sudo ldconfig 2>/dev/null || ldconfig)
  fi
  cd - >/dev/null
fi
printf '   自检 gmp: '
echo '#include <gmp.h>
#include <stdio.h>
int main(){ mpz_t a; mpz_init_set_ui(a,42); gmp_printf("gmp ok: %Zd\n",a); return 0; }' \
  | gcc -x c - -o /tmp/_gmptest -lgmp && /tmp/_gmptest

# ----------------------------------------------------------------------
step "rsync 替身（opam 用它拷本地 pin 源；系统没装且装不了时用这个）"
if command -v rsync >/dev/null; then
  echo "   系统已有 rsync，跳过"
else
  cp "$HERE/rsync-shim.py" "$WORK/bin/rsync"
  chmod +x "$WORK/bin/rsync"
  echo "   已安装 rsync 替身到 $WORK/bin/rsync"
fi

step_done

echo "完成。请把这行加进你的 shell（后续脚本都需要）："
echo "    export PATH=$WORK/bin:\$PATH"
echo "下一步： ./20-prepare-opam-repo.sh"
