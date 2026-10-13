# 本地开发环境的公共变量（被其它脚本 source；也可以在交互 shell 里 source 它）。
# 目录布局刻意对齐公司机器：/usr1/llvm-project-14/cmake-build-release  ↔  ~/llvm-project-14/cmake-build-release
export LLVM_TAG=${LLVM_TAG:-llvmorg-14.0.0}
# llvmorg-14.0.0 这个 tag 指向的提交（GitHub 上游）；拿它和公司那份 llvm-project-14 的提交比，就知道两边是不是同一份源码
export LLVM_TAG_COMMIT=329fda39c507e8740978d10458451dcdb21563be
# GitHub Releases 上的源码包 llvm-project-14.0.0.src.tar.xz（约 101 MB）的 sha256（沙箱里实际下载核对过）
export LLVM_TARBALL_SHA256=35ce9edbc8f774fe07c8f4acdf89ec8ac695c8016c165dd86b8d10e7cba07e23
export LLVM_SRC=${LLVM_SRC:-$HOME/llvm-project-14}
export LLVM_BUILD=${LLVM_BUILD:-$LLVM_SRC/cmake-build-release}
export LLVM_GIT=${LLVM_GIT:-https://github.com/llvm/llvm-project.git}
