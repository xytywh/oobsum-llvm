# SPEC-0021 引擎接进 checker（`Phase=inline`）：单个 TU 内从摘要推到判定、带标签报出、全部写进判定日志

状态：已实现（S8c-1）
来源：`STATUS.md` S8c；06- §7 `Phase=inline`（"单个 TU 内一次做完、跨 TU 的被调者视为未知"）、A-80…A-82（求解）、F-10…F-14（判定）、F-30…F-35（告警）、
D-27 `not_in_tu`；SPEC-0001（标签、策略、判定日志）、SPEC-0013（`Propagate`）、SPEC-0014…0020（函数内解释、模型）。

## 1. 要解决什么

到 S7d 为止，引擎的每一段都只在 `Dump` 和对照脚本里跑；checker 自己报的仍只有调试内建函数的判定。本步把它们串起来：`Phase=inline`（默认）时，
每个 TU 降级 → 函数内解释 → 摘要 → `Propagate` → 判定 → Reporter 报出 + 判定日志。

## 2. 设计

### 2.1 流程（`core/Engine`，不依赖 Clang）

`engine::RunInline(module, models)`：

1. 每个有函数体的函数：`AnalyzeFunction` → `BuildSummary`；按函数名放进摘要表（同一 TU 里不会重名）。
2. `solver::Propagate`（自底向上，SCC 先按 `TopoOrder` 的处理）。
3. 判定（§2.2），每条都带标签与原因。

### 2.2 判定的来源

| 来源 | 标签 | 原因（`reason`） | 位置 |
|---|---|---|---|
| 调用点判出 L1 / L2（`reps`） | DEFINITE / POSSIBLE | — | 汇合点调用（`rep.site`） |
| 调用点判安全 | SAFE | `guard_false`（守卫在此为假）/ `within`（最大末端 ≤ 大小）/ `guard_entailed` / `guard_bounds`（F-12b） | 调用点 |
| 调用点记账（`open`） | SUSPECT | `entry_unproven` / `type_range_only` / `pointer_stepping` | 调用点 |
| 入口函数上推不下去的 Req | SUSPECT | `typed_field` / `type_range_only` / `entry_unproven`（A-80、SPEC-0019） | depth 0：访问点；否则入口函数 |
| 本函数当场证明安全的访问（`FunctionFacts::safe`） | SAFE | `local_entailed` | 访问点 |
| 本函数的账本（`opens`：`untracked_pointer`、`alloc_size_unknown`、`type_range_only`、`pointer_stepping`） | SUSPECT | 同账本 | 访问点 |
| 函数没分析完（`status` 不是 `analyzed`） | SUSPECT | `unanalyzed:<原因>` | 函数声明处，每个函数一条 |
| 指针（含函数指针）流进本 TU 没有函数体、也不在模型表里的被调者 | SUSPECT | `not_in_tu`（D-27；inline 模式下判不了它是跨 TU 还是真的未知） | 调用点，每个调用点一条 |

`Classify` 的四种判安全各给一个机器原因（`Outcome::reason`），`Propagate` 把调用点的 `safe` / `open` 结局也交出来（`FunctionResult::sites`）。

### 2.3 文本、指纹、调用链

* DEFINITE / POSSIBLE：F-32 原样（`alert::AlertMessage`），指纹用 `rep.fp`（与 `alerts.json` 相同）。
* SUSPECT（有 Req 的）：`Cannot prove the {read|write}[ through a {N}-level call chain] stays within {object} ({大小}): up to byte {needHi} may be accessed`；
  `{大小}` 是常数时写 `N bytes`，是符号时写 `size ext(p0)`。
* SUSPECT（只有账本原因的）：`Cannot prove this access is within bounds: {短语}`——`untracked_pointer` → `the pointer's target is not tracked`、
  `alloc_size_unknown` → `the size of the object is unknown`、`type_range_only` → `the offset is bounded only by its type`、`pointer_stepping` → `the pointer is advanced in a loop`、
  `not_in_tu` → `a pointer is passed to '{callee}', whose body is not in this translation unit`、`unanalyzed:*` → `the function was not fully analyzed ({原因})`。
