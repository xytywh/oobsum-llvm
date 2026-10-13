# Infer 跨过程分析与函数摘要机制 —— 调研包（当前 v3.13）

一句话：**这个包里只有一份"正文"（报告），其余全是为了让你能亲手复现、读懂、并在自己代码库上用起来的材料。**

> **v3.7 起，项目已立项、进入实施阶段。先看这一份：
> [`08-summary2-design/00-implementation-plan.md`](08-summary2-design/00-implementation-plan.md)**
> —— 方案一句话、P0/P1/P2 优先级、倒排到 **2026-12-15** 的周计划与五个检查点、人力、验收标准、需要你提供的输入。
> `01-report/` 是调研阶段的报告（回答"为什么选这条路"），其中 §25/§28 的路线与 §29 的 PoC 已被那份计划取代。
> **v3.10 起，做哪些场景、承诺到什么程度看 [`08-summary2-design/04-scenario-tiers.md`](08-summary2-design/04-scenario-tiers.md)**
> —— 58 个场景（v3.11 补 2 个）按 Lv1–Lv10 从简单到复杂排好，年内承诺 26 个，程序分析的覆盖上限与 AI + 程序分析的分工都在里面。
> **v3.11 起，摘要到底长什么样、怎么取舍、多层怎么串起来检测，看
> [`08-summary2-design/05-summary-design-and-cases.md`](08-summary2-design/05-summary-design-and-cases.md)**
> —— 配套 `cases/` 里 67 个按级别写的跨函数用例（每个用例头部写好摘要与串联过程），`cases/run.py` 一键编译检查 / 用 Infer 对照。
> **v3.12 起，开始写代码：代码写成什么样看 [`08-summary2-design/06-spec.md`](08-summary2-design/06-spec.md)（规格说明），
> 按什么顺序写、每周交什么、需要你提供什么看 [`08-summary2-design/07-dev-plan.md`](08-summary2-design/07-dev-plan.md)**
> —— 写代码前先在 fork 里跑一次 I0 集成探针（`probes/i0-integration/`）。
> **v3.13：方案备选按五个独立问题重排、摘要看真实 JSON，见 [`08-summary2-design/08-options-and-summary-examples.md`](08-summary2-design/08-options-and-summary-examples.md)**
> —— 16 个用例、104 份函数摘要在 `cases/summaries/`，`python3 replay.py` 在每个调用点重做代换并对照期望。

调研基线：`facebook/infer` commit `3d38229b04b649024fee065621f6a4c838f6d029`（main，2026-09-10）；
实验用官方预编译 `infer v1.3.0`，以及从源码自建的 `v1.3.0-3d38229b`（含本包里的 MiniOobChecker）。

