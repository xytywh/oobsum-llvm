# Infer 跨过程分析与函数摘要机制 —— 调研包（当前 v3.2）

一句话：**这个包里只有一份"正文"（报告），其余全是为了让你能亲手复现、读懂、并在自己代码库上用起来的材料。**

调研基线：`facebook/infer` commit `3d38229b04b649024fee065621f6a4c838f6d029`（main，2026-09-10）；
实验用官方预编译 `infer v1.3.0`，以及从源码自建的 `v1.3.0-3d38229b`（含本包里的 MiniOobChecker）。

> **v1 → v2 变更清单见 [`CHANGELOG.md`](CHANGELOG.md)。** 新增了前置知识、全量工程分析、
> 可观测点手册、受限环境构建脚本四块内容，报告增补了三个附录。
> **v2.1** 修了 `gen-compile-commands.sh` 不建 `build/` 目录导致 capture 失败的问题，
> 并新增「开跑之前：先体检 compile_commands.json」一节（真实工程同样会踩）。
> **v2.2** 新增 `07-capability-boundary/`（混淆矩阵 + 漏报的机制/参数归因 + 参数代价表，附可复现语料），
> 收录使用方的跨编译工具链 capture 修复文档与脚本，
> 并**更正了报告 §13 场景 #6 的结论**（结构体字段数组只在基址是形参或 malloc 时才检出）。
> **v2.3** 新增 `08-summary2-design/`（方案 C 的可开工设计 + 98–151 人日工作量估算 +
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

08-summary2-design/                     ★ 方案 C 从"建议"到"可开工"
   01-architecture.md                          组件/数据结构/算法/接口，每条决策挂证伪条件
   02-effort-and-milestones.md                 98–151 人日，分 M0–M5；含"让 Claude 写代码"的估算
   03-assumptions-and-test-plan.md             ★ 先测这个。Tier-0 四项半天，能推翻设计
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
| **★ 准备立项 / 排期** | `08-summary2-design/` 三篇，**但先跑 `03-assumptions-and-test-plan.md` 的 Tier-0** | 读 1.5 小时 + 测半天 |
| **交叉编译工程 capture 一片红** | `06-practice/03-cross-toolchain-capture-fix.md` | 视工具链而定 |
| **★ 第一次在真实代码库上跑** | `06-practice/04-end-to-end-runbook.md`，① 到 ⑧ 照着敲 | 半天 |
| **`infer analyze` 跑完了想知道成没成** | `cd 06-practice && ./diagnose-analyze.sh <infer-out> <analyze.log>` | 2 分钟 |
| **亲手改 Infer、评估 OCaml 门槛** | `04-setup/01-restricted-network-build.md` → `03-MiniOobChecker/README.md` | 半天 |

---

## 快速开始（复制粘贴即可）

```bash
# 1) 装 infer（不要一上来就从源码编译）
curl -L -o infer.tar.xz \
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

> **但这条推荐有一个尚未验证的前提**：「调用深度是主要瓶颈」来自团队自述，
> 还没有被你们代码库上的数据验证过。`08-summary2-design/03-assumptions-and-test-plan.md`
> 的 **T0-3 / T0-4** 就是验证它的（半天）。若验证不通过，方案 C 应当降级，
> 改走 `02-effort-and-milestones.md` §5 那个 **15–20 人日拿 70% 收益**的选项。

---

## 不在这个包里的东西

* **Infer 源码本身**：请自行 `git clone https://github.com/facebook/infer.git`，
  报告里所有源码引用都标了文件路径与行号，对着上面那个 commit 看即可。
* **官方预编译二进制**（276 MB）：下载命令见上。
* **我们调研时对 Infer 工作树做的沙箱适配改动**（为绕开不可达的 charon 而摘掉 Rust 前端）：
  那是一次性环境 hack，不该进入你们的代码。需要时由
  `04-setup/scripts/45-strip-rust-frontend.sh` 按需施加。
