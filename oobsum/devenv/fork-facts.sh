#!/usr/bin/env bash
# 在【公司机器】上跑（只读，不改任何文件、不输出任何业务源码）：收集"本地环境要对齐什么"的配置事实。
# 输出全是版本号、构建开关、文件计数；上游 LLVM 的文件名是公开的，xir 自有文件只给个数。贴回之前请自己过一眼。
# 用法：bash fork-facts.sh > fork-facts.txt
#   路径与默认不同时：LLVM14=/path/llvm-project-14 BXP=/path/BXPlugin bash fork-facts.sh
LLVM14=${LLVM14:-/usr1/llvm-project-14}
LLVMB=${LLVMB:-$LLVM14/cmake-build-release}
BXP=${BXP:-/usr1/BXPlugin}
BXB=${BXB:-$BXP/cmake-build-release-release}
UPSTREAM_1400=329fda39c507e8740978d10458451dcdb21563be   # GitHub llvmorg-14.0.0

echo "== 1. 系统与宿主编译器"
( . /etc/os-release 2>/dev/null && echo "os: $PRETTY_NAME" ); echo "arch: $(uname -m)  cores: $(nproc)"
for t in gcc g++ cmake make ninja python3; do printf '%-8s %s\n' "$t" "$($t --version 2>/dev/null | head -1 || echo '-')"; done

echo; echo "== 2. 公司的 clang（编 bxen 用的那份 llvm-project-14）"
if [ -x "$LLVMB/bin/clang" ]; then "$LLVMB/bin/clang" --version | head -2; else echo "找不到 $LLVMB/bin/clang"; fi

echo; echo "== 3. llvm-project-14 源码是不是上游 14.0.0、有没有改过"
if git -C "$LLVM14" rev-parse HEAD >/dev/null 2>&1; then
  head=$(git -C "$LLVM14" rev-parse HEAD)
  echo "HEAD: $head  ($(git -C "$LLVM14" log -1 --format='%cd %<(60,trunc)%s' --date=short | sed 's/ *$//'))"
  echo "describe: $(git -C "$LLVM14" describe --tags --always 2>/dev/null)"
  if [ "$head" = "$UPSTREAM_1400" ]; then echo "= 正好是上游 llvmorg-14.0.0"
  elif git -C "$LLVM14" merge-base --is-ancestor "$UPSTREAM_1400" HEAD 2>/dev/null; then
    echo "在上游 14.0.0 之后另有 $(git -C "$LLVM14" rev-list --count "$UPSTREAM_1400"..HEAD) 个提交"
    echo "改动的文件数（相对 14.0.0）：$(git -C "$LLVM14" diff --name-only "$UPSTREAM_1400" HEAD | wc -l)"
    echo "其中 clang/lib/StaticAnalyzer 与 clang/include/clang/StaticAnalyzer：$(git -C "$LLVM14" diff --name-only "$UPSTREAM_1400" HEAD -- clang/lib/StaticAnalyzer clang/include/clang/StaticAnalyzer | wc -l)"
  else
    echo "历史里没有上游 14.0.0 的提交（可能是导入的快照）"
  fi
  echo "未提交的改动：$(git -C "$LLVM14" status --porcelain 2>/dev/null | grep -v '^??' | wc -l) 个文件"
else
  echo "$LLVM14 不是 git 仓库；版本以第 2 节为准"
  [ -f "$LLVM14/llvm/CMakeLists.txt" ] && grep -E 'set\(LLVM_VERSION_(MAJOR|MINOR|PATCH)' "$LLVM14/llvm/CMakeLists.txt" | head -3
fi

echo; echo "== 4. llvm-project-14 的构建开关"
grep -E '^(CMAKE_BUILD_TYPE|LLVM_ENABLE_ASSERTIONS|LLVM_TARGETS_TO_BUILD|LLVM_ENABLE_PROJECTS|CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_CXX_STANDARD|CMAKE_CXX_FLAGS|LLVM_ENABLE_RTTI|LLVM_ENABLE_EH|LLVM_ENABLE_PLUGINS|BUILD_SHARED_LIBS|LLVM_BUILD_LLVM_DYLIB|LLVM_LINK_LLVM_DYLIB|LLVM_USE_LINKER|LLVM_ENABLE_ABI_BREAKING_CHECKS|CLANG_ENABLE_STATIC_ANALYZER|CMAKE_GENERATOR):' \
  "$LLVMB/CMakeCache.txt" 2>/dev/null || echo "找不到 $LLVMB/CMakeCache.txt"

echo; echo "== 5. BXPlugin 的构建开关"
grep -E '^(CMAKE_BUILD_TYPE|CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|CMAKE_CXX_STANDARD|CMAKE_CXX_FLAGS|CMAKE_CXX_FLAGS_RELEASE|LLVM_ROOT|CLANG_TABLEGEN_EXE|provider|ptype|arch|os):' \
  "$BXB/CMakeCache.txt" 2>/dev/null || echo "找不到 $BXB/CMakeCache.txt"

echo; echo "== 6. xir 内嵌的 CSA 与 llvm-project-14 的同名目录比（只给计数；改过的上游文件给文件名）"
cmpdir() {  # $1 = xir 里的目录（相对 BXPlugin），$2 = llvm-project 里的目录
  a=$BXP/$1; b=$LLVM14/$2
  if [ ! -d "$a" ] || [ ! -d "$b" ]; then echo "  $1：目录不存在（跳过）"; return; fi
  same=0; mod=0; onlyx=0; onlyu=0; lines=0; modlist=""
  while IFS= read -r f; do
    if [ -f "$b/$f" ]; then
      if cmp -s "$a/$f" "$b/$f"; then same=$((same+1)); else
        mod=$((mod+1)); n=$(diff "$b/$f" "$a/$f" | grep -c '^[<>]'); lines=$((lines+n)); modlist="$modlist $f($n)"; fi
    else onlyx=$((onlyx+1)); fi
  done < <(cd "$a" && find . -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.td' -o -name '*.def' -o -name '*.inc' \) | sed 's#^\./##' | sort)
  onlyu=$(cd "$b" && find . -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.td' -o -name '*.def' \) | sed 's#^\./##' | while read -r f; do [ -f "$a/$f" ] || echo x; done | wc -l)
  echo "  $1 ↔ $2：相同 $same，改过 $mod（共 $lines 行差异），xir 独有 $onlyx，上游有而 xir 没有 $onlyu"
  [ "$mod" -gt 0 ] && [ "$1" != "xir/llvm_ast/StaticAnalyzer/Checkers" ] && echo "    改过的：$modlist"
}
cmpdir xir/llvm_ast/StaticAnalyzer/Core      clang/lib/StaticAnalyzer/Core
cmpdir xir/llvm_ast/StaticAnalyzer/Frontend  clang/lib/StaticAnalyzer/Frontend
cmpdir xir/llvm_ast/StaticAnalyzer/Checkers  clang/lib/StaticAnalyzer/Checkers
cmpdir xir/include/clang/StaticAnalyzer      clang/include/clang/StaticAnalyzer
echo "  （Checkers 目录里改过的上游 checker 只给个数；如果 StaticAnalyzer 不在上面这些路径，告诉我实际路径）"

echo; echo "== 7. bxen"
ls -la "$BXB/bxen" 2>/dev/null | awk '{print "bxen: " $5 " bytes, " $6 " " $7 " " $8}'
ldd "$BXB/bxen" 2>/dev/null | grep -Ei 'llvm|clang' | awk '{print "  links: " $1}' | head -5
