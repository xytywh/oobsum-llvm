# CLAUDE.md — OobSummary（CSA Summary 2.0）开发须知

> 这个文件每次会话自动加载。它是**常驻规则**；"现在做到哪、下一步做什么"在 `oobsum/STATUS.md`，背景与已定决策在 `oobsum/HANDOFF.md`。
> 本仓库 = 上游 llvm-project `llvmorg-14.0.0` + 我们的分支 `oobsum/dev`。用户是 Gehui（静态分析工程师，CSA checker 维护者）。

## 1. 在做什么

`rb.enhance.ipa.OobSummary`：一个 Clang Static Analyzer checker，用**函数摘要**检测**跨多层调用**的内存越界（C 语言）。

* 目标代码：公司的嵌入式 C 业务代码（aarch64_be + ILP32，约 1 万个源文件，内存操作用 securec 安全函数 `memcpy_s` 等）。
  **业务代码不在这里，也永远不进这个仓库**——这里只有我们自己写的合成用例。
* 现有规则（CSA 的 inline）只覆盖 1–2 层；本规则覆盖 3 层及以上，靠"自底向上算摘要、在调用点代换判定"，不依赖路径敏感的 ExprEngine。
* 交付：2026-12-15。代码最终编进公司的 BXPlugin/xir（公司 fork 的 LLVM 14），用 bxen 运行。本仓库里的代码通过
  `oobsum/devenv/land.py` 同步过去（五处落点，见 `oobsum/COMPANY.md`）。

## 2. 每次会话开始先做

1. 读 `oobsum/STATUS.md`（现状、下一步、待用户提供的输入、已知问题）。
2. `git log --oneline -20` 和 `git status`，看最近做了什么、有没有没提交的改动。
3. 读和本次任务相关的规格：`oobsum/06-spec.md`（主规格）+ `oobsum/specs/` 里的增量规格（SPEC-NNNN，新的覆盖旧的）。
4. 不要重新论证已定的决策（`oobsum/HANDOFF.md` §3）。有新理由要推翻，先写 SPEC 提出来、在提交信息里说明。

## 3. 目录

| 路径 | 内容 |
|---|---|
| `clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp` | checker 入口（选项、TU 回调、把判定交给 Reporter） |
| `clang/lib/StaticAnalyzer/Checkers/OobSummary/core/` | **不依赖 Clang**：标签、判定、策略、指纹、判定日志；以后的 FIR、抽象域、摘要、代换、求解都在这里 |
| `clang/lib/StaticAnalyzer/Checkers/OobSummary/lower/` | Clang AST/CFG → FIR（只有这里碰 Clang AST） |
| `clang/lib/StaticAnalyzer/Checkers/OobSummary/report/`、`debug/` | 判定 → CSA 告警；调试输出 |
| `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td` | 末尾的 `rb.enhance.*` 包块（本地镜像公司的包结构）与 `OobSummaryChecker` 条目 |
| `clang/test/Analysis/oobsum/` | lit 用例（`-verify` / FileCheck） |
| `oobsum/06-spec.md`、`07-dev-plan.md` | 主规格、实施计划（工作包 WP0–WP13） |
| `oobsum/specs/` | 增量规格 SPEC-NNNN（每次设计 / 行为修改一份，模板在 `specs/README.md`） |
| `oobsum/cases/` | 67 个分级用例（Lv1–Lv10，文件头有 `@expect` 与预期摘要）；`cases/summaries/`：16 个用例 104 份摘要金标准 + `replay.py` + `alerts.json` |
| `oobsum/devenv/` | 构建、冒烟、运行、落地脚本（`30-build-llvm.sh`、`40-smoke.sh`、`cc1-run.sh`、`land.py`…） |
| `oobsum/landing/`、`oobsum/testcase/` | 同步到公司的清单；公司 TestCase 目录（bxen 用） |
| `oobsum/probes/` | 公司侧集成探针（I0） |
| `oobsum/research/` | 前期 Infer 调研（背景，只读） |
| `.claude/skills/csa-checker-dev/` | 用户的 CSA checker 开发规范（项目技能）。注意它写的是公司路径，见 §6 的对应关系 |
| `.claude/settings.json` | 构建、测试、提交、推送到 `oobsum/dev` 免确认；禁止强推与 `reset --hard`（用户可自行改） |
| `oobsum/PROMPTS.md` | 给 Claude Code 的提示词：首次启动、每次新窗口、带回公司结果、版本节点 |

