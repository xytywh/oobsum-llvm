# 落地 / 编译 / 运行

新增/修改 CSA 规则要落地五处，然后**重新编译再运行验证**。默认 **LLVM 14**。

## 1. `Checkers.td` 注册（按包路由）

路径 `BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td`。**默认路由见下；跨文件/跨 TU/全工程或拿不准的先与用户确认包。新增条目追加到对应包块最后，描述英文。**

```tablegen
// AST 规则 / 单函数规则 / 单函数路径敏感规则 -> rb.enhance.core
let ParentPackage = RosaRBEnhanceCore in {

// ... 既有规则 ...
// >>> 新增追加到末尾 <<<
def IntegerNarrowingThenWideningCastChecker : Checker<"IntegerNarrowingThenWideningCast">,
    HelpText<"Detect integer narrowing-then-widening cast patterns that may lose data">,
    Documentation<NotDocumented>;

} // end "rb.enhance.core"

// 跨函数、单文件 路径敏感规则 -> rb.enhance.ipa
let ParentPackage = RosaRBEnhanceIpa in {

// ... 既有规则 ...
// >>> 新增 IPA 规则追加到末尾 <<<

} // end "rb.enhance.ipa"
```

> **`-checker=` 用 `Checker<"...">` 里的运行短名（不带 `Checker` 后缀），不是 td `def` 名。** 例：C++ 类/文件名 `IntegerNarrowingThenWideningCastChecker`，td `def` 名同样是 `IntegerNarrowingThenWideningCastChecker`（带后缀，与类名一致），而 `Checker<"...">` 字符串是 `IntegerNarrowingThenWideningCast`（去后缀），运行时即 `rb.enhance.core.IntegerNarrowingThenWideningCast`。`-checker=` 必须与 `Checker<"...">` 字符串严格一致。

带 option 的写法见文末附录。

## 2. checker 注册函数（`.cpp` 末尾，`ento::` 命名空间）

注册函数名跟 td `def` 名一致（`register<DefName>`/`shouldRegister<DefName>`，即带 `Checker` 后缀，与 C++ 类名相同）：

```cpp
void ento::registerIntegerNarrowingThenWideningCastChecker(CheckerManager &mgr)
{
    mgr.registerChecker<IntegerNarrowingThenWideningCastChecker>();
}

bool ento::shouldRegisterIntegerNarrowingThenWideningCastChecker(const CheckerManager &mgr)
{
    return true;
}
```

文件顶部务必 `#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"`。

## 3. `CMakeLists.txt` 登记

打开 `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt`，把新规则 `.cpp` **追加到 `add_library()` 源文件列表最后**，**保持与既有条目相同的缩进/对齐**（参考相邻规则写法），**只追加、不重排既有行、不改无关 CMake 逻辑**：

```cmake
add_library(...                                   # 既有 target/选项不动
    ExistingCheckerA.cpp
    ExistingCheckerB.cpp
    # ... 既有规则 ...
    IntegerNarrowingThenWideningCastChecker.cpp   # <<< 追加到最后，对齐既有行
)
```

### 3.1 测试目录自身的 `CMakeLists.txt` + 父级登记

测试用例落在 `BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/`（目录名用**大驼峰**规则名）。该目录根**必须**放一个 `CMakeLists.txt`。模板见 `templates/test_cmakelists.txt`。

单测试文件：

```cmake
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(ResourceDoubleFreeChecker
        TC_ResourceDoubleFreeChecker.cpp)
```

多测试文件（`src/` 放用例、`include/` 放公共头，`file(GLOB ...)` 收集）：

```cmake
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# .c 是当前 N 个用例，.cpp 给后续可能补的 C++ 用例预留位
file(GLOB OOB_TC_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
add_library(TC_OobOnUniqueGlobalChecker
        ${OOB_TC_SOURCES}
)
```

`add_library()` 的 target 名唯一即可（参考既有用例，可带/不带 `TC_` 前缀）。

然后在父级 `BXPlugin/xir/llvm_ast/TestCase/CMakeLists.txt` 的**最后追加一行**引入该目录：

```cmake
add_subdirectory(<CheckerName>)
```

## 4. 编译（每次改动后必做）

```bash
cd /usr1/BXPlugin
# 固定使用 cmake-build-release-release：不存在则创建，已存在 mkdir 失败也无妨（不加 -p 时退出码非 0 但不影响后续）
mkdir cmake-build-release-release 2>/dev/null; cd cmake-build-release-release

cmake -G "Unix Makefiles" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_VERBOSE_MAKEFILE=ON \
  -Darch=64 \
  -Dos=linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ROOT=/usr1/llvm-project-14/cmake-build-release/ \
  -Dprovider=llvm_ast \
  -Dptype=lib \
  -DCLANG_TABLEGEN_EXE=/usr1/llvm-project-14/cmake-build-release/bin/clang-tblgen \
  ../

make -j16
```

