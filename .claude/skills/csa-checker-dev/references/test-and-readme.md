# 测试 / README / 提交

## 1. 正交测试方法论（TP/TN/FP/FN 为骨架 + 边界/回归补充）

每条规则**默认覆盖四象限**，并在其上补充边界与回归：

| 象限 | 含义 | 目的 |
|---|---|---|
| **TP** True Positive | 真实缺陷，应报且报了 | 证明能抓到目标模式 |
| **TN** True Negative | 安全代码，不应报且没报 | 证明不乱报 |
| **FP** False Positive | 已知误报，不应报却报了 | 诚实记录边界缺陷、给缓解依据 |
| **FN** False Negative | 已知/可接受漏报，应报却没报 | 显式承认能力边界、留工业取舍痕迹 |

补充维度（按规则相关性覆盖）：
- 边界：等值/极限、null/unknown、类型转换、宏/枚举常量。
- 误报防护：safe wrapper、成员仅作普通函数实参传递、guard 后访问、已校验长度。
- 嵌套成员路径（若涉及）：告警含稳定路径格式。
- 回归：覆盖本次修复的具体问题。
- 路径敏感相关：常量边界安全/越界、有/无 guard 的符号边界、do-while 首次执行。

要点：
- 每个 case 注释标注象限与理由；FP/FN 不是"测试失败"，是对边界的诚实记录。
- 结构体测试遵循 `typedef struct XxxTag { ... } Xxx;`（tag 带后缀、typedef 名干净）；union/enum 同理。
- 每个测试说明：预期告警数量、预期告警关键子串。

## 2. 测试用例落点与文件头

## 2. 测试用例落点、目录结构与构建登记

**落点**：`BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/`，每规则一子目录（目录名用**大驼峰**的规则名，如 `ResourceDoubleFreeChecker`）。目录内按需组织：单文件、或 `src/`+`include/` 均可。**优先把所有用例集中一个文件**（运行用 `-analysis_file`）；确需分散才用 `-analysis_folder=<目录>/`。

```text
BXPlugin/xir/llvm_ast/TestCase/
├── CMakeLists.txt                      # 父级：最后追加一行 add_subdirectory(<CheckerName>)
└── <CheckerName>/                      # 规则用例根目录（大驼峰）
    ├── CMakeLists.txt                  # 该目录自身的构建文件（必须有）
    ├── TC_<CheckerName>.c              # 单文件形态
    └── (或) src/*.c[pp] + include/     # 多文件形态
```

**该目录根的 `CMakeLists.txt`**——单测试文件：

```cmake
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(ResourceDoubleFreeChecker
        TC_ResourceDoubleFreeChecker.cpp)
```

多测试文件（`src/` 放用例、`include/` 放公共头，用 `file(GLOB ...)` 收集）：

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

> `add_library()` 的 target 名唯一即可（参考既有用例的写法，可带或不带 `TC_` 前缀）；模板见 `templates/test_cmakelists.txt`。

**父级登记**：在 `BXPlugin/xir/llvm_ast/TestCase/CMakeLists.txt` 的**最后追加一行**引入该目录：

```cmake
add_subdirectory(<CheckerName>)
```

**文件头必须含四块**：① 作者/日期/描述 ② `bxen` 运行命令注释 ③ issue 链接占位 `DTSXXXX / https:xxxx` ④ case 列表。

**作者动态获取**：优先 `git config --list`（或 `git config --list --global`）取 `user.name` 作为实际作者；取不到则回退默认 `g00586360`。

```c
/*
 * Author : <git user.name，取不到则 g00586360>
 * Date   : 2026-05-29
 * Desc   : Test cases for rb.enhance.core.IntegerNarrowingThenWideningCast —
 *          <one line: what this checker detects and the boundary>
 *
 * Run (single file):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen \
 *     -exec_mode=onlySa \
 *     -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
 *     -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/IntegerNarrowingThenWideningCastChecker/TC_IntegerNarrowingThenWideningCastChecker.c \
 *     -checker=rb.enhance.core.IntegerNarrowingThenWideningCast \
 *     "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *   # 多文件分散：-analysis_file 换成 -analysis_folder=<该用例目录>/
 *   # ipa 包(跨函数)规则：去掉 ipa=none，按需开启 IPA
 *
 * Issue  : DTSXXXX / https:xxxx
 *
 * Cases:
 *   TP1 - <desc>      (expect: report)
 *   TN1 - <desc>      (expect: no report)
 *   FP1 - <desc>      (expect: report, KNOWN FALSE POSITIVE)
 *   FN1 - <desc>      (expect: no report, KNOWN/ACCEPTED FALSE NEGATIVE)
 *   B1  - <boundary>  (expect: ...)
 *   R1  - <regression>(expect: ...)
 */

typedef struct SampleTag {
    int field;
} Sample;

// ---- TP1: <why true positive> ----
void Tp1(void)
{
    // expect: report here
}

// ---- TN1: <why safe> ----
void Tn1(void)
{
    // expect: no report
}

// ---- FP1: KNOWN FALSE POSITIVE — <why accepted / how to mitigate> ----
void Fp1(void)
{
    // expect: report (acknowledged false positive)
}

// ---- FN1: KNOWN/ACCEPTED FALSE NEGATIVE — <why out of boundary> ----
void Fn1(void)
{
    // expect: no report (acknowledged miss)
}
```

