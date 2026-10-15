# SPEC-0015 局部事实的调用点记录与摘要组装、`Dump=summary` / `Dump=state`、引擎对照脚本（端到端 T-06）

状态：已实现（S7a-2）
来源：`STATUS.md` S7a-2；06- D-25（调用点记录）、D-23（顶层字段）、D-28（状态）、T-06（金标准）；SPEC-0001 §6.1（`summary`、`state` 两个 Dump 种类）；
SPEC-0013（`Propagate` 只读局部事实）；SPEC-0014（depth 0 的 Req）。

## 1. 要解决什么

S7a-1 产出了 depth 0 的 Req；跨函数推导还要每个调用点的**实参是什么**（D-25）。把它们组装成摘要，打出来（`Dump=summary`），
再用 `replay.py` 的 `derive` 从这些**真实引擎算出的**局部事实推到底，与金标准 `alerts.json` 比——这是 T-06 在真实引擎输出上的端到端检查。

## 2. 设计

### 2.1 调用点记录（`core/Interp` 的 `RecordCall`）

* `site` = `文件名:行`；`callee` = 被调函数名（键的名字部分）；间接调用本步不记（`fptr`，S9）。
* `model`：被调者在本 TU 没有定义、且在纯函数清单里（目前只有 `LOG_Print`，`cases/common.h` 写明日志不接收任何流进内存访问的东西；
  WP5 的模型表接上后由配置给出，I8 / I9）→ `pure`。
* 实参：
  * 整数、确切值 → 表达式（接口符号的规范形式，如 `p0`、`4`）；区间 → 一个局部名（实参就是某个局部变量时用它的名字，否则 `_arg<k>`），区间写进 `locals`
    （`{range, bound}`）；未知 → `_unknown<k>`（不是接口符号，调用点上只会走向 `entry_unproven`，即 SUSPECT；**不写 null**——`replay.py`
    把 null 当成"与越界无关"，被调者的 `pk` 会被误当成调用方的形参）。
  * 指针 → `{obj, have, alias?}`：`have` = 基址大小 − 偏移（偏移是区间时减上界，取最小的剩余，多报不少报）；实参就是调用方的第 i 个形参且偏移为 0 时
    `alias = pi`；基址未知 → `{obj: local:_unknown<k>, have: _unknown<k>}`（同上，只会走向 SUSPECT）。
  * 纯函数模型的指针实参 → null（与金标准一致）。
* `results` 留空（由求解填）。效果（`ret_new` 等）在 S7d：本步调用的返回值未知。

### 2.2 摘要组装（`BuildSummary`）

`fn`、`key`（引擎的键，SPEC-0003）、`loc`、`params`（名字 + FIR 的类型写法 `u32` / `u8*` / `rec#0*`）、`fields`（用到的 `pi->字段`：偏移、大小、类型）、
`status`、`reqs`（depth 0）、`calls`、`opens`；`effs`、`reps` 为空。`core/Range` 的 `TypeRangeOf` 认 FIR 的整数写法（`u8` … `i32`）。

### 2.3 `Dump=summary` / `Dump=state`（checker 接线）

* `Dump=summary`：每个函数一行 `[oobsum:summary] <摘要 JSON>`，`WorkDir/dump/<tu>/summary/<函数>.json`（缩进 1）。
* `Dump=state`：每个块入口的状态一行 `[oobsum:state] fn <函数> b<号>: <局部>=<值> … [<格>]=<值> …`（只读，不影响结果）。
* 只要 `fir` / `summary` / `state` 之一打开就降级一次、三者共用（默认运行仍不降级）。`DumpFilter` 按函数名过滤。

### 2.4 引擎对照脚本（`oobsum/tools/engine_check.py`）

对给定用例：用本仓库的 clang 跑用例的全部源文件（跨 TU 用例跑整个目录），收集 `Dump=summary` 的摘要；**规范化**写法差异——键 `F@<文件 id>:名` →
`F@<定义文件名>:名`（金标准用文件名；文件 id 依赖本机绝对路径，不进金标准）、类型 `u32` → `UINT32` 等；交给 `replay.py` 的 `derive` 推到底；
最终告警与 `alerts.json` 里这个用例的条目**逐条相同**（消息、调用链 note、指纹）才算一致；同时打印对 `@expect` 的结论。

## 3. 对召回与误报的影响

不接进 checker（S8c）。召回相关：未知的实参写成非接口符号（只会走向 SUSPECT），不写 null。

## 4. 实现位置

`OobSummary/core/Interp.{h,cpp}`（`RecordCall`、`ShowState`、`BuildSummary`、`IsPureModel`）、`core/Range.cpp`（`TypeRangeOf`）、
`OobSummaryChecker.cpp`（`dumpSummaries`）、`oobsum/tools/engine_check.py`。

## 5. 测试

* 单元测试 `InterpTest`：调用点记录（确切 / 区间 / 未知的整数实参、指针实参的 `have` 与 `alias`、纯函数模型的 null、未知指针）。
* lit `summary-dump.c`：`Dump=summary` 的字段、`Dump=state` 的格式、落盘。
* lit `engine-check.test`：`engine_check.py LV1-01 LV1-02 LV1-07`——三个用例从 C 源码端到端与 `alerts.json` 一致（含跨 TU 的 LV1-07）。
  其余用例按 STATUS 逐步加入（LV2-09 需要 S7b 的守卫，否则 TN 误报 L1）。

## 6. 调试

`Dump=summary`（看局部事实）、`Dump=state`（看每个块入口的值）、`engine_check.py -v <用例>`（多出 / 缺少的告警、推到底的说明）、
`python3 replay.py <用例> -v`（金标准的推导过程）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-25 `args` | 与越界无关的写 `null` | 只有纯函数模型的指针实参写 null；值未知的实参写成非接口符号 `_unknown<k>`（召回优先） |
| D-23 `params[].type` | 类型 | 引擎产出的写 FIR 的类型写法（`u32`、`u8*`）；金标准是 C 类型名，对照时规范化 |

## 8. 公司侧验证

可选：bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=summary`，看业务代码函数的局部事实是否合理（不带出公司）。

## 9. 待定 / 以后

* S7b 守卫（`guard`、调用点 `guards`）；S7c 循环（调用点 `locals` 的循环界）；S7d 效果（`ret_new` 等）。
* 函数键的规范化以后是否改成金标准与引擎同一写法（例如金标准也存文件名、引擎另记文件 id），在 S8c 定。
