# 交接说明：OobSummary（CSA Summary 2.0）从对话搬进仓库

> v4.0 · 2026-10-14 · 读者：Gehui，以及在 WSL 里接手开发的 Claude Code。
> 之前所有的设计、讨论和交付都在 claude.ai 的对话里进行，每一版打成调研包 `infer-research-kit-vX.Y.tar.gz`。
> 从 v4.0 起，**这个仓库（llvm-project 的 `oobsum/dev` 分支）就是唯一的工作场所**：设计、规格、代码、用例、脚本、历史都在这里，
> 每一步都是一个提交。开一个新的 Claude Code 窗口时，读 `CLAUDE.md` → `oobsum/STATUS.md` → 本文件，就能接着干。

---

## 1. 一句话与背景

用函数摘要检测**跨多层调用**的内存越界：每个函数算出"调用方必须满足什么才不越界"（Req）和"对调用方有什么影响"（Eff），
自底向上传递；在某个调用点代入实参后能判定了，就在那里报。不依赖 CSA 的路径敏感引擎，所以层数再多也不会路径爆炸。

* 已立项，处于实施阶段；交付日期 **2026-12-15**。立项时粗略统计目标层数 2–17 层；1–2 层现有规则（CSA inline）已经覆盖。
* 目标代码：公司的嵌入式 C 业务代码（aarch64_be + ILP32，万级源文件，`compile_commands.json` 全量可用；内存操作用
  securec 安全函数；大量函数指针）。
* 公司电脑有安全要求、不能做开发：**在自己的 Windows 电脑（WSL2 + Ubuntu 22.04）上开发**，推到 GitHub，公司电脑从 GitHub 拉下来、
  用 `land.py` 同步进 BXPlugin、`make -j16`、用 bxen 跑（`oobsum/COMPANY.md`）。
* 用户的工业偏好（v4.0 明确）：**宁可多报，不要漏报**；告警后面会接大模型复核做过滤；所以每条判定都要带标签、带原因，
  不报的也要留下记录。

## 2. 到目前为止（详细见 `git log` 与 `oobsum/CHANGELOG.md`）

`git log --oneline -- oobsum/` 里，v3.0–v3.15 每一版都是一个提交，提交说明就是当时交付时写的说明（含用户的反馈与决定）。
`oobsum/history/README.md` 按版本整理了"用户问了什么 → 我们决定了什么"。主线：

| 阶段 | 版本 | 产出 |
|---|---|---|
| Infer 调研 | v1–v2.9 | Infer 跨过程分析与函数摘要机制调研、受限网络下源码编译、能力边界与误报漏报全表（`oobsum/research/`） |
| 方案设计 | v2.3–v3.9 | 方案 C（自研摘要层）的总体设计、工作量、实测清单；T0-1 / T0-1b 证明 fork 与上游在相关行为上一致 |
| 立项与场景 | v3.6–v3.11 | 场景分级 Lv1–Lv10（年内承诺 26 个）；67 个分级用例（`oobsum/cases/`），每个用例头写好预期摘要 |
| 开工准备 | v3.12 | P0 规格 `06-spec.md`、实施计划 `07-dev-plan.md`、I0 集成探针 |
| 方案与摘要实样 | v3.13–v3.14 | 方案按五个独立问题重排；16 个用例 104 份摘要 JSON 与回放器；自然语言只放 `doc`，去掉 doc 就是完整摘要 |
| 本地开发环境 | v3.15 | WSL2 + 上游 llvm-project 14.0.0 的脚本（全部沙箱实跑） |
| **搬进仓库** | **v4.0** | 本仓库：历史导入、`CLAUDE.md`、本文件、规格流程（`specs/`）、checker 骨架（标签、召回优先策略、判定日志、调试输出） |

## 3. 已定的决策（不要重新论证；要推翻先写 SPEC）

来自 `06-spec.md` §12（D5′…D15，用户尚未逐条点头，按推荐执行）与 v4.0 新增（D16…D19，来自用户的明确要求）：

