---
name: csa-checker-dev
description: 设计、实现、修改、调试、评审、测试、文档化 Clang Static Analyzer (CSA) checker 的完整工程化工作流，面向 LLVM/Clang 14 工业级 C/C++ 代码库，规则归入 BXPlugin/xir 的 rb.enhance.core / rb.enhance.ipa 体系，用 bxen 运行。Use this skill whenever the user wants to write, design, refactor, debug, review, test, or document a CSA checker / Clang 静态分析检查器 / clang static analyzer rule — including AST-visitor checkers (RecursiveASTVisitor), path-sensitive checkers (checkPreCall/checkBind/checkLocation/checkPreStmt 等), Checkers.td 注册与包路由, CMakeLists 登记, 编译与 bxen 运行命令, 诊断信息, struct-typedef 名解析, TP/TN/FP/FN 正交测试用例, 检测边界与误报/漏报取舍, README, 以及 git commit message。Trigger even when the user only says "写个 checker / 加一条静态分析规则 / detect XXX in CSA / 这个 checker 误报漏报了 / 帮 checker 补测试或 README" without naming the framework. 也可在用户询问 LibTooling 预扫描是否更合适时触发（见附录）。
---

# CSA Checker 工程化开发（BXPlugin / xir / rb.enhance.*）

把"写一条 CSA checker"做成 **设计 → 选范式定包 → 实现 → 落地五处 → 编译 → bxen 验证 → 测试 → README → commit** 的完整闭环，并固化 `BXPlugin/xir` 体系的全部工程约定。默认 **LLVM/Clang 14**，面向用户解释用**中文**，但**诊断文案与 `Checkers.td` 一律英文**。

> 阅读顺序：先读完本文件（架构 + 原则 + 工作流 + 决策 + 规范摘要），再按需点开 `references/`。**不要跳过设计阶段直接写代码**——CSA checker 的成败几乎全在边界设计上。**实现与测试用例一律从 `templates/` 复制后替换占位，禁止从空白文件写起**——测试文件头四块已内置于 `templates/test_case.c`，从模板起步可天然避免遗漏。

---

## 1. 项目架构与文件落点

顶层 `BXPlugin`，下挂两个 submodule：

- **`BinExplorer`** — 数据流分析实现：过程间分析、函数摘要、use-def chain、基于 BE-IR 的数据流。
- **`xir`** — 文件级 AST、BE-IR 构建、**内嵌 Clang Static Analyzer 与自定义 CSA checker 开发**。

> **关键**：所有 CSA checker（含跨函数的 `rb.enhance.ipa` 规则）都**纯粹在 `xir/llvm_ast` 内用 CSA 自身的 IPA 能力实现，与 `BinExplorer` 无关**。不要把 CSA 规则的跨函数分析误导向 BinExplorer。

新增一条规则要**同时改动五处**（源码、测试用例+其 CMakeLists、测试目录登记、源文件登记、td 声明），改完**必须重新编译再运行验证**：

| 内容 | 路径 | 要点 |
|---|---|---|
| checker 源码 `.cpp`/`.h` | `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/<CheckerName>.cpp` | |
| 测试用例（+ 自身 CMakeLists） | `BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/`（含 `CMakeLists.txt`、`TC_<CheckerName>.c` 或 `src/`、`include/`） | 每规则一子目录（规则名大驼峰），**优先集中单文件**；目录根放一个 `CMakeLists.txt` |
| 测试目录登记 | `BXPlugin/xir/llvm_ast/TestCase/CMakeLists.txt` | **最后追加一行** `add_subdirectory(<CheckerName>)` |
| 源文件登记 | `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` | 加进 `add_library()`，**追加到最后**，保持原对齐，不重排既有行 |
| checker 声明 | `BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` | 按包路由（§4），**追加到对应包块最后**，描述英文 |

