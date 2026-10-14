# 项目上下文

## 1. 仓库结构

```text
BXPlugin/                         # 顶层入口：main 入口、上层数据流规则编排、与静态分析平台集成
├── BinExplorer/                  # submodule：数据流分析实现
│   ├── 过程间分析
│   ├── 函数摘要构建
│   ├── use-def chain 构建
│   └── 基于 BE-IR 的数据流分析
└── xir/                          # submodule：文件级 AST + BE-IR + 内嵌 CSA + 自定义 checker
    ├── include/clang/StaticAnalyzer/Checkers/Checkers.td   # checker 声明（TableGen）
    └── llvm_ast/
        ├── StaticAnalyzer/Checkers/      # checker 源码 *.cpp/*.h
        │   └── CMakeLists.txt            # 源文件登记
        └── TestCase/                     # 测试用例（每规则一子目录）
```

## 2. 模块职责

- **BXPlugin**：顶层入口仓库——main 入口、上层数据流规则构建、静态分析任务编排、与平台集成。
- **BinExplorer**：数据流分析的具体实现——过程间分析、函数摘要构建、use-def chain、基于 BE-IR 的数据流分析。
- **xir**：文件级 AST 实现、BE-IR 中间表示构建、内嵌/搬移 Clang Static Analyzer、**自定义 CSA checker 开发**、按需优化 CSA 底层能力。

### 2.1 关键澄清：CSA 规则都在 xir 内实现

**所有 CSA checker——包括跨函数的 `rb.enhance.ipa` 路径敏感规则——都纯粹在 `xir/llvm_ast` 内，使用 CSA 自身的 IPA（过程间分析）能力实现，与 `BinExplorer` 无关。**

不要因为"跨函数"就把规则导向 BinExplorer：BinExplorer 是另一条基于 BE-IR 的数据流分析线，与 CSA checker 是两套体系。CSA 的跨函数能力通过 analyzer 的 inlining/IPA 配置（运行时 `-analyzer-config ipa=...`）提供。

## 3. 新增 checker 的落点（五处）

```text
源码：       BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/<CheckerName>.cpp
源码 CMake： BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt      (add_library() 追加到最后)
td：         BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td    (按包路由追加到最后)
用例目录：   BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/                      (大驼峰；含自身 CMakeLists.txt；优先集中单文件)
用例登记：   BXPlugin/xir/llvm_ast/TestCase/CMakeLists.txt                      (最后追加 add_subdirectory(<CheckerName>))
```

测试目录自身 `CMakeLists.txt` 的单/多文件写法见 `references/test-and-readme.md` 与 `templates/test_cmakelists.txt`。

## 4. 包路由表（`Checkers.td` ParentPackage）

| 规则类型 | 默认 package | ParentPackage | 运行时 ipa |
|---|---|---|---|
| AST 规则 | `rb.enhance.core` | `RosaRBEnhanceCore` | `ipa=none` |
| 单函数规则 | `rb.enhance.core` | `RosaRBEnhanceCore` | `ipa=none` |
| 单函数路径敏感规则 | `rb.enhance.core` | `RosaRBEnhanceCore` | `ipa=none` |
| 跨函数、单文件 路径敏感规则 | `rb.enhance.ipa` | `RosaRBEnhanceIpa` | 开启 IPA |
| 跨文件 / 跨 TU / 全工程规则 | **需向用户确认** | **需向用户确认** | — |

```td
let ParentPackage = RosaRBEnhanceCore in {
  // checker definition
} // end "rb.enhance.core"

let ParentPackage = RosaRBEnhanceIpa in {
  // checker definition
} // end "rb.enhance.ipa"
```

新增条目放在对应 package 块**最后**，`Checkers.td` 描述文本保持英文。

## 5. 工业规则设计原则

- 安全类/质量类规则优先**降低漏报**；必须显式说明误报/漏报取舍。
- 某场景会导致大量误报时，可明确标记为暂不支持，并写明理由。
- 规则边界与取舍要在 **README、代码注释、测试用例** 三处一致体现。

## 6. 常见路径速查

```text
顶层：           /usr1/BXPlugin
构建目录(固定)： /usr1/BXPlugin/cmake-build-release-release      # 不存在则 mkdir 后直接使用，不探测
bxen：           /usr1/BXPlugin/cmake-build-release-release/bxen
LLVM 14 根：     /usr1/llvm-project-14/cmake-build-release/
rbchecker 配置： /usr1/clang_check/src/cfg/ini/config.ini
funcptr 配置：   /usr1/clang_check/src/cfg/ini/config_funcptr.ini
```

## 7. 双层规则目录（附录场景，见 libtooling-prescan-appendix.md）

需要 LibTooling 预扫描 + CSA 的双层规则时，典型布局：

```text
.../StaticAnalyzer/Checkers/<rule>_checker/
├── Common/
├── Layer1_PreScan/        # LibTooling 预扫描，输出 JSON
├── Layer2_CSAChecker/     # CSA checker，消费 JSON + analyzer 状态
├── include/
├── src/
└── config/
```