> **v1 → v2 变更清单见 [`CHANGELOG.md`](CHANGELOG.md)。** 新增了前置知识、全量工程分析、
> 可观测点手册、受限环境构建脚本四块内容，报告增补了三个附录。
> **v2.1** 修了 `gen-compile-commands.sh` 不建 `build/` 目录导致 capture 失败的问题，
> 并新增「开跑之前：先体检 compile_commands.json」一节（真实工程同样会踩）。
> **v2.2** 新增 `07-capability-boundary/`（混淆矩阵 + 漏报的机制/参数归因 + 参数代价表，附可复现语料），
> 收录使用方的跨编译工具链 capture 修复文档与脚本，
> 并**更正了报告 §13 场景 #6 的结论**（结构体字段数组只在基址是形参或 malloc 时才检出）。
> **v2.3** 新增 `08-summary2-design/`（方案 C 的可开工设计 + 98–151 人日工作量估算（v3.4 修正为 99–149）+
> 每条决策的证伪条件 + 分层实测清单），报告增补附录 E 并修正了 §25.2 的架构主次关系。
> **v2.4** 同步使用方对跨编译工具链两份文件的修订（方案 A 参数组修正：
> `--clang-libcxx-include-to-override-regex` 实测不独立生效，改用 `--Xclang -I` 注入 libc++），
> 并把其中的**真实全量数据回填**进相关文档：
> **1383 工程 10232 个唯一源文件 100% 落库、328602 个过程 —— 报告 §31 的第一风险已过关**。
> **v2.5** **全部目录与文件名改为英文**（根目录 `Infer调研包-v2` → `infer-research-kit`），
> 文档正文仍是中文。这一版只动文件名和文件里的路径引用，**没有任何内容改动**。
>
> **v2.6** 修了 `04-setup/scripts/20-prepare-opam-repo.sh` 的顺序 bug
> （预填缓存先于 `opam init`，导致 init 被跳过却"看起来成功"），
> 并给所有长任务加上进度与耗时输出。
> **v2.7** 再修两个实测踩出来的 bug（脚本 20 没 clone infer 源码就去读它的 lock 文件；
> `diagnose-analyze.sh` 把别的进程的 OOM 记成 infer 的、且没装 sqlite3 时把成功误判成失败），
> 并新增 `06-practice/04-end-to-end-runbook.md` 端到端操作手册。
> **v2.8** 新增 `04-setup/scripts/25-prefetch-git-sources.sh`：装依赖时大面积 `HTTP 403`
> 而手工 `git clone` 又是通的，就用它把源串行拉到本地，让 opam 不再碰网络；
> 同时把 opam 下载并发默认压到 4。
> **v2.9** 修了 v2.8 引入的参数错误（`--dl-jobs` 不是 `opam install` 的选项，
> 会让整轮装依赖直接失败），改走 `OPAMDLJOBS`；装依赖前先 `--dry-run` 试算，
> 几秒钟就能抓到参数错误或依赖解不开，不再白跑 40 分钟。
> **v3.0** 新增 `patch_git_source_quirks.py`：把源改成 git 会让 opam 的 `dev` 变量
> 从 false 翻成 true，激活上游没人测过的构建分支（实测 `fpath` 因此装不上）。
> 脚本 20 现在会自动修这类问题。
> **v3.1** 新增 `04-setup/scripts/opam-env.sh`（`source` 用）。本套脚本的 opam root 在
> `$WORK/opamroot` 而非 `~/.opam`，在交互 shell 里手敲 `opam` 会报
> `Opam has not been initialised`，照字面去 `opam init` 又会撞上缺 bubblewrap ——
> 这是一条**错误提示把人带偏**的连环坑，v3.1 从提示语、文档、脚本三处堵死。
> **v3.2** 新增 `04-setup/scripts/run-all.sh`：干净窗口从零构建，`./run-all.sh` 一条命令跑完
> 00→10→20→30→取预编译包→40→50，支持 `FROM=<步号>` 断点续跑。
> 同时补全了文档里那份**不完整**的手动命令序列（缺条件步骤 25、缺 `OPAMROOT`/`DLJ`、
> 缺最后把 infer 加进 PATH）。
> **v3.3** 三项来自使用方实测的加固：① **infer 源码钉在调研基线 `3d38229b`**
> （旧版 clone 的是最新 main，MiniOobChecker 补丁迟早会因上下文漂移打不上）；
> ② 预编译包下载挪进新脚本 `35-fetch-prebuilt.sh`，失效的 `SSL_CERT_FILE` 不再让 curl 报 (77)；
> ③ `run-all.sh` 先打执行计划、逐步 `[i/N]`、失败即停并告诉你怎么单独重跑那一步、最后出汇总表。
> **v3.4** 回填 **T0-1 实测**（方案 C 的第一个 Tier-0 数据）：fork 上 `getStaticSize` 存在、对象层 8/8 正确、
> 与上游 LLVM 14.0.6 逐行一致；但原探针测错了层（打的是整个 struct，不是字段数组）。
> 新增 `08-summary2-design/probes/t0-1b/` 字段层复核探针，并在上游实测出 CSA 14 的三处盲区
> （指向数组中间的指针 + 符号下标连地址都没有、多维只到整数组级、柔性数组 `[1]`）。
> 工作量 98–151 → **99–149 人日**。
> **v3.5** 回填 **T0-1b**：fork 上 19 行探针输出与上游逐行一致，**A1 成立**（字段数组 extent 7/7 正确），
> 三处盲区在 fork 上同样复现。新增 `08-summary2-design/probes/t0-3/t03_depth.py`，
> 从 Infer 告警或历史 DTS 单统计跨过程深度（同时给出 T0-2 的判读），并修正了旧版把两种深度混成一个数的问题。
> **v3.6** **已立项，转入实施阶段**：目标确定为支持现有工具因路径爆炸而不支持的跨多层调用越界场景
> （立项统计 2–17 层，不设上限，验收到 20 层）。T0-2/3/4 不再作为前置条件，省钱路线标为不采用；
> 新增"摘要方式受不受层数限制"的说明与"通道分工"（摘要通道只报 > 2 层的，CSA 已知盲区例外）。
> **v3.7** 新增 **`08-summary2-design/00-implementation-plan.md`**：唯一的"方案与计划"，倒排到 2026-12-15（56 个工作日）。
> 设计补两块：申请函数直接复用现有 `config.ini` 的 `MEM_ALLOC`/`MEM_FREE` 与 `alloc_free_identity` 识别结果，
> 封装函数的申请大小由摘要自底向上推出（`01-` §3.7）；跨 TU 的执行模型（`01-` §3.11）。
> **v3.8** 同步使用方修订的 `06-practice/capture-cross-toolchain.sh`：C++ 标准头改用 gcc-14.3.0 自带 libstdc++
> （主目录 / 平台目录 / `backward` 三个 `-I`），不再注入 infer 自带 libc++/v1；`03-cross-toolchain-capture-fix.md` 同步，
> 其中 2026-09-20 的全量数字是 libc++ 方案测的，附了重跑对照表待回填。
> **v3.9** 同步使用方对同一脚本的第二轮修订：目标参数改为 `--target=aarch64_be-linux-gnu -mbig-endian -m32`
> （指针真的 4 字节，旧的 `-U__LP64__ -D__ILP32__` 只改了宏），加 `-D__AARCH64EB__` 与 `__uint128_t` 替身，
> 新增查 `capture.db` 的进度监控。本版另修了一处会让 `capture.log` 变空的重定向
> （infer 的输出全走 stderr，**实测** stdout 为空），并把 `-m32` 的代价写进文档。
> **v3.10** 新增 **`08-summary2-design/04-scenario-tiers.md`**：按你们给的正交维度（调整后 8 个维度）把多层越界分成
> Lv1–Lv10 共 56 个场景，年内承诺 26 个；每个场景带最小代码（36 段全部过 clang 语法检查，并用 Infer 当标尺跑过）。
> 实施计划的验收改按场景级别，真实样例改为可选，现有 checker 源码不再需要；设计补了两处缺口（条件带守卫、访问点先化简）。
> **v3.11** 新增 **`08-summary2-design/05-summary-design-and-cases.md`** 与 **`cases/`**：Lv1–Lv10 共 67 个用例（70 个 .c、134 个 TP/TN 入口），
> 每个用例头部写好各层函数的摘要、调用点怎么代入、在哪一层判出；文档回答摘要形式、取舍规则、可判定性、切片、摘要单元与层数、
> 风险，并给出 A–G 七种"多个摘要怎么读写组合"的备选方案。用 Infer 当标尺跑过全部入口（104/134 与期望一致，
> 漏的 28 个逐条归因）。分级补 S4.6（校验函数）、S9.6（跨任务消息）→ 58 个场景；`01-` 补 §3.12（效应词表、条件效应、报告口径、账本）。
> **v3.12** 新增 **`06-spec.md`**（P0 规格：功能、选项、FIR 与摘要格式、算法、跨 TU 方案 B、非功能、测试与验收、追溯矩阵、8 个待点头决策）、
> **`07-dev-plan.md`**（10-14 → 12-15 的编码计划：开工输入 I1–I12、两环境闭环、14 个工作包、周历与检查点），
> 以及 **`probes/i0-integration/`**（只读集成探针 + 全量形态统计脚本；沙箱实测：挂 `EndOfTranslationUnit` 会让每个函数跑 ExprEngine，故改挂 `ASTDecl`）。
> **v3.13** 新增 **`08-options-and-summary-examples.md`**：旧 A–G 拆成五个独立问题（内容 / 生成 / 顺序 / 判定 / 跨文件）的选项矩阵，拼成 S1–S6 完整方案；
> **`cases/summaries/`**：16 个用例（Lv1–Lv10 每级至少一个）的 104 份函数摘要 JSON + JSON Schema + 回放器（32 个入口与期望一致）。
> `06-spec.md` §5.3 摘要格式定稿为可读 JSON，按 securec 真实语义细化安全函数条件，新增守卫界判定 F-12b。
>
> ⚠️ **v2.5 在 git 里是一次大规模重命名**（35 个条目 + 根目录）。
> 提交时用 `git add -A`，让 git 的 rename detection 生效，
> 否则会显示成"删 144 个文件 + 加 144 个文件"。