> ⚠️ **实测最易遗漏的两处**：① `TestCase/CMakeLists.txt` 的 `add_subdirectory(<CheckerName>)`；② 测试文件头四块。两者都**不会引发编译错误**（前者只有在 bxen 按"预期告警数"严格核验时才间接暴露为找不到用例 TU / 0 告警，后者完全静默），所以收尾**必须**用 `scripts/check_checker_conventions.py` 机械核验（见 §9）。

详见 `references/project-context.md`（仓库结构/职责）与 `references/registration-build-run.md`（落地/编译/运行）。

---

## 2. 黄金原则（每条规则都遵守）

1. **设计先行**：先用需求归一化模板（`references/checker-workflow.md`）把"检测什么、分析对象、触发点/sink、需证明的条件、支持/不支持、漏报/误报取舍"写清楚，再动键盘。复杂规则先与用户对齐设计。
2. **正交测试**：每条规则默认覆盖 **TP / TN / FP / FN** 四象限（真阳、真阴、已知误报、已知/可接受漏报），并在四象限骨架上**补充边界与回归**用例。
3. **边界与取舍可追溯**：在规则代码、测试用例、README 三处都写清"检测边界 + TP/TN/FP/FN 取舍"。**工业偏好：优先压低漏报率，但不以不可接受的误报为代价**——每处边界裁剪都要给 justification。
4. **告警要全，且不被分类吞掉**：报告优先 `generateNonFatalErrorNode`（避免同路径多 issue 被截断）；`BugType` 的 category 仅用于**归类展示**，**严禁**作为过滤/抑制告警的判据。
5. **诊断只用英文**：`Checkers.td` 与所有 `BugReport` 文案英文（中文注释/文档不受限；Python 注释/docstring 默认中文）。
6. **保留用户注释**：改用户代码时原有注释一律保留（仅在明显错误/过时时纠正），可新增补充注释，绝不静默删除；优先最小化局部修改。
7. **写完就给 commit message**：凡产出代码都附 commit message（除非用户明说不要），**默认中文**，scope 用规则全名（如 `rb.enhance.core.CheckName`）。

---

## 3. 工作流总览

```
① 需求归一化 & 设计     → references/checker-workflow.md（归一化模板 + 边界/取舍预案）
② 选范式 + 定包         → §4 决策 + 包路由
③ 实现 checker          → references/ast-visitor-checker.md 或 path-sensitive-checker.md + templates/
④ 处理 API 陷阱         → references/csa-api-cookbook.md（cast / 原始 Expr 取 SVal / typedef 名 / 报告 / category / 确定性）
⑤ 落地五处             → references/registration-build-run.md（源码 / 源码 CMakeLists / Checkers.td / 用例目录+其 CMakeLists / TestCase 父 CMakeLists 的 add_subdirectory）
⑥ 编译 + bxen 验证      → references/registration-build-run.md（完整编译命令 make -j16 + 运行命令 + 失败排查 + 验证输出）
⑦ 正交测试用例          → references/test-and-readme.md（四象限 + 边界/回归 + 文件头四块）
⑧ README（含边界/取舍） → references/test-and-readme.md（合并版 README 模板）
⑨ git commit message    → 默认中文，scope=规则全名（templates/git_commit_message.md）
⑩ 收尾自检（强制）      → 跑 scripts/check_checker_conventions.py + 重读 §9 逐项核对，最终回答输出 ✅/❌
```

调试不顺时走 `references/checker-workflow.md` 的 10 步调试流程。实现中若发现边界设计有洞，回到 ① 修正，不要在代码里打补丁糊弄。

---

## 4. 决策：范式 + 包路由

### 4.1 范式：AST-visitor 还是 path-sensitive