| # | 决策 | 一句话理由 |
|---|---|---|
| D5′ | 跨 TU：一遍导出 FIR → 离线求解 → 一遍出告警（方案 B） | 遍数固定为 2，与调用链跨多少文件无关 |
| D7 | `rb.enhance.ipa.OobSummary` / `OobSummaryChecker` | 跨函数规则归 ipa 包 |
| D9 | 引擎跑在自己的中间表示 FIR 上，不直接跑 `clang::CFG` | 与 Clang 解耦：可单测、可离线求解、可喂给 AI |
| D10 | 落盘用 JSON（`llvm::json`），C++ 侧不引入第三方库 | 不动 xir 的依赖 |
| D11 | 只挂 `check::ASTDecl<TranslationUnitDecl>` | 多挂 `EndOfTranslationUnit` 会让每个函数跑 ExprEngine（实测） |
| D12 | 告警报在汇合点的调用表达式上；跨文件的链写进 note / 消息 | 访问点常在别的 TU |
| D13 | 自定义函数键（外部链接用名字，内部链接带定义文件），不用 USR | C 没有重载；不依赖 clangIndex |
| X-04 | solve 用桩 TU 宿主 | 不改 BXPlugin 构建 |
| D14 | 摘要里的自然语言只放在 `doc` 字段，引擎不读 | 静态分析不消费自然语言；去掉 doc 就是完整摘要 |
| D15 | L2 消息只写界的种类，`bound_at` 留到 P1 | 先保证消息全部由机器字段生成 |
| **D16** | **判定四个标签 DEFINITE / POSSIBLE / SUSPECT / SAFE；召回优先：拿不准报 SUSPECT，只有证明安全才不报；所有判定写判定日志** | 用户要求：尽量不漏报，误报交给后续大模型复核（SPEC-0001） |
| **D17** | **每次设计 / 修改先写 `oobsum/specs/SPEC-NNNN`，再写代码；代码注释引用 SPEC 号** | 用户要求：可追溯、便于定位问题 |
| **D18** | **每个引擎环节配 `Dump` 种类（stderr + `WorkDir/dump/`），不影响结果** | 用户要求：像 CSA 的 dump 一样能观察中间数据 |
| **D19** | **小步提交并推送；仓库 = llvm-project `llvmorg-14.0.0` + `oobsum/dev`；公司从 GitHub 拉、`land.py` 同步** | 用户要求：每次开新窗口都能从提交记录接着干；公司能看到完整记录 |

D16 对 `06-spec.md` 的修订（详见 SPEC-0001 §9）：F-15（"S2 不输出"）改为 SUSPECT 按 `Policy` 输出；F-20 的层数过滤只作用于
DEFINITE / POSSIBLE（浅层的交给现有规则，仍写日志），SUSPECT 不按层数过滤；F-31 每个标签一个 BugType；F-32 消息加标签前缀。

## 4. 现状（v4.0）

* **规格与计划**：`oobsum/06-spec.md`（P0 规格，~800 行）、`07-dev-plan.md`（WP0–WP13 与日历）、`05-summary-design-and-cases.md`、
  `08-options-and-summary-examples.md`（方案对比与摘要实样）、`09-local-dev-wsl.md`（本地环境）。
* **金标准**：`oobsum/cases/`（67 个用例）、`oobsum/cases/summaries/`（16 个用例 104 份摘要、`replay.py`、`alerts.json`）。
  `replay.py` 是调用点判定（06- §6.6）与告警生成（F-30…F-35）的参考实现，C++ 引擎要与它逐项一致。
* **checker 骨架**（SPEC-0001）：注册与选项、四个标签与召回优先策略、告警（每标签一个 BugType、消息带 `[OOB-…]` 前缀）、
  判定日志 `WorkDir/decisions/<tu>.jsonl`、调试输出（`Dump=options+callgraph+decisions+stats`）、调试内建函数
  `oobsum_debug_emit()`（用例里用来驱动"判定 → 标签 → 告警 / 日志"这条路）。6 个 lit 用例全过。
  **引擎（FIR、抽象域、摘要、代换判定）还没有**——这是接下来的主线。
