# SPEC-0029 `Phase=solve`：读全部 FIR，跨 TU 自底向上解释与求解，判定按 TU 落盘

状态：已实现（S9-3）
来源：`STATUS.md` S9；06- §7 X-03（solve 读 `fir/` 下全部文件、单进程求解、写结果；缺文件 / 坏文件跳过并记日志）、X-04（宿主：桩 TU 跑 `Phase=solve`）、
D-07 / D-08（函数键；头文件里的 `static inline` 每个 TU 一份、取第一份）、D-27（`unknown_callee`）；SPEC-0021（判定）、SPEC-0027（按键找被调者）、SPEC-0028（export）。

## 1. 要解决什么

inline 模式看不到跨文件的调用链。solve 把所有 TU 的 FIR 放在一起：被调者的摘要（含 `ret_new` / `ret_cond` 效果）在别的 TU 里也能用，调用链能跨文件推到底。

## 2. 设计

### 2.1 引擎（`core/Engine`）

`RunProgram(模块列表, 模型表, …, linked)`：

1. 所有模块的函数按函数键去重（同一个键出现在多个模块里——头文件里的 `static inline`——取第一份，D-08）。
2. 全局调用图（直接调用，按 `callee` 键）上 DFS 后序：被调者先。逐个函数解释（被调者的效果可以来自别的模块），组装摘要。
3. `Propagate` 一次推到底，判定同 SPEC-0021。位置的文件名按**函数所在模块**的文件表换回完整路径。
4. 被调者在所有模块里都没有函数体、也不在模型表里：`linked` 时记 `unknown_callee`（链接时才判得了），否则（inline）仍是 `not_in_tu`。

`RunInline(m)` = `RunProgram({m}, …, linked = false)`；`RunSolve(模块列表)` = `RunProgram(…, linked = true)`。结果带 `moduleOf`（函数键 → 模块下标）。

### 2.2 checker：`Phase=solve`

宿主是任意一个桩 TU（X-04）：回调里读 `WorkDir/fir/*.json`（读不了的跳过，stderr 一行），跑 `RunSolve`，把判定按**汇合点函数所在的 TU** 分组，
写 `WorkDir/solved/<tu-id>.json`：`{"schema": "oobsum-solved/1", "tu": …, "file": 主文件, "decisions": [判定…]}`（判定的写法同判定日志，不带"报没报"）。
没有判定的 TU 也写（空列表），report 阶段据此知道它被求解过。桩 TU 本身不报、不处理调试内建函数。`Dump=stats`：`solve tus=… functions=… decisions=…`。

### 2.3 判定读回

`core/DecisionLog` 加 `DecisionFromJson`（S9-4 的 report 用）。

## 3. 对召回与误报的影响

跨 TU 的调用链从"`not_in_tu`（SUSPECT）"变成推到底（DEFINITE / POSSIBLE / SAFE / 入口账本）；真的没有定义的被调者是 `unknown_callee`（SUSPECT）。

## 4. 实现位置

`core/Engine.{h,cpp}`（`RunProgram`、`RunSolve`）、`core/DecisionLog.{h,cpp}`（`DecisionFromJson`）、`OobSummaryChecker.cpp`（`Phase=solve`）。

## 5. 测试

* 单元测试 `EngineTest.SolveAcrossModules`：两个模块（A 调 B 里定义的函数）→ 跨模块的 L1，`moduleOf` 正确；B 里调用一个哪里都没有的函数 → `unknown_callee`。
* 单元测试 `DecisionLogTest`：判定写成 JSON 再读回，字段相同。
* lit `phase-solve.test`：export 两个 TU（`Inputs/solve/`，各有一个同名 `static` 函数）→ 桩 TU solve → `solved/` 两个文件、A 的判定带跨文件的调用链。

## 6. 调试

`WorkDir/solved/*.json`；`Dump=stats`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| X-03 | 写 `summaries/`、`decisions/`、`reports.json`、`ledger.jsonl`、`stats.json` | 本步写 `solved/<tu>.json`（判定按 TU 分组）；`decisions/`（判定日志）在 report 阶段由各 TU 写，其余文件以后 |

## 8. 公司侧验证

先在本地用驱动（S9-5）把 export → solve → report 串通；公司侧等驱动好了一起验。

## 9. 待定 / 以后

* S9-4 `Phase=report`；S9-5 驱动；solve 的内存（X-07）。