## 4. 构建与测试（WSL / Ubuntu 22.04）

构建目录是仓库根下的 `cmake-build-release/`（已在 `.gitignore`）。第一次全量构建由用户跑（8 核约半小时）：`oobsum/devenv/30-build-llvm.sh`。

```bash
ninja -C cmake-build-release clang                       # 增量：改 .cpp 约 1 分钟；改 Checkers.td 会重编全部 checker（几分钟）
cmake-build-release/bin/llvm-lit -sv cmake-build-release/tools/clang/test/Analysis/oobsum   # lit 用例
python3 oobsum/cases/summaries/replay.py                 # 摘要金标准回放（只要 Python 3）
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --check-tree   # 新文件是否都登记了
oobsum/devenv/40-smoke.sh                                # 整体冒烟（7 项）
oobsum/devenv/cc1-run.sh -checker=rb.enhance.ipa.OobSummary <文件.c> [-analyzer-config rb.enhance.ipa.OobSummary:Dump=stats]
```

* 构建超过几分钟时放后台跑并轮询，不要让一个命令卡到超时。
* `-analyzer-config` 里的列表值用 **`+`** 连接（`Dump=callgraph+decisions`）：逗号会被当成键值对分隔符（实测）。
* 上游 clang 14 不支持 aarch64 ILP32；本地要"4 字节指针 + 大端"时用 `TRIPLE=armebv7-linux-gnueabi oobsum/devenv/cc1-run.sh …`。

## 5. 工作方式（用户的硬性要求）

1. **小步快跑**：一个小能力 / 一处修改 = 一个提交。每个提交都要：能编译、lit 全过、`replay.py` 不回退、`--check-tree` 一致；然后
   **`git push`**。不要攒一大堆改动一次提交，也不要提交编不过的半成品。
2. **先写 spec 再写代码**：每次设计或行为修改，在 `oobsum/specs/` 新增 `SPEC-NNNN-<slug>.md`（模板见 `specs/README.md`），写清来源、
   改了哪些规格条目、实现位置、怎么测、怎么调试。改到 `06-spec.md` 已有条目时，在原条目处注明"由 SPEC-NNNN 修订"。
   代码注释引用 SPEC 编号与 06- 条目号，方便从代码追到设计。
3. **每条判定都带标签**（SPEC-0001）：`DEFINITE`（明确越界）/ `POSSIBLE`（代码给出的界下可能越界）/ `SUSPECT`（证明不了安全）/
   `SAFE`（证明了安全）。新的判定来源必须给标签和原因（reason）。
4. **宁可多报，不要漏报**：拿不准就是 `SUSPECT`（带原因），只有证明了安全才是 `SAFE`；不报的判定也一律写进判定日志
   （`WorkDir/decisions/*.jsonl`），日志会交给大模型复核过滤。任何"提高精度"的改动，都不能让原来报的变成不报，除非证明了安全
   ——用回归用例守住。
5. **可调试**：每个新的引擎环节都配一个 `Dump` 种类（`fir`、`state`、`summary`、`apply`…），打印到 stderr（`[oobsum:<种类>]` 开头）
   并写 `WorkDir/dump/`；`DumpFilter` 按函数名过滤。调试输出**不得影响结果**，并配一个用到它的 lit 用例。结果不符合预期时，
   先用 dump 看中间数据，再改代码——就像用 `debug.DumpCFG`、`ExprInspection` 看 SVal 和约束。
