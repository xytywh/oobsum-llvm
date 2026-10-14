# 附录：LibTooling 预扫描双层规则（备选方案）

> **定位**：这是一条**备选**路径，不是默认。绝大多数规则用 CSA 即可。**只有**当需要全工程/跨 TU 信息、且 CSA 实现不了或成本远高于 LibTooling 时，才作为备选提给需求方。**需求方未明确提出就不用。**

## 1. 何时考虑

- 需要扫描**所有函数/文件**的某类信息再做判定，例如：
  - 收集全工程的宏定义；
  - 基于所有文件做全局变量赋值的全局扫描；
  - 跨 TU 唯一性判断（如 unique allocation global）；
  - 稳定 size 符号收集。
- 这类需求超出"单 TU、单文件"的 CSA 工作面，用 CSA 表达会很别扭或很贵。

## 2. 关键限制

LibTooling 预扫描**只有 AST 能力，没有路径敏感分析**。凡涉及路径可行性、符号值、约束求解的判定，仍须落到 CSA checker。所以双层规则里 LibTooling 只做"全局信息收集/预计算"，真正的缺陷判定能放 CSA 就放 CSA。

## 3. 分层模式

```text
Layer 1：LibTooling 预扫描
  输入：compile_commands.json（CommonOptionsParser / ClangTool 遍历）
  处理：RecursiveASTVisitor 收集全工程信息
  输出：JSON 中间结果

Layer 2：CSA checker
  输入：源码文件 + analyzer 状态 + Layer1 的 JSON 中间结果
  输出：诊断告警（仍遵循本 skill 的 CSA 全部约定）
```

需要源码改写时，用 LibTooling + `Rewriter`。

## 4. 典型目录布局

```text
BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/<rule>_checker/
├── Common/            # 双层共用的数据结构/常量
├── Layer1_PreScan/    # LibTooling 预扫描，产出 JSON
├── Layer2_CSAChecker/ # CSA checker，消费 JSON
├── include/
├── src/
└── config/            # 规则配置
```

## 5. 与默认流程的关系

- 决策时若判断"CSA 能做且不贵"，**直接走 CSA**，不要引入双层复杂度。
- 若确需双层，先与需求方对齐：哪部分放 Layer1（纯收集）、哪部分放 Layer2（缺陷判定），以及 JSON 中间格式。
- Layer2 的 CSA checker 部分照常遵循本 skill 的全部约定（命名、报告、category、typedef 名、测试四象限、README、编译运行）。