---

## 目录结构

```
README.md      ← 你现在看的，只是导航
CHANGELOG.md   ← 版本变更清单

01-report/                              ★ 正文。结论都在这里，其余目录都是它的附件
   infer-interprocedural-summary-research.md   完整报告（3200+ 行，含五个附录）
   report-web-offline.html                     决策摘要网页，评审会投屏用

02-prerequisites/                       报告里看不懂的概念在这里展开
   01-ocaml-for-reading-infer.md               包括「Location.t 里的 .t 到底是什么」
   02-abstract-interpretation-primer.md        join / 不动点 / widening，配真实 widening 轨迹
   03-separation-logic-and-biabduction.md      p |-> v、Frame Rule、溯因，配真实 Pulse 摘要
   04-glossary-csa-vs-infer.md                 左边你熟悉的，右边 Infer 的说法 + 源码位置

03-MiniOobChecker/                      报告 §19 的实物：真实编译跑通的跨过程越界 Checker
   src/  patch/  demo/  README.md

04-setup/                               三条路：预编译二进制 / 源码构建 / Docker
   01-restricted-network-build.md              回答「卡在 initializing opam」
   scripts/run-all.sh                          ★ 一把梭：一条命令从零跑到编译完成
   scripts/00…50                               受限网络下可直接执行的编号脚本
                                               25 是条件步骤：只在 30 报 HTTP 403 时才跑
   scripts/opam-env.sh                         ★ source 它（不是执行）再手敲 opam 命令
   Dockerfile-upstream                         官方 Dockerfile 原文

05-experiments/                         报告 §13–§17 每张数据表对应的 C 用例
   run-all.sh                                  一键跑完全部实验，逐组打印【预期】与实测
   00-basic-summary/  01-thirteen-scenarios/  02-call-depth/
   03-path-explosion/ 04-global-arrays/       05-incremental/

06-practice/                            ★ 上你们自己代码库要用的东西
   01-whole-program-analysis.md                ★ 全量 vs 逐文件，含对照实测
   02-observability-handbook.md                ★ 每步能看到什么、SQL 怎么查
   03-cross-toolchain-capture-fix.md           ★ 交叉编译工程 capture 一片红怎么办
                                               含真实全量结果：10232 源文件 100% 落库
   capture-cross-toolchain.sh                  上面那份的落地脚本
   04-end-to-end-runbook.md                    ★ 从 cdb 到结果的一条龙操作手册
   diagnose-analyze.sh                         跑完 analyze 先跑它：成功/失败判定 + 定位
   gen-compile-commands.sh                     解压后先跑它修正绝对路径
   observe.sh                                  一键把所有可观测点 dump 出来
   sql/queries.sql                             可直接跑的 SQL 速查
   demo-project/  demo-project-plain/          多文件工程 + compile_commands.json

07-capability-boundary/                 ★ 选型权衡用：Infer 到底能看住什么
   01-capability-boundary-fp-fn.md             ★ 混淆矩阵 + 漏报归因（机制 vs 参数）+ 参数代价表
   run-boundary.sh                             一键复现全部数字
   corpus/tp tn fp probe                       60 个正交语料（真越界 / 绝对安全 / 易误报 / 探针）

08-summary2-design/                     ★ 方案 C 的实施（已立项）
   00-implementation-plan.md                   ★★ 先看这个：方案、步骤、优先级、倒排到 12-15 的计划
   00-implementation-plan-offline.html         同一份计划的网页版（含甘特图，内网可直接用浏览器打开）
   04-scenario-tiers.md                        ★★ 场景分级：Lv1–Lv10、年内承诺 26 个、PA 覆盖上限、AI + PA 分工
   04-scenario-tiers-offline.html              同一份分级的网页版
   05-summary-design-and-cases.md              ★★ 摘要形式与取舍、可判定性、切片、多层串联、A–G 组合方案、用例统计与 Infer 对照
   05-summary-design-and-cases-offline.html    同一份的网页版（67 个用例卡片：摘要 + 代码，可按级别 / 是否报告筛选）
   06-spec.md                                  ★★ 规格说明（P0）：写代码以它为准
   07-dev-plan.md                              ★★ 编码实施计划：开工输入、工作包、周历、检查点
   06-07-spec-and-plan-offline.html            上面两份的网页版
   08-options-and-summary-examples.md          ★★ 方案按五个问题拆开比 + 16 个用例的真实摘要与逐步回放（专家讨论用）
   08-options-and-summary-examples-offline.html 同一份的网页版（方案矩阵可点选；用例浏览器：每个函数的 JSON + 回放）
   cases/                                      ★ 分级用例语料（v3.11）
      common.h                                 公共类型与安全函数声明；-DCASE_SHIM 时换成 Infer 认识的 malloc/memset
      lv1/ … lv10/                             67 个用例，头部 @id/@scene/@expect/摘要/串联；跨文件用例单独成目录
      demo/relevance_demo.c                    05- §1.2 的例子：一整个函数里摘要实际剩下什么
      run.py                                   index / check / infer / doc 四个子命令
      summaries/                               ★ 16 个用例的 104 份函数摘要 JSON（06- §5.3 的落盘格式）
         replay.py                             回放器：在每个调用点重做代换与判定，核对 JSON 与 @expect；--md 生成 08- 的 B.5
         summary.schema.json                   摘要格式的 JSON Schema
      INDEX.md                                 用例总表（run.py index 生成）
   01-architecture.md                          组件/数据结构/算法/接口，每条决策挂证伪条件
   02-effort-and-milestones.md                 99–149 人日，分 M0–M5；含"让 Claude 写代码"的估算
   03-assumptions-and-test-plan.md             实施阶段需要的输入 + 验证清单（T0-1 ✅ A1 成立；其余 Tier-0 已不需要）
   probes/t0-1b/                               T0-1b 字段层复核探针：checker + 用例，直接覆盖即可
   probes/t0-3/t03_depth.py                    跨过程深度统计（v3.6 起备用：从告警/DTS 里挑多层验收样例）
   probes/scenarios/snippets.py                从 04- 抽出场景最小代码：语法检查 / 用 Infer 当标尺跑对照表
   probes/i0-integration/                      ★ 开工前在 fork 里跑的集成探针（README 写了五处落点、三次运行、回传什么）
```

