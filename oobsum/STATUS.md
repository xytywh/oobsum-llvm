# STATUS — 现在做到哪、下一步做什么

> 每次会话开始先读这里；每次提交一批改动后更新这里（"已完成"挪进 CHANGELOG 时可以删）。
> 规则在 `../CLAUDE.md`，背景与决策在 `HANDOFF.md`，主规格 `06-spec.md`，增量规格 `specs/`。

更新：2026-10-15 · 分支 `oobsum/dev` · 版本 v4.0

## 已完成

* 仓库化：调研包 v3.0–v3.15 逐版导入为提交（`git log -- oobsum/`）；`CLAUDE.md`、`HANDOFF.md`、`COMPANY.md`、`specs/` 流程、
  `history/` 纪要；`devenv/` 改为在本仓库里构建（`cmake-build-release/`）；`land.py` 支持仓库内清单（同步语义、`--check-tree`）。
* checker 骨架（SPEC-0001）：
  * `rb.enhance.ipa.OobSummary` 注册；选项 `Phase`、`WorkDir`、`Policy`、`InlineDepth`、`ReportSafe`、`SuspectReasons`、`Dump`、`DumpFilter`。
  * 标签 DEFINITE / POSSIBLE / SUSPECT / SAFE（`core/Label`）、判定（`core/Decision`）、报告策略（`core/Policy`）、指纹（`core/Fingerprint`）。
  * 告警：每个标签一个 BugType，消息 `[OOB-<标签>[:<原因>]] …`，按（文件、行、列、指纹）排序（`report/Reporter`）。
  * 判定日志 `WorkDir/decisions/<tu-id>.jsonl`，报了的、没报的都在（`core/DecisionLog`）；`oobsum/tools/decisions.py` 汇总。
  * 调试输出 `Dump=options+callgraph+decisions+stats`、`DumpFilter`（`debug/DumpSink`）；`fir`、`state`、`summary`、`apply` 已预留，等引擎。
  * 调试内建函数 `oobsum_debug_emit(label, reason, depth)`：用例里驱动"判定 → 标签 → 告警 / 日志"。
  * lit：`clang/test/Analysis/oobsum/` 6 个用例全过；公司 TestCase `oobsum/testcase/OobSummaryChecker/`（预期 3 条告警）。
* 冒烟第 6 项修复：`replay.py` 在 jsonschema 3.x（Ubuntu 22.04 的 `python3-jsonschema`）下解析 schema 的相对 `$id` 出错崩溃；
  现在校验前去掉 `$id`，校验器自身出错时打印异常并退回内置检查。`40-smoke.sh` 7 项全过。
* S1 回归尺子（SPEC-0002）：`python3 oobsum/cases/run.py csa` 用本仓库的 clang（`-cc1 -analyze`，`armebv7-linux-gnueabi`）跑 67 个用例 / 70 个 TU，
  读判定日志按 `@expect` 对照（口径修订 06- T-03：SUSPECT 计为召回），并与入库基线 `oobsum/cases/csa-baseline.json` 比"召回不回退"
  （原来报出的判定消失、同一处又没有 SAFE → 失败）。**当前基线**（引擎未接入）：134 个入口 通过 69（全是 `-` / TN 的空通过）、漏报 63、
  需跨TU 2（LV1-07、LV6-01），报出 0 条。lit `cases-csa.test`（合成用例在 `Inputs/cases/`）。CLAUDE.md 的每提交检查加上 `run.py csa` 无回退。

## 下一步（小步，每步一个提交；做完一步勾掉一步）

- [x] **S1 回归尺子**：`oobsum/cases/run.py csa`——用 `cmake-build-release/bin/clang -cc1 -analyze` 跑 67 个用例（含跨 TU 的多文件用例），
      `WorkDir` 收判定日志，按用例头 `@expect`（TP/TN + pa=L1/L2/记账）对照，输出通过表。引擎没接上时全部"未判"——这正是基线。
      SPEC-0002 写清 `@expect` 与标签的对应（L1→DEFINITE、L2→POSSIBLE、记账→SUSPECT 或日志、TN→不报或 SAFE）。
- [ ] **S2 函数键**（06- D-01…D-08）：`lower/Keys`；`Dump=keys` 或并入 `Dump=fir`；lit 覆盖 static / extern / 头文件 static inline。
- [ ] **S3 FIR 结构与 JSON**（D-10…D-14）：`core/Fir`、`core/FirJson`（读写往返）；`core/` 单元测试的最小框架（不依赖 gtest，06- §9.3）。
- [ ] **S4 降级第一刀**（A-01…A-09 的直线代码子集）：赋值、数组下标、成员、指针算术、调用、返回；`Dump=fir` 快照用例。
- [ ] **S5 降级补全**：分支 / 循环（assume 边）、`?:`、`&&`/`||`、`switch`；`unsupported` 计入判定日志（SUSPECT:unsupported_construct）。
- [ ] **S6 表达式代数**（WP2，A-10…A-17）：`core/LinExpr`、`core/Bound`；与 `replay.py` 的 `Lin`/`Env` 同语义；约 120 条单元测试。
- [ ] **S7 函数内解释**（WP3）：状态、WTO、不动点、转移、访问检查与 Req 生成；`Dump=state`；depth 0 的判定接进标签与日志。
- [ ] **S8 摘要与代换**（WP4）：`core/Summary*`、`core/Apply`、`core/Solver`；`Dump=summary`、`Dump=apply`；Lv1 + Lv2 用例出告警；
      与 `cases/summaries/`（去掉 doc）和 `alerts.json` 对照（T-06）。
- [ ] S9 之后：WP5 安全函数族模型 → WP7 循环与守卫 → WP8 效果与分配器 → WP9 大小模型 → WP10 驱动 → WP11 真实代码子集（公司）→ WP12 全量与性能 → WP13 冻结。

## 等用户提供 / 在公司做的

* **点头**：SPEC-0002 §7 提议把 07- / 06- T-09 的 G2"期望为 `-` / `记账` 的入口零告警"改成"零 DEFINITE / POSSIBLE 告警，SUSPECT 条数单列"
  （D16 召回优先的直接推论；不点头的话，G2 时 TN 上的 SUSPECT 都算不通过）。
* 公司：`oobsum/devenv/fork-facts.sh` 的输出；I0 探针 R1–R3；本骨架在 bxen 下的 TestCase 结果（预期 3 条告警 + 判定日志 5 条）。
* 其余输入见 `HANDOFF.md` §6。

## 已知问题与注意

* `-analyzer-config` 的列表值用 `+` 连接（逗号会被当成键值对分隔符）。
* 上游 clang 14 不支持 aarch64 ILP32；本地布局近似用 `TRIPLE=armebv7-linux-gnueabi`。
* 调试内建函数只用于测试；引擎接上后保留它（便于单独测策略与日志），但业务代码里不会出现。
* `summary.schema.json` 的 `$id` 是相对的（`oobsum-summary/1`）：别的脚本要用 jsonschema 校验摘要，也要先去掉 `$id`（jsonschema 3.x 会解析失败，见 `replay.py`）。
* `run.py csa` 的基线只在有意改变时 `--save`（不能与 `--only` 同用），提交信息里写清楚哪些入口的实际变了；`csa-result.json` 在
  `cmake-build-release/oobsum-csa/`。`alerts.json` / `replay.py` 仍把 `记账` 当成不报，补不补 SUSPECT 条目留到 S8 另写 SPEC。
* `InlineDepth` 默认 2：浅层的 DEFINITE / POSSIBLE 交给现有规则（仍写日志，原因 `inline_depth`）；要全报设 0。