* SAFE：`Proven within bounds: up to byte {needHi} of {object} ({大小})`。
* 调试内建函数 `oobsum_debug_*` 按纯函数（模型表加正则 `^oobsum_debug_`），不产生 `not_in_tu`。
* 指纹：L1 / L2 用 `rep.fp`；其余 `DecisionFingerprint(标签名, 函数键, 访问键, kind, 访问末端, 对象)`；只有账本原因的，访问键写位置、对象写原因。
* 调用链：`rep.chain` / Req 的 `path`（`函数@文件名:行`）→ `Hop`；位置里的文件名按本 TU 的文件表换回完整路径（摘要只存文件名，D-23；同名取第一个）。列号没有，Reporter 用 1。

### 2.4 checker 接线

`Phase=inline` 时跑（其余 Phase 以后，§9）；调试内建函数的判定照旧并进来。新 Dump 种类 **`apply`**：每个调用点上被调者每条 Req 的结局一行
`[oobsum:apply] <函数> @ <调用点> <被调者>.<访问位置> → <结局>[ (<原因>)]`，按 `DumpFilter` 过滤。降级、解释只做一次（与 `Dump=fir/summary/state` 共用）。

## 3. 对召回与误报的影响

引擎第一次真正报告警。召回优先：证明不了的一律 SUSPECT（按原因可过滤，SPEC-0001 §3）；SAFE 不报但进日志。
`not_in_tu` 在业务代码里会很多（inline 模式的固有局限），`SuspectReasons` 可关；跨 TU 模式（`export` / `solve`）里由链接时判成 `unknown_callee` 或用对方的摘要。

## 4. 实现位置

新增 `OobSummary/core/Engine.{h,cpp}`；`core/Apply.cpp`（判安全的机器原因）、`core/Solver.{h,cpp}`（`sites`）、`OobSummaryChecker.cpp`（接线、`Dump=apply`）、
`Checkers.td`（`Dump` 的说明加 `apply`）。

## 5. 测试

* 单元测试 `EngineTest`：手工 FIR 的三层链（TP L1、TN SAFE）、入口 SUSPECT、`not_in_tu`、`unanalyzed`、消息与原因。
* lit `engine.c`（`-verify`）：DEFINITE / POSSIBLE / SUSPECT 的文本前缀、SAFE 不报；`Dump=apply` 的行。
* `run.py csa`：67 个用例的变化列在提交信息里；基线有意更新（`--save`），之后以新基线守召回。
* 原有 lit 用例里因引擎多出的告警按实际更新（只加 `expected-warning`，不删已有期望）。

## 6. 调试

`Dump=apply`（调用点结局）、`Dump=decisions`（每条判定的 JSON）、`Dump=summary` / `state`（局部事实）、`WorkDir/decisions/*.jsonl`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-32 | L1 / L2 的模板 | 加 SUSPECT / SAFE 的模板（§2.3） |
| D-27 | `not_in_tu`：inline 模式下的跨 TU 被调者 | 同；判定在调用点，每个调用点一条（只在有指针流进去时） |

## 8. 公司侧验证

bxen 跑 TestCase 与少量业务目录：各标签与原因的条数、总耗时（只带回数字）。

## 9. 待定 / 以后

* `export` / `solve` / `report`（跨 TU）；`unknown_callee`（链接时判）；告警的 note 按 F-33 的三种写法（现在每跳一条 `call chain: <函数>`）。
* `ret_new` 等效果（S7d-3）要求被调者的摘要先算出来：本步的 `RunInline` 先把每个函数各自解释一遍，以后改成按 `TopoOrder` 自底向上解释、把被调者的效果代进来。