要点：
- `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 在构建目录生成 `compile_commands.json`——正是运行时 `-cc_json` 指向的目录。
- configure 成功后用 `make -j16`（明确用 16，**不要更大**，避免影响他人程序）。若项目已有更精确 target，优先用既有 target。
- 构建目录固定为 `/usr1/BXPlugin/cmake-build-release-release`，直接 `mkdir` 后 `cd` 进入，不再探测或与用户确认目录名。
- **编译失败**：如实说明失败点并保留关键错误，判断是否由 LLVM 路径/TableGen/CMake 缓存/新增文件未注册导致，**与用户对齐正确编译方式后再继续**，不要擅自换命令硬试。

## 5. 运行（`bxen`，编译成功后）

可执行与 `-cc_json` 都指向构建目录。**不要**用 `clang --analyze -Xanalyzer` 形式。core/ipa 规则 `-analyzer-max-loop` 统一用 **3**。

**单测试文件**（推荐：所有用例集中一个文件）：

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/IntegerNarrowingThenWideningCastChecker/TC_IntegerNarrowingThenWideningCastChecker.c \
  -checker=rb.enhance.core.IntegerNarrowingThenWideningCast \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

**整目录**（用例确实分散到多个文件时，把 `-analysis_file` 换成 `-analysis_folder` 指向目录）：

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_folder=/usr1/BXPlugin/xir/llvm_ast/TestCase/IntegerNarrowingThenWideningCastChecker/ \
  -checker=rb.enhance.core.IntegerNarrowingThenWideningCast \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

参数说明：
- `-cc_json` = **构建（release）目录**，即 `compile_commands.json` 所在处。
- `-analysis_file` = 该规则测试用例文件；**推荐所有用例集中一个文件**。分散时改用 `-analysis_folder=<目录>/`。
- `-checker` = 包名 + `Checker<"...">` 运行短名（`rb.enhance.core.<CheckerNameWithoutChecker>`，不带 `Checker` 后缀；**不是** td `def` 名）。
- **引号写法**：整个 `-extra_args=...` 连同内含空格**一起**用双引号包住：`"-extra_args=-analyzer-max-loop 3 ..."`（不是只引值）。
- `-extra_args` 内：`-analyzer-max-loop 3`（core/ipa 统一 3）、`-analyzer-config ipa=...`、`-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini`、`-funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini`。
- **`ipa` 与作用域匹配**：`rb.enhance.core`/单函数规则用 `ipa=none`；`rb.enhance.ipa` 跨函数规则**去掉 `ipa=none`**、按需开启 IPA。
- 谨慎调大循环（显著增加耗时）：`-analyzer-config widen-loops=true`、`-analyzer-config unroll-loops=true`。

**全量 `compile_commands.json`**（`-cc_json` 指向 json 本身，省略 file/folder）：

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
  -cc_json=path-to/compile_commands.json \
  -checker=rb.enhance.core.<CheckerNameWithoutChecker> \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

**运行失败排查**：如实说明错误，判断是否由 ① checker 注册名不正确 ② `Checkers.td` 未重生成 ③ `CMakeLists.txt` 未加新源文件 ④ `-cc_json` 指向错误 ⑤ 测试文件路径错误 ⑥ `extra_args` 格式错误 ⑦ analyzer config 不兼容 导致；与用户对齐是否调整参数后重跑。

## 6. 验证结果输出格式

验证完成后输出应包含：

```text
编译结果： 成功 / 失败（失败附关键错误）
运行命令： 实际执行的 bxen 命令
运行结果： 实际告警数 / 预期告警数 / 是否符合预期 / 不符合时的初步原因
```

## 7. TableGen 重生成坑（CLion / 切分支）

改了 `Checkers.td` 后若 `Checkers.inc` 未重生成，会"新 checker 注册不上 / 老定义残留"。措施：
- 给依赖 `Checkers.inc` 的 target 显式加依赖，确保 TableGen 先跑：
  ```cmake
  add_dependencies(<your_target> <ClangSACheckers_tablegen_target>)
  ```
- 单独构建某 target 可能不触发 TableGen；必要时做完整构建。
- 切分支后 checker 注册异常，优先检查并重新生成 `Checkers.inc`。

## 8. 自检
- 五处都改了（源码 / 源码 CMakeLists 追加到最后 / Checkers.td 追加到最后 / 用例目录含自身 CMakeLists / `TestCase/CMakeLists.txt` 追加 `add_subdirectory(<CheckerName>)`）？
- 包路由对（core/ipa/已确认）？`-checker=` 与 `Checker<"...">` 运行短名一致（不带 `Checker` 后缀）？def 名 / `register…`/`shouldRegister…` 函数带 `Checker` 后缀（与 C++ 类名一致）？
- 重新编译（`make -j16`）了？失败已与用户对齐？
- `-cc_json` 指向构建目录？`-extra_args` 整体加引号？`max-loop 3`？core 用 `ipa=none` / ipa 规则开 IPA？带 `-funcptr-config`？
- 诊断与 td 描述全英文？给了验证输出（实际 vs 预期告警数）？

---

## 附录：带 option 的 td 与读取

```tablegen
def IntegerNarrowingThenWideningCastChecker : Checker<"IntegerNarrowingThenWideningCast">,
    HelpText<"Detect integer narrowing-then-widening cast patterns that may lose data">,
    CheckerOptions<[
      CmdLineOption<Boolean, "WarnOnConstantTruncation",
                    "Also warn when a compile-time constant is provably truncated",
                    "false", InAlpha>
    ]>,
    Documentation<NotDocumented>;
```

```cpp
void ento::registerIntegerNarrowingThenWideningCastChecker(CheckerManager &mgr)
{
    auto *chk = mgr.registerChecker<IntegerNarrowingThenWideningCastChecker>();
    chk->WarnOnConstantTruncation =
        mgr.getAnalyzerOptions().getCheckerBooleanOption(chk, "WarnOnConstantTruncation");
}
```
