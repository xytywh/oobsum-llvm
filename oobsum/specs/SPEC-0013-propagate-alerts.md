# SPEC-0013 从局部事实推到底（`core/Solver` 的 `Propagate`）与告警成品（`core/Alert`），与 `alerts.json` 逐条相同

状态：已实现（S8b）
来源：`STATUS.md` S8b；SPEC-0012 §9；06- A-44（覆盖合并）、A-80（自底向上求解）、F-30…F-35（告警位置、BugType、消息模板、调用链 note、指纹、
排序）、A-92（告警成品只由机器字段生成，C++ 与 `replay.py --alerts` 逐条相同）、T-06（金标准后一半）；参考实现 `replay.py` 的 `derive`、
`absorb`、`req_key`、`make_alert`、`alert_msg`、`obj_name`、`BOUND_PHRASE`。

## 1. 要解决什么

S8a 证明了"给定被调者的摘要，调用点怎么判"。引擎实际要做的是反过来：只有每个函数**自己**算出来的事实（本函数访问点生成的 depth 0 Req、
调用点处的实参 / 局部区间 / 守卫），自底向上把上传的 Req、判定与入口账本全部推出来，再给判定生成告警。`replay.py` 的 `derive` 就是这件事，
`alerts.json` 是它的产物（13 条）。C++ 要逐条推出同样的结果。

## 2. 设计

### 2.1 `Propagate`（`core/Solver`，同 `derive`）

输入：一组函数的摘要（只读其中的局部事实：`reqs` 里 `depth = 0` 的、其 `absorbed`，`calls` 的实参 / `locals` / `guards`、`params` / `fields` / `key`）；
上传的 Req（depth ≥ 1）、`reps`、`opens` 一概不读。

* 顺序：`replay.py` 的 `topo`（离叶子的层数，同层按名字）——同 TU 的 SCC 逆拓扑序的简化版，A-80 的完整调度在 S8c。
* 每个函数：候选 Req = 本函数 depth 0 的 Req 及其 `absorbed`（每个访问各一条）+ 每个调用点上被调者已推出的 Req 经 `Classify`（SPEC-0012）
  结局为 `up` 的；结局为 L1 / L2 的生成判定记录（`RepOf`）。候选经 **覆盖合并**（A-44，同 `absorb`：同一 `have`、对象、守卫，`end` 上界恒不大于
  另一条的并掉；相等时留前面的）得到本函数推出的 Req。
* 没有调用方的函数（入口）：每条推出的 Req 记账——`have` 是常数且 `end − have` 的上界只来自类型范围 → `type_range_only`，否则 `entry_unproven`。
* 输出：每个函数推出的 Req、判定记录、入口账本；统计（上传、判定、入口账本的条数）。

### 2.2 告警成品（`core/Alert`，同 `make_alert` / `alert_msg` / `obj_name`）

只用判定记录的机器字段（A-92）：

* 位置 `loc` = 判定的 `site`；`fn` = 汇合点函数；`level`；`fp`（F-34）。
* BugType：`Out-of-bound access via call chain (definite|possible)`，category `Memory error`。
* 消息（F-32）：`Definite out-of-bound <write|read> through a <depth>-level call chain: up to byte <end 上界> of <对象> (<have> bytes[; via <字段>]) is accessed`；
  L2 是 `Possible … may be accessed; the bound comes from <界的说法>`（`loop` → `a loop condition` 等，同 `BOUND_PHRASE`）；链跨了多个文件时末尾加
  ` [f (site) -> …]`（F-33 ③）。对象：`g:` / `local:` → `'名'`，`heap@位置` → `heap object allocated at 位置`，其余把 `pN` 换成形参名后加引号。
* note（F-33）：第一条 `Call chain: f (site) -> …`；之后链上与汇合点同一文件的每一跳一条：`site: call to <下一跳>`，最后一跳 `site: the <write|read> happens here`。
* 只有 `reported`（`max(depth, eff_depth) > InlineDepth`）的判定生成告警。

本 SPEC 的告警是**数据**（与 `alerts.json` 同形）；SPEC-0001 的标签前缀、四个 BugType、判定日志由 `report/Reporter` 在 S8c 接上时套上去
（L1 → DEFINITE、L2 → POSSIBLE）。

## 3. 对召回与误报的影响

与参考实现一致；不接进 checker。

## 4. 实现位置

`OobSummary/core/Solver.{h,cpp}`（`Propagate`、`Absorb`）、`OobSummary/core/Alert.{h,cpp}`（`AlertMessage`、`MakeAlert`、`AlertToJson`）；不依赖 Clang。

## 5. 测试

单元测试 `PropagateTest.cpp`：16 个用例推出的 Req（按 `req_key` 比较）与 JSON 一致、判定记录与 JSON `reps[]` 逐字段一致、入口账本原因在 JSON `opens[]` 里；
全部告警与 `cases/summaries/alerts.json` 逐条相同（JSON 相等，含 `case`）；统计：上传 58、判定 13、入口账本 8（与 `replay.py` 打印的相同）。

## 6. 调试

失败时打印用例、函数、推出的与 JSON 里的 Req 键 / 判定字段；对照 `python3 replay.py <用例> -v` 的"② 从局部事实推到底"。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* S8c：调度（同 TU 调用图、SCC、递归按未知处理并记账 `recursion`，A-80…A-82），摘要来自 S7 的解释器，判定接进 `Reporter` 与判定日志。