| 维度 | **AST-visitor**（语法/类型层） | **path-sensitive**（路径敏感/符号执行层） |
|---|---|---|
| 基类 | `Checker<check::ASTCodeBody>` + 内嵌 `RecursiveASTVisitor` | `Checker<check::PreCall, check::Bind, check::PreStmt<...>, ...>` |
| 看到 | AST 节点、类型、声明、字面量、语法结构 | `ProgramState`、`SVal`、`SymbolRef`、`MemRegion`、约束、执行路径 |
| 适合 | 类型不匹配、危险 cast、函数指针调用形态、结构体成员访问形态、声明/命名规约 | 空指针、越界、未初始化、内存泄漏、taint、值域/约束相关缺陷 |
| 路径条件 | ❌ 无法感知分支 | ✅ 能感知路径约束 |
| 特征 | 便宜、确定性强；误报多来自"语法像但语义不构成缺陷" | 贵、依赖路径覆盖；漏报多来自"路径未探索 / loop 展开不足" |

启发式：能在单个 AST 子树/类型签名上判定 → AST-visitor（更稳更便宜）；依赖变量在某路径上的值/状态/约束 → path-sensitive。二者皆可时工业上优先 AST-visitor。**AST 规则足够时不要强行上符号执行。**

第三条路（备选）：需要全工程信息/跨 TU 唯一性/全局扫描（如扫所有宏定义、全局变量赋值全局扫描）且 CSA 实现不了或成本远高于 LibTooling 时，可向需求方提 **LibTooling 预扫描** 备选方案——但它**只有 AST 能力、无路径敏感**，**默认仍走 CSA**，需求方没明确提就不用。详见 `references/libtooling-prescan-appendix.md`。

### 4.2 包路由（决定 `Checkers.td` 的父包）

**默认按下表；跨文件/跨 TU/全工程规则或拿不准的，先与用户确认包，不要默认放置。** 无论哪个包，新增规则追加到该包块最后。

| 规则类型 | 父包 | 包名 | 运行时 ipa |
|---|---|---|---|
| AST 规则 **或** 单函数路径敏感规则 | `RosaRBEnhanceCore` | `rb.enhance.core` | `ipa=none` |
| 跨函数、单文件 路径敏感规则（用 CSA IPA） | `RosaRBEnhanceIpa` | `rb.enhance.ipa` | 开启 IPA（不要 `ipa=none`） |
| 跨文件 / 跨 TU / 全工程 | **先与用户确认**，不默认放置 | — | — |

选定后读 `references/ast-visitor-checker.md` 或 `references/path-sensitive-checker.md`。

---

## 5. 全局编码规范（摘要，详见 references/coding-style.md）

### 5.1 命名

**默认规则**：**变量（局部变量、函数参数、类成员变量）一律 lowerCamelCase；函数（普通函数、类成员函数）默认 UpperCamelCase。** 唯一例外见下表"同类一致性"行——类内回调是 `checkXXX` 小驼峰时全类统一小驼峰。

| 场景 | 风格 | 例 |
|---|---|---|
| 局部变量 / 函数参数 / 类成员变量 | lowerCamelCase | `baseExpr`, `const Expr *argExpr`, `fieldName` |
| 普通函数 / 类成员函数（默认） | UpperCamelCase | `ResolveRecordName`, `VisitMemberExpr` |
| `RecursiveASTVisitor` 的 `VisitXX` 回调及同级方法 | UpperCamelCase（与默认一致） | `VisitMemberExpr` |
| path-sensitive `checkXX` 回调 | lowerCamelCase（贴合继承的 CSA API） | `checkPreStmt`, `checkPreCall` |
| **同类一致性**：若某个类继承/重写的回调是 `checkXXX` 小驼峰（路径敏感的 `checkPreCall`/`checkBind`/`checkPreStmt` 等，**以及 AST checker 外层类的 `checkASTCodeBody`**），则**该类内所有其它函数/helper 也统一 lowerCamelCase**，与回调保持一致，**不**按默认改用大驼峰。RAV 的嵌套 `Visitor`（回调是 `VisitXX` 大驼峰）则照默认走大驼峰 | lowerCamelCase（全类统一） | `reportBug`, `isTaintedArg` |
| 单语句 `if`/`while`/`for`/`do` 体 | **必须带 `{}`** | `if (x) { return; }` |

### 5.2 `.clang-format`（C/C++ 一律遵守）