* **环境**：`oobsum/devenv/` 的脚本在沙箱里实跑过：上游 14.0.0 全量构建 2 核约 90 分钟（8 核约半小时）、构建目录 1.0 GB；
  上游 clang 14 不支持 aarch64 ILP32（本地用 `armebv7-linux-gnueabi` 近似）。

## 5. 接下来（按 `07-dev-plan.md` 的工作包，拆成小步，每步一个提交）

`oobsum/STATUS.md` 里有带编号的清单，这里是顺序与理由：

1. **用例驱动的回归脚手架**：`cases/run.py csa` 用本仓库的 clang 跑 67 个用例，收判定日志，按 `@expect` 与标签核对。先有尺子再写引擎。
2. **WP0 其余**：函数键（D-01…D-08）、FIR 数据结构与 JSON（D-10…D-14）、`Dump=fir`。
3. **WP1 降级**：Clang AST/CFG → FIR（A-01…A-09），FIR 快照用例（T-02）。
4. **WP2 表达式代数**：线性式、不透明项、界与来源标签（A-10…A-17）；`replay.py` 里有同语义的 Python 实现，可对照；配单元测试。
5. **WP3 函数内**：抽象解释、访问检查与 Req 生成（A-20…A-52）；`Dump=state`；depth 0 的判定接进标签与日志。
6. **WP4 跨函数**：摘要、调用点代换、调度、出告警（A-60…A-92）；`Dump=summary`、`Dump=apply`；与 `cases/summaries/` 和 `alerts.json` 对照（T-06）。
7. 之后 WP5–WP13：安全函数族模型、循环与守卫、效果与分配器、大小模型、驱动、真实代码子集、全量与性能、冻结交付。

每一步都按 `CLAUDE.md` §5：先 SPEC、再代码、带 lit 用例与 dump、编译测试全过、提交并推送、更新 STATUS。

## 6. 还等用户提供的输入（`07-dev-plan.md` §2）

| # | 输入 | 没有时的默认 |
|---|---|---|
| I1 | 点头 D5′…D15（以及 v4.0 的 D16…D19） | 按推荐执行 |
| I1′ | 公司机器上 `oobsum/devenv/fork-facts.sh` 的输出 | 按上游 14.0.0 + Release 对齐 |
| I2 / I2′ | I0 探针在公司跑的 R1、R2、R3 输出（`oobsum/probes/i0-integration/README.md`） | 按沙箱结果写 |
| I4 | bxen 全量模式的并发方式、告警落在哪 | 串行、从输出里按 checker 名计数 |
| I5 | 底层分配器原型（字节数是第几个形参）、`config.ini` 的 `MEM_ALLOC` 段、`alloc_free_identity` 结果 | 用例里的原型 |
| I6 | 现有规则实际的 inline 深度 | 2 |
| I8 / I9 | 内部访问类封装清单；已知纯函数（日志、打点）清单 | 只建模安全函数族与 libc；通用命名规律 |
| I10 | 验收数值（报出率、精确率、全量耗时、峰值内存） | 80% / 80% / 8 小时 / 64 GB |

## 7. 一次往返怎么走

```
 WSL（开发）                          GitHub（私有仓库，分支 oobsum/dev）          公司电脑（验证）
 ─────────────                        ─────────────────────────────           ─────────────
 Claude Code 小步开发 → 编译 → 测试  ─push─▶  完整提交记录（规格 + 代码 + 用例） ─pull─▶ land.py 同步进 BXPlugin
 → 提交（中文说明，引用 SPEC）                                                    → make -j16 → bxen 跑 TestCase / 业务代码
                     ◀────────────── 用户带回不敏感的结论：编译报错、告警计数、耗时 ──────────────
```

公司那边的命令见 `oobsum/COMPANY.md`。

## 8. 安全提醒

这个仓库里有公司内部的路径（`/usr1/BXPlugin`）、工号（用例文件头的作者）、内部函数名（`VOS_MemAlloc` 等）与产品名。
**GitHub 仓库请设成私有**：GitHub 上 fork 的公开仓库只能是公开的，所以不要用 Fork 按钮，而是新建一个私有仓库，把本分支推上去
（交接包里的 `import.sh` 就是这么做的）。公司电脑能不能访问个人的 GitHub 私有仓库，以公司的规定为准。