---

## 按你的目的选一条路

| 你想做什么 | 从哪开始 | 大概要多久 |
|---|---|---|
| **只想看结论** | `01-report/report-web-offline.html`，或完整报告的「执行摘要 + §33 核心问题结论」 | 15 分钟 |
| **报告里概念看不懂** | `02-prerequisites/`，四篇按顺序读 | 1–2 小时 |
| **验证结论是不是真的** | 按 `04-setup/README.md` 下预编译二进制，然后 `cd 05-experiments && ./run-all.sh` | 30 分钟 |
| **★ 在你们自己代码库上跑** | `06-practice/01-whole-program-analysis.md` | 半天（含切子集试水） |
| **★ 搞清楚每一步在干什么** | `06-practice/02-observability-handbook.md` + `./observe.sh` | 1 小时 |
| **★ 做选型权衡（能看住什么/看不住什么）** | `07-capability-boundary/01-capability-boundary-fp-fn.md` | 40 分钟 |
| **★ 和专家讨论方案 / 看摘要到底长什么样** | **`08-summary2-design/08-options-and-summary-examples.md`**（网页版可点选），`cases/summaries/replay.py -v` | 读 1 小时 |
| **★ 开始写代码（规格 + 计划 + 你要给什么）** | **`08-summary2-design/07-dev-plan.md` §0、§2**，再看 `06-spec.md` §12；探针 `probes/i0-integration/README.md` | 读 1 小时 + 探针 1 小时 |
| **★ 摘要长什么样、多层怎么串起来** | **`08-summary2-design/05-summary-design-and-cases.md`**，用例在 `cases/` | 读 1 小时 |
| **★ 实施排期（已立项）** | **`08-summary2-design/00-implementation-plan.md`**，细节再看 `01-architecture.md` | 读 30 分钟 |
| **交叉编译工程 capture 一片红** | `06-practice/03-cross-toolchain-capture-fix.md` | 视工具链而定 |
| **★ 第一次在真实代码库上跑** | `06-practice/04-end-to-end-runbook.md`，① 到 ⑧ 照着敲 | 半天 |
| **`infer analyze` 跑完了想知道成没成** | `cd 06-practice && ./diagnose-analyze.sh <infer-out> <analyze.log>` | 2 分钟 |
| **亲手改 Infer、评估 OCaml 门槛** | `04-setup/01-restricted-network-build.md` → `03-MiniOobChecker/README.md` | 半天 |