6. **测试**：每个提交带 lit 用例；`core/` 的纯逻辑配单元测试；`oobsum/cases/` 与 `cases/summaries/` 是金标准，引擎接上后
   与它对照（06- T-06）。`alerts.json`、`@expect` 只在有意改变时更新，并在提交信息里说明。
7. **收尾**：更新 `oobsum/STATUS.md`（做了什么、下一步、新发现的问题）；版本节点更新 `oobsum/CHANGELOG.md`。

提交信息（中文，`csa-checker-dev` 格式）：

```
feat(rb.enhance.ipa.OobSummary): <一句话>

修改背景：…（SPEC-NNNN）
核心改动：…
规则边界：支持 / 不支持 / 取舍（召回优先）
测试说明：新增 / 更新的 lit 用例；验证命令；结果
影响范围：…
```

type 取 feat / fix / refactor / test / docs / build；纯文档用 `docs(oobsum): …`。

## 6. 代码约定（摘自 `.claude/skills/csa-checker-dev`，冲突时以它为准）

* LLVM/Clang 14 API；C++14、无 RTTI、无异常（与 LLVM 一致）。`.cpp` 顶部包含 `clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h`；
  用到的类型显式包含头文件，include 手工分组、不排序。
* `.clang-format`：Google 基底、4 空格、行宽 120、`PointerAlignment: Right`、函数定义左大括号换行、单语句 `if` 也要 `{}`。
* 命名：变量（局部、参数、成员）lowerCamelCase；函数默认 UpperCamelCase；重写 `checkXXX` 回调的类里全部函数 lowerCamelCase。
  def 名 / 类名 / 文件名 / `register…` 带 `Checker` 后缀，`Checker<"...">` 与 `-checker=` 用不带后缀的短名。
* **分层**：`core/` 不得包含任何 `clang/` 头文件；`lower/` 只产出 FIR；只有 `OobSummaryChecker.cpp`（和 `report/`）同时看到两边（06- §9.1）。
* 只挂 `check::ASTDecl<TranslationUnitDecl>`，**不挂任何路径敏感回调**（多挂一个 `EndOfTranslationUnit` 就会让每个函数跑一遍 ExprEngine，实测）。
* `Checkers.td` 与所有告警文本只用英文；代码注释、文档用中文。
* **文档和注释里不写 `clang --analyze -Xanalyzer` 这种形式**；本地写 `clang -cc1 -analyze …` 或 `cc1-run.sh`，公司写 bxen 命令。
* 修改已有文件时保留原有注释（明显错误或过时的才改），可以加新注释。
* 只改这些上游文件：`Checkers.td` 末尾的 `rb.enhance.*` 块、`clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt`（源文件列表末尾追加）。
  其余都在 `OobSummaryChecker.cpp`、`OobSummary/`、`clang/test/Analysis/oobsum/`、`oobsum/` 里。

公司路径与本仓库的对应（land.py 按这个同步）：

| 公司 BXPlugin | 本仓库 |
|---|---|
| `xir/llvm_ast/StaticAnalyzer/Checkers/` | `clang/lib/StaticAnalyzer/Checkers/` |
| `xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` | `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td` |
| `xir/llvm_ast/TestCase/OobSummaryChecker/` | `oobsum/testcase/OobSummaryChecker/` |
| `make -j16` + bxen | `ninja -C cmake-build-release clang` + `cc1-run.sh` / lit |

## 7. 不要做

* 不要把任何公司代码、业务告警原文、内部数据写进仓库。
* 不要为了"让用例过"降低召回：宁可报 `SUSPECT`。
* 不要绕过失败的测试提交；不要 `git push --force` 到 `oobsum/dev`。
* 不要改 `oobsum/research/`（历史调研，只读）和 `oobsum/cases/` 的用例源码（用例的 `@expect` 可以按 SPEC 有意更新）。
