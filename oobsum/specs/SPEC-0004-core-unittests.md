# SPEC-0004 `core/` 单元测试：`oobsum-core-tests`（不依赖 gtest）

状态：已实现（S3a）
来源：06- §9.3（"`core/` 单测：独立可执行 `oobsum-core-tests`（自带最小测试框架，不依赖 gtest）"）、T-01；CLAUDE.md §5.6
（"`core/` 的纯逻辑配单元测试"）；SPEC-0003 §5（`core/Key` 的单元测试欠到 S3）。

## 1. 要解决什么

`core/` 以后是引擎的主体（线性式、界、蕴含、代换……，T-01 约 120 条），这些纯逻辑用 lit 测太绕，要能直接调函数、比结果。
现在先把框架和构建搭起来，补上已有 `core/` 代码（键、指纹、标签、报告策略）的测试；FIR（S3b）起每个 `core/` 模块都带单测。

## 2. 设计

### 2.1 放在哪、怎么构建

* 源码在 **`oobsum/unittests/`**（不在 `clang/` 树里）：`Test.h` / `Test.cpp`（框架 + `main`）、`core/<模块>Test.cpp`、`CMakeLists.txt`。
  不放进 `clang/lib/StaticAnalyzer/Checkers/OobSummary/`：那里的 `.cpp` 都会被 `land.py` 同步到公司、并要求登记进 checker 的
  `CMakeLists.txt`（`--check-tree`）；也不改上游的 `clang/unittests`（CLAUDE.md §6 只允许改两处上游文件）。
* 独立的 CMake 工程，`find_package(LLVM CONFIG)` 指向本仓库的构建目录 `cmake-build-release/lib/cmake/llvm`，链接导入目标 `LLVMSupport`
  （依赖的系统库由它带上）；编译选项取 LLVM 的（`LLVM_DEFINITIONS`、无 RTTI、C++14）。`core/*.cpp` 用 `GLOB CONFIGURE_DEPENDS` 全部编进来——
  `core/` 不依赖 Clang（06- §9.1），所以只要 LLVM Support 就能链接；**哪天 `core/` 误包含了 `clang/` 头文件，这里会编不过**，等于多一道分层检查。
* 一条命令：`oobsum/devenv/core-tests.sh [过滤子串]`——首次配置到 `cmake-build-release/oobsum-core-tests/`，之后增量 `ninja`，再运行。

### 2.2 框架（`Test.h`）

```cpp
TEST(Key, NormalizePath) {
    EXPECT_EQ(oobsum::NormalizePath("/a/./b/../c.c"), "/a/c.c");
    ASSERT_TRUE(cond);        // 失败即返回（LLVM 不开异常，不能 throw）
    EXPECT_TRUE(cond); EXPECT_FALSE(cond);
}
```

* 用例按"套件.名字"排序后执行（不依赖静态初始化顺序）；参数是子串过滤。
* 输出：每条失败 `FAIL <文件>:<行>: <表达式>` 加左右值；最后一行 `[oobsum:unittests] <N> tests, <M> failed`；有失败退出码 1。
* `EXPECT_EQ` 的两边用 `llvm::raw_ostream` 打印（整数、`std::string`、`StringRef`、C 串）；枚举先转成名字再比。

### 2.3 本次的用例

| 文件 | 覆盖 |
|---|---|
| `core/KeyTest.cpp` | `NormalizePath`（`.`、`..`、相对路径变绝对）、`FileId`（与 `replay.py` 的 Python xxh64 同值）、四种键的写法、`KeyName`（含 `#哈希` 与 `S@F:…:名字`） |
| `core/FingerprintTest.cpp` | `HashHex` 与 Python xxh64 同值；`TuId` = `FileId`；`DecisionFingerprint` 与 `replay.py` 的 `fingerprint()` 同值（A-92 的金标准） |
| `core/PolicyTest.cpp` | `Classify` 的全部分支（SPEC-0001 §3 表）：DEFINITE / POSSIBLE 报、`inline_depth`（取 `max(depth, effDepth)`，`InlineDepth=0` 全报）；SUSPECT 不按层数过滤、`policy_balanced`、`reason_filtered`；SAFE 不报 / `ReportSafe`；`SplitList`；`ParseLabel` / `LabelName` / `LabelTag` 往返 |

期望的哈希值由 `replay.py` 的 `xxh64` / `fingerprint` 算出，写在用例的注释里。

### 2.4 进 lit

`clang/test/Analysis/oobsum/core-unittests.test` 调 `core-tests.sh`，检查最后一行 `0 failed`。这样"lit 全过"就包含单元测试，
不会忘跑。首次运行要配置 + 编译（实测约 3 秒，ccache 命中时；冷编译约半分钟），之后增量。

## 3. 对召回与误报的影响

只加测试，不改 checker。`PolicyTest` 把"SUSPECT 不按层数过滤"、"拿不准默认报"钉成单测，以后改 `Classify` 时会先在这里失败。

## 4. 实现位置

`oobsum/unittests/{CMakeLists.txt,Test.h,Test.cpp,core/*Test.cpp}`、`oobsum/devenv/core-tests.sh`、
`clang/test/Analysis/oobsum/core-unittests.test`；CLAUDE.md §4 加命令。

## 5. 测试

`oobsum/devenv/core-tests.sh` 17 个全过；lit 9 个全过；删掉构建目录后经 lit 从零配置也通过。失败路径已实地验证过一次：`LabelTag` 的期望一开始写错（实际是 `OOB-DEFINITE`），框架报出 `FAIL PolicyTest.cpp:107` 与左右两边的值、退出码 1。

## 6. 调试

`oobsum/devenv/core-tests.sh Policy` 只跑名字含 `Policy` 的用例；失败行给出文件、行号与左右两边的值。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §9.3 `core/` 单测 | 独立可执行 `oobsum-core-tests` | 不变；补充：源码在 `oobsum/unittests/`，独立 CMake 工程链接本仓库构建的 `LLVMSupport`，并由 lit 用例 `core-unittests.test` 驱动 |

## 8. 公司侧验证

不需要（单测只在本地跑，不同步到 BXPlugin）。

## 9. 待定 / 以后

* S3b 起：`FirTest`、`FirJsonTest`；WP2 的 `LinExprTest`、`BoundTest`（T-01 约 120 条）。