Google 基底，关键项：4 空格缩进、行宽 120、`PointerAlignment: Right`（`int *p`）、`SortIncludes: false`（**手工分组、不排序**）、`UseTab: Never`、`AllowShortFunctionsOnASingleLine: None`、`AllowShortBlocksOnASingleLine: false`、`AccessModifierOffset: -4`（`public:`/`private:` 与 `class` 齐平）、`NamespaceIndentation: None`、`Standard: Cpp11`、`ContinuationIndentWidth: 4`、`MaxEmptyLinesToKeep: 1`、`BreakBeforeBraces: Custom` 且**仅 `AfterFunction: true`**（函数/方法定义左大括号换行，控制流不换行）。

### 5.3 头文件

每个 checker `.cpp` 顶部包含 `#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"`；include **按逻辑分组手工维护，不排序**。

---

## 6. CSA API 关键陷阱（速记，详见 references/csa-api-cookbook.md）

1. **`generateNonFatalErrorNode` 优先于 `generateErrorNode`**：同路径多 issue 时 fatal node 会截断后续，造成漏报；仅在确需 sink 中止路径时才 fatal。
2. **判类型剥括号用 `IgnoreParenCasts`，不要用 `IgnoreParenImpCasts`**：后者不剥显式 cast，会让类型判断失配。
3. **取 `SVal`/`MemRegion` 用原始 `Expr`**（未经 `IgnoreXXX`）：处理过的 expr 可能因死符号清理返回 null。判类型用剥离后的、取值用原始的。
4. **结构体名诊断走 typedef 反查**：因 `typedef struct XxxTag {} Xxx;`，`RecordDecl::getName()` 返回 `XxxTag`。优先级：**① typedef 反查 clean 名 → ② `RecordDecl::getName()` tag 名 → ③ `"<anonymous struct/union>"`**。
5. **BugType category 仅用于展示**：传非空英文、按缺陷性质选（内置 `categories::*` 或自定义英文串），**绝不用于过滤/抑制**。
6. **多报告确定性**：按 source location 排序后再报，保证 CI 输出稳定。
7. **默认 LLVM 14 API；为用到的每个 CSA/AST 类型显式包含其定义头，不依赖间接包含；include 路径以 LLVM 14 环境实际为准，不要凭记忆**。典型坑：调用 `AnalysisManager` 成员需 `clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h`（否则 `incomplete type`）；`PathDiagnosticLocation` 在 `clang/Analysis/PathDiagnostic.h`（**非**旧版 `StaticAnalyzer/Core/BugReporter/`）。头文件速查见 `references/csa-api-cookbook.md` §0。

---

## 7. References / Templates 索引

References（按需读，勿一次全读）：
- `references/project-context.md` — 仓库结构、模块职责、五处落点、包路由表、IPA 在 xir 内实现的澄清。
- `references/checker-workflow.md` — 需求归一化模板、实现检查清单、10 步调试流程、完整闭环。
- `references/coding-style.md` — `.clang-format` 全量、命名分层、注释/语言约定、include 不排序。
- `references/ast-visitor-checker.md` — AST 范式骨架（含两遍遍历/别名定点）。
- `references/path-sensitive-checker.md` — 路径敏感骨架（`ProgramState` trait / 报告 / 确定性 / 路径覆盖）。
- `references/csa-api-cookbook.md` — cast、原始 Expr 取 SVal、typedef 名反查实现、报告与 category、确定性排序、`APSInt` 值域、（附）函数名/成员路径参考配方。
- `references/registration-build-run.md` — `Checkers.td`、`CMakeLists`、完整编译命令（`make -j16`）、`bxen` 运行命令、失败排查清单、验证输出格式、循环配置、TableGen 坑。
- `references/test-and-readme.md` — 四象限+边界/回归方法论、测试文件头四块（含动态作者）、合并版 README（含边界/取舍）。
- `references/libtooling-prescan-appendix.md` — 附录：LibTooling 预扫描双层规则备选方案。

