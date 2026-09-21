#!/usr/bin/env bash
# 受限路线第 3.5 步：取官方预编译包，**只为借它里面编译好的 clang**。
# 从源码自举 facebook-clang-plugins 用的定制 LLVM/Clang 要好几个小时，
# 借预编译包里的那份，脚本 40 只需要编 OCaml 部分（2 核约 13 分钟）。
#
# 用法：
#   ./35-fetch-prebuilt.sh
#   PREBUILT_TARBALL=/path/to/infer-linux-x86_64-v1.3.0.tar.xz ./35-fetch-prebuilt.sh
#       ↑ 这台机器下不了的话，在别处下好拷过来，用这个变量指过去即可
#
# 【v3.3 新增】以前这一步是文档里一条手敲的 curl，实测会因为 shell 里残留的
# SSL_CERT_FILE=/tmp/_MEIxxxx/certifi/cacert.pem 报
#     curl: (77) error setting certificate file
# 挪进脚本之后，source _progress.sh 时的 CA 环境自检会先把失效变量清掉
# （只在当前进程生效，不影响你别的窗口）。
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/_progress.sh"          # ← 这一行同时完成 CA 证书环境变量自检

WORK="${WORK:-$HOME/infer-build}"
VER="${PREBUILT_VER:-v1.3.0}"
NAME="infer-linux-x86_64-$VER"
URL="https://github.com/facebook/infer/releases/download/$VER/$NAME.tar.xz"
TARBALL="${PREBUILT_TARBALL:-$WORK/infer-bin.tar.xz}"
DEST="$WORK/$NAME"
CLANG="$DEST/lib/infer/facebook-clang-plugins/clang/install"
mkdir -p "$WORK"

step_total 3

# ----------------------------------------------------------------------
step "检查是否已经就位"
if [ -d "$CLANG" ]; then
  echo "   ✓ 已存在：$DEST"
  echo "     （想重新下载就先 rm -rf $DEST $WORK/infer-bin.tar.xz）"
  step_done
  echo "下一步： ./40-build-infer.sh   （它会自动找到这个目录，不用 export PREBUILT）"
  exit 0
fi
echo "   还没有，开始准备"

# ----------------------------------------------------------------------
step "下载 $NAME.tar.xz（约 280 MB）"
tarball_ok() {   # 非空，且（有 xz 的话）通过完整性校验
  [ -s "$1" ] || return 1
  command -v xz >/dev/null 2>&1 || return 0
  xz -t "$1" 2>/dev/null
}
if tarball_ok "$TARBALL"; then
  echo "   ✓ 已有完整的压缩包：$TARBALL，跳过下载"
else
  [ -e "$TARBALL" ] && echo "   ! $TARBALL 不完整（上次下载中断？），重新下载"
  echo "   $URL"
  # 先下到 .part，完整了再改名 —— 中断的话不会留下一个"看起来存在"的半截文件
  set +e
  curl -fL --retry 3 --retry-delay 5 --connect-timeout 30 \
       -o "$TARBALL.part" "$URL"
  rc=$?
  set -e
  if [ "$rc" -ne 0 ]; then
    rm -f "$TARBALL.part"
    echo >&2
    echo "   ✗ 下载失败（curl 退出码 $rc）" >&2
    case "$rc" in
      77) echo "     77 = 读不了 CA 证书文件。检查： env | grep -iE 'SSL_CERT|CA_BUNDLE'" >&2
          echo "     指向不存在的文件会被自动清理；指向存在但损坏的文件则需要你手动 unset。" >&2 ;;
      60) echo "     60 = 证书校验失败。多半是代理做了 TLS 中间人，需要把代理的根证书加进系统 CA，" >&2
          echo "     或者 export SSL_CERT_FILE=<代理根证书所在的 CA 包>。" >&2 ;;
      22) echo "     22 = HTTP 4xx/5xx。代理可能不放行 github.com/.../releases/download。" >&2 ;;
      6|7|28) echo "     $rc = 连不上 / 超时。确认 https_proxy 已设置： env | grep -i proxy" >&2 ;;
      35) echo "     35 = TLS 握手失败。多半是代理配置问题。" >&2 ;;
    esac
    cat >&2 <<EOF

     这一步不是死路，两条出路任选：
       a) 在能上网的机器上下载 $URL
          拷到这台机器上，然后：
              PREBUILT_TARBALL=/path/to/$NAME.tar.xz $HERE/35-fetch-prebuilt.sh
       b) 不用预编译包，让脚本 40 从源码自举 LLVM/Clang（多花几个小时）：
              $HERE/40-build-infer.sh      # 找不到预编译包时它会问你是否继续
EOF
    exit 1
  fi
  mv "$TARBALL.part" "$TARBALL"
  echo "   ✓ 下载完成：$(du -h "$TARBALL" | cut -f1)"
fi

# ----------------------------------------------------------------------
step "解压到 $WORK"
heartbeat_start "解压中"
tar xf "$TARBALL" -C "$WORK" || { heartbeat_stop; echo "   ✗ 解压失败：$TARBALL" >&2; exit 1; }
heartbeat_stop
if [ -d "$CLANG" ]; then
  echo "   ✓ clang 就位：$CLANG"
else
  echo "   ✗ 解压完了但找不到 $CLANG —— 压缩包版本不对？" >&2
  exit 1
fi

step_done
echo "下一步： ./40-build-infer.sh   （它会自动找到 $DEST，不用 export PREBUILT）"