> 预期告警：项目支持 FileCheck 风格注释就写 expected-warning；否则在 README 写明预期告警数量与关键子串。测试用例 C/C++ 同样遵循 `.clang-format`。

## 3. 合并版 README 模板（含边界/取舍）

README 是规则边界与取舍的权威载体，须与代码、测试三处一致。建议结构：

```markdown
# <CheckerName>   (package: rb.enhance.core.<CheckerNameWithoutChecker>)

## 1. 规则目的
检测什么缺陷模式、为什么要检测。

## 2. 风险说明
对应的安全 / 可靠性 / 可维护性 / 编码规范风险。

## 3. 检测策略
AST-only / 路径敏感 CSA / LibTooling 预扫描+CSA / 混合；关键 source 与 sink；
路径/展开假设（path-sensitive 适用：loop 展开 N、IPA 模式）。

## 4. 支持场景
1. ...

## 5. 检测边界与取舍（TP/TN/FP/FN）
| 象限 | 场景 | 处理 | 理由 |
|---|---|---|---|
| TP | ... | 报 | ... |
| TN | ... | 不报 | ... |
| FP | ... | 误报 | 接受/缓解：... |
| FN | ... | 漏报 | 工业取舍：为压低误报而裁掉，因为 ... |

不支持场景示例措辞：
> 该 checker 当前不做完整的用户自定义 sanitizer 函数过程间识别。这是有意取舍，
> 因为无函数契约时识别任意校验函数易引入大量误报或漏报。

## 6. 示例
### 6.1 违规示例（附 English warning message）
### 6.2 合规示例
### 6.3 边界示例

## 7. 测试覆盖
| 场景 | 预期 |
|---|---|
| 直接违规 (TP) | 告警 |
| 安全反例 (TN) | 不告警 |
| 已知误报 (FP) | 告警（已记录） |
| 已知漏报 (FN) | 不告警（已记录） |
| 边界 / 类型转换 | 按规则 |
| 嵌套成员路径 | 告警含稳定路径 |
| 回归 | 按修复预期 |

## 8. 注册与构建
- 源码：BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/<CheckerName>.cpp
- CMakeLists：add_library() 追加到最后
- Checkers.td：package（core/ipa/已确认），追加到对应包块最后
- 编译：cd 构建目录 → cmake ... ../ → make -j16

## 9. 运行方式
（单文件 / 整目录 bxen 命令，见 registration-build-run.md；core 用 ipa=none，ipa 规则开 IPA）

## 10. 备注
实现说明、CSA 已知限制、后续优化方向。
```

工业偏好声明：优先低漏报，但不以不可接受的误报为代价；每处边界裁剪给 justification。

## 4. git commit message（默认中文，scope=规则全名）

凡产出代码都附（除非用户明说不要）。模板见 `templates/git_commit_message.md`，结构：

```
<type>(rb.enhance.core.CheckName): <中文概括>

修改背景：
- <为何修改：补漏报 / 降误报 / 完善边界 等>

核心改动：
- <改动点>

规则边界：
- 支持：<...>
- 不支持：<...及原因>
- 取舍：<误报/漏报取舍>

测试说明：
- 新增/更新 <测试文件或函数>，覆盖 TP/TN/FP/FN + 边界/回归
- 验证命令：/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa ...

影响范围：
- <checker / README / 测试 / 注册项>

Issue: DTSXXXX
```

type 取 feat/fix/refactor/test/docs/build；**scope 用规则全名**（`rb.enhance.core.CheckName` 或 `rb.enhance.ipa.CheckName`）；正文中文；带 issue 号。

## 5. 自检
- 四象限 + 边界/回归齐全、每个 case 标注象限与理由？
- 用例落 `TestCase/<CheckerName>/`（大驼峰）、优先集中单文件、目录根有自身 `CMakeLists.txt`、父级 `TestCase/CMakeLists.txt` 已追加 `add_subdirectory(<CheckerName>)`？
- 文件头四块齐全（动态作者→回退 g00586360 / 运行命令含正确 cc_json 与引号 / DTSXXXX 占位 / case list）？
- 结构体用 `XxxTag`/`Xxx`？预期告警数与关键子串写明？
- README 含边界/取舍且与代码、测试一致？commit 已给（中文，scope=规则全名，带 issue）？
