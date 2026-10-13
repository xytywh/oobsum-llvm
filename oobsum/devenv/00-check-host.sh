#!/usr/bin/env bash
# 第 0 步：检查 WSL 里的环境是否适合编 LLVM、写 checker（只读，不改任何东西）。
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
warn=0
w() { echo "  注意 $*"; warn=$((warn+1)); }
echo "== 内核：$(uname -r)"
case "$(uname -r)" in *microsoft*WSL2*|*WSL2*) echo "  WSL2 ✓" ;; *microsoft*) w "这是 WSL1，请在 PowerShell 里 wsl --set-version <发行版> 2" ;; *) echo "  （不是 WSL，按普通 Linux 处理）" ;; esac
. /etc/os-release; echo "== 系统：$PRETTY_NAME"
[ "$VERSION_ID" = "22.04" ] || w "推荐 Ubuntu 22.04（gcc 11 直接编 LLVM 14；自带 clang-14 正好是 14.0.0）。24.04 也能用，30-build-llvm.sh 会自动打 gcc 13 补丁"
cores=$(nproc); memgb=$(awk '/MemTotal/ {printf "%d", $2/1024/1024}' /proc/meminfo)
swapgb=$(awk '/SwapTotal/ {printf "%d", $2/1024/1024}' /proc/meminfo)
echo "== $cores 核，内存 ${memgb} GB，swap ${swapgb} GB"
[ "$memgb" -ge 12 ] || w "内存 < 12 GB：编 LLVM 时把 JOBS 调小（JOBS=$(( memgb/2 > 0 ? memgb/2 : 1 ))），或在 .wslconfig 里给 WSL 多分一些（见 wslconfig.example）"
fs=$(df -T "$HOME" | awk 'NR==2 {print $2}'); free=$(df -BG --output=avail "$HOME" | tail -1 | tr -dc 0-9)
echo "== 家目录 $HOME：$fs，剩余 ${free} GB"
[ "$fs" = "ext4" ] || w "家目录不在 ext4 上；源码和构建目录必须放在 Linux 文件系统里（~/），不要放 /mnt/c、/mnt/d（慢 10 倍以上）"
case "$(pwd)" in /mnt/[a-z]/*) w "当前目录在 Windows 盘上（$(pwd)）；把调研包也拷到 ~/ 下再用" ;; esac
[ "$free" -ge 15 ] || w "剩余空间 < 15 GB：源码 1.6 GB + 构建目录约 1 GB + ccache 约 0.2 GB（实测），再给第二个构建目录、apt 包和日常留余量，建议 ≥ 15 GB（VHDX 默认在 C 盘，可迁到别的盘，见 09- 文档）"
echo "== git"
git --version 2>/dev/null || w "没装 git（10-install-deps.sh 会装）"
ac=$(git config --global core.autocrlf || true); echo "  core.autocrlf=${ac:-<未设>}"
[ "$ac" = "true" ] && w "core.autocrlf=true 会把脚本改成 CRLF，带去公司后 bash 报 \$'\\r'：git config --global core.autocrlf input"
[ -n "$(git config --global user.name || true)" ] || w "还没设 git 用户名：git config --global user.name <名字>; git config --global user.email <邮箱>"
echo "== 网络（GitHub）"
if timeout 20 git ls-remote --tags "$LLVM_GIT" "$LLVM_TAG" 2>/dev/null | grep -q "$LLVM_TAG"; then echo "  能访问 $LLVM_GIT ✓"
else w "访问不了 $LLVM_GIT：用浏览器下载源码包后 LLVM_TARBALL=... ./20-fetch-llvm.sh（见 09- 文档）"; fi
echo
[ "$warn" -eq 0 ] && echo "== 全部正常，可以往下走" || echo "== $warn 条注意事项（不一定都要处理，看说明）"