---

## 快速开始（复制粘贴即可）

```bash
# 1) 装 infer（不要一上来就从源码编译）
#    报 curl: (77) error setting certificate file 的话，先 unset SSL_CERT_FILE（只影响当前窗口）
curl -fL -o infer.tar.xz \
  https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
tar xf infer.tar.xz && export PATH=$PWD/infer-linux-x86_64-v1.3.0/bin:$PATH
infer --version                                  # 预期 Infer version v1.3.0

# 2) 一键复现报告里的全部实验（3–6 分钟）
cd 05-experiments && ./run-all.sh && cd ..

# 3) 在 demo 工程上体验「全量跨文件分析」
#    ★ 必须先跑 gen-compile-commands.sh：它把 compile_commands.json 里的 directory
#      改写成你解压后的真实绝对路径、建出 build/ 目录、再自检一遍。
#      跳过这步会看到 chdir(.../build): No such file or directory
cd 06-practice && ./gen-compile-commands.sh
cd demo-project
infer capture --compilation-database compile_commands.json
infer analyze --bufferoverrun-only
# 预期：src/main.c:5 报 BUFFER_OVERRUN_L1  Offset: 17 Size: 10 by call to `mid_fetch`
#      （17 = 5 来自 main.c + 12 来自 middle.c 自己的 -DIDX_BIAS）

# 4) 把这次运行的所有可观测点 dump 出来
../observe.sh infer-out          # 结果同时写入 observation-report.txt
```