Templates（复制后替换占位）：
- `templates/ast_visitor_checker.cpp` / `path_sensitive_checker.cpp` / `checkers_td_entry.td` / `test_case.c` / `test_cmakelists.txt` / `rule_readme.md` / `git_commit_message.md`

Scripts：
- `scripts/check_checker_conventions.py` — **产物**约定 lint（§9 机械核对部分；存在 FAIL 时退出码非 0，CI 可直接复用）。
- `scripts/validate_skill.py` — 校验 skill 包自身完整性（与产物无关）。

---

## 8. 最终回答格式（完成一次实现/修改时）

1. 修改内容概述。2. 关键设计决策。3. 规则边界 / TP·TN·FP·FN 取舍。4. 改动文件清单。5. 新增/更新的测试（四象限+边界/回归，预期告警数与关键子串）。6. 登记情况：源码 `CMakeLists.txt`、`Checkers.td`（含包路由）、测试目录 `CMakeLists.txt` 与 `TestCase/CMakeLists.txt` 的 `add_subdirectory`。7. 完整编译命令与编译结果。8. `bxen` 验证命令与"实际告警数 vs 预期"。9. 中文 git commit message。10. 收尾自检结果：`check_checker_conventions.py` 输出摘要 + §9 清单逐项 ✅/❌（存在 FAIL/❌ 时不得宣布完成，修复后重跑直至全绿）。

用户要完整代码时给完整文件，不要只给片段。

---

## 9. 收尾自检（强制执行，结果随最终回答输出）

> 完成实现/修改后**重新读取本节**逐项核对——不要凭对规范的记忆作答。可机械核对项以脚本输出为准：
> `python3 scripts/check_checker_conventions.py --repo <BXPlugin根> --checker <CheckerName> [--build-dir <构建目录>]`
> （覆盖：五处落点含父级 `add_subdirectory`、`BuiltinCheckerRegistration.h`、注册函数后缀、td 条目英文与运行短名、`generateErrorNode`/`IgnoreParenImpCasts` 用法告警、字符串字面量英文、Tab/列宽、测试文件头四块；给 `--build-dir` 时联动核验 `compile_commands.json` 是否含用例 TU。）脚本不覆盖的判断项（范式/包路由理由、边界四象限取舍、README 一致性）逐条自查。**全部 ✅ 才允许宣布完成。**
- [ ] 范式与包路由有理由（core/ipa/已确认）？边界与四象限取舍写清？
- [ ] 命名 / `.clang-format` / 单语句控制流带 `{}` / include 不排序，全部合规？
- [ ] 含 `BuiltinCheckerRegistration.h`？用 `generateNonFatalErrorNode`？判类型用 `IgnoreParenCasts`、取值用原始 Expr？
- [ ] 结构体名走 typedef 反查（clean→tag→匿名）？诊断与 td 描述全英文？category 非空、按性质、不用于过滤？多报告确定性排序？
- [ ] 五处落点齐全（源码 / `TestCase/<CheckerName>/` 含其 `CMakeLists.txt` / `TestCase/CMakeLists.txt` 追加 `add_subdirectory(<CheckerName>)` / 源码 CMakeLists 追加到最后 / Checkers.td 追加到最后）？
- [ ] 改动后执行了完整编译命令（`make -j16`）？失败是否如实说明并与用户对齐？
- [ ] `bxen` 命令：`-cc_json` 指向构建目录、整段 `"-extra_args=..."`、`-checker=` 用 `Checker<"...">` 运行短名（不带 `Checker` 后缀；def 名与注册函数才带后缀）、`max-loop 3`、core 用 `ipa=none` / ipa 规则开 IPA、带 `-funcptr-config`？运行失败是否与需求方对齐参数？
- [ ] 测试四象限+边界/回归齐全，文件头四块完整（动态作者→回退 `g00586360` / 运行命令 / `DTSXXXX` issue 占位 / case list）？
- [ ] README 含边界/取舍且与代码、测试一致？commit message 已给（默认中文，scope=规则全名）？保留了用户原有注释？
