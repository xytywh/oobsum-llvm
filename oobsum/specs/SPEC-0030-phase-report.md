# SPEC-0030 `Phase=report`：各 TU 按求解结果报出，写判定日志

状态：已实现（S9-4）
来源：`STATUS.md` S9；06- §7 X-05（report：驱动从原 cdb 原样复制出有判定的 TU，bxen 全量模式跑 `Phase=report`，TU id 一致）；SPEC-0001（策略、判定日志）、
SPEC-0029（`solved/<tu>.json`）。

## 1. 要解决什么

solve 在桩 TU 里算出了所有 TU 的判定，但告警要在各自的 TU 里发（CSA 的 `BugReporter` 只认本 TU 的位置）。

## 2. 设计

* `Phase=report`：读 `WorkDir/solved/<本 TU 的 id>.json` 里的判定（`DecisionFromJson`），与调试内建函数的判定一起交给 Reporter：按策略报出、写判定日志
  `decisions/<tu>.jsonl`（同 inline）。不在本 TU 跑引擎。
* 文件不存在 / 读不了：stderr 一行提示（这个 TU 没有被求解，或 TU id 对不上——X-05 要求原样复制 cdb 条目），只处理调试判定。
* 位置在本 TU 看不到的文件里（调用链跨文件时的中间跳）：Reporter 已有的处理（判定位置退回主文件开头并把原位置写进文本，A-91；看不到的跳不出 note）。
* `Dump=stats` 照常（`decisions=… emitted=…`）。

## 3. 对召回与误报的影响

与 solve 的判定相同；策略（`Policy`、`InlineDepth`、`SuspectReasons`）在 report 时生效。

## 4. 实现位置

`OobSummaryChecker.cpp`：`loadSolved`、`checkASTDecl` 的 report 分支。

## 5. 测试

* lit `phase-solve.test` 续：solve 之后对 a.c、b.c 各跑 `Phase=report`：a.c 报跨文件的 POSSIBLE（`InlineDepth=0`），b.c 报 DEFINITE 与 `unknown_callee`；
  判定日志写出；没有 `solved/` 的 TU 提示一行。

## 6. 调试

`Dump=decisions`、`WorkDir/decisions/*.jsonl`、`WorkDir/solved/*.json`。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

随驱动（S9-5）一起：bxen 全量模式三阶段跑通 TestCase 目录。

## 9. 待定 / 以后

* S9-5 驱动。