---

## 报告最关键的四条结论（免得你翻）

1. **摘要里真的写着 requires**。BufferOverrun 的摘要主体是符号安全条件，真实文本
   `buf.offset + i < buf.length`，能沿调用链逐层代换。看它的命令是
   `infer debug --procedures-summary`，**不是** `--dump-json-summaries`。
2. **200 层调用链 0.76 秒**，摘要条数等于过程数，内存几乎不增长。
   "深度从 2 层提到十几层"在直线链上已被证实。
3. **循环不 unsupported 的秘密只有 10 行**：把比较条件里的常量收进 widening 阈值，
   于是 `i` 外推成 `[0,10]` 而不是 `[0,+∞]`。这条今天就能抄进你们的 CSA。
4. **但不建议直接用 Infer**：唯一有数组越界能力的 BufferOverrun 已被官方废弃，
   Pulse 在 22 个真实越界用例上**一条都不报**；Infer 自己在**结构体字段数组**、
   **全局数组**和**函数指针**上有结构性漏报，其中前两项**弱于你们现有的 CSA**。

5.（v2.2 补）**默认档 100% 精确率 / 82% 召回，高灵敏档不值得开**：
   47 个文件的正交语料上，高灵敏档召回只多 1 例，误报却从 0 个文件涨到 13 个。
   详见 `07-capability-boundary/`。

推荐路线是**方案 C：借鉴 Infer 重新设计 CSA Summary 2.0**，详见报告 §25 / §27 / §28，
落地设计与工作量在 `08-summary2-design/`。

> **v3.6：已立项，进入实施阶段。** 目标是支持现有工具因路径爆炸而不支持的跨多层调用越界场景
> （立项统计 2–17 层）。立项前"先验证深度是不是主要瓶颈"的那组测量不再需要，
> 只加深 inline 的省钱路线不采用（够不到 17 层，且会放大路径爆炸）。
> 摘要方式受不受层数限制、摘要通道与现有规则怎么分工，见 `08-summary2-design/01-architecture.md` §0.1。

---

## 不在这个包里的东西

* **Infer 源码本身**：请自行 `git clone https://github.com/facebook/infer.git`，
  然后 `git checkout 3d38229b04b649024fee065621f6a4c838f6d029`。
  报告里所有源码引用的文件路径与行号、MiniOobChecker 的补丁，都以这个 commit 为准。
* **官方预编译二进制**（276 MB）：下载命令见上。
* **我们调研时对 Infer 工作树做的沙箱适配改动**（为绕开不可达的 charon 而摘掉 Rust 前端）：
  那是一次性环境 hack，不该进入你们的代码。需要时由
  `04-setup/scripts/45-strip-rust-frontend.sh` 按需施加。
