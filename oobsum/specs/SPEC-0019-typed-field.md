# SPEC-0019 F-02 的字段访问照生成，入口上记成 `typed_field`；金标准补齐漏写的访问

状态：已实现
来源：用户决定（2026-10-16，STATUS"等用户提供"里 SPEC-0018 §3 的选项 (c)："按你的决定来"）；06- F-02（`p->f` 的条件 `off(f) + sizeof(f) <= ext(p)`）、
D-23（`params`）、D-27（账本原因）、A-80（入口记账）、T-06（金标准）；SPEC-0001（`SuspectReasons` 按原因过滤 SUSPECT）。

## 1. 要解决什么

引擎按 F-02 为经形参指针读写字段生成 Req（LV10-04 `pstMsg->usMsgLen` → `R ext(p0) end 4`、LV5-01 `pstCtx->pucBuf / ulBufLen / ulUsed`），金标准摘要一律没写。
业务代码里几乎每个消息处理入口都读形参结构体的字段：这些 Req 在入口上若都记 `entry_unproven`（SUSPECT），噪声极大；不生成又会丢掉"按大结构体访问小对象"（LV1-05 一类）。

## 2. 设计

### 2.1 照 F-02 生成，入口上单列原因

* 引擎照旧生成这些 Req（不改召回）。在调用点有对象大小时照常判定（`&stCtx` 12 字节 → 安全或越界）。
* **入口记账**（A-80）：一条到入口还没证明的 Req，若同时满足——对象是入口自己的形参所指 `*pK`、`have` 就是 `ext(pK)`、末端上界是常数、且 ≤ 这个形参**声明的所指类型大小**——
  记账原因写 **`typed_field`**（按声明类型的访问；对象由外部按类型给出），否则照旧 `type_range_only` / `entry_unproven`。
* 报不报由 `SuspectReasons` 决定（SPEC-0001 §3）：默认（空）照样报 SUSPECT，复核时可按原因整类关掉。
  入口形参是 `void *`、或访问超出声明类型的，仍是 `entry_unproven`——正是值得看的那一类。

### 2.2 摘要：形参带所指类型大小

`params[k]` 加可选字段 `size`：形参是指向完整类型的指针时，所指类型的字节数（`void *`、不完整类型不写）。引擎（`BuildSummary`）对所有这样的形参都写；
金标准只在用到的入口函数上写（LV10-04 的 `Om_OnSaveCfg_TP/TN`，`OM_MSG_S` 1028 字节）。

### 2.3 金标准补齐（有意修改，T-06）

用引擎对照（`engine_check.py`）逐函数比 depth 0 的 Req，金标准漏写、按 F-01 / F-02 应有的访问补上，上层的上传 Req、调用点 `results`、入口账本按 `replay.py` 的规则重算
（新工具 `oobsum/tools/golden_regen.py`）：

| 用例 | 函数 | 补的访问 | 影响 |
|---|---|---|---|
| LV10-04 | `Om_OnSaveCfg_TP/TN` | `pstMsg->usMsgLen` 的读：`R ext(p0) end 4` | 入口账本 +2（`typed_field`） |
| LV5-01 | `Enc_PutBytes` | `pstCtx` 三个字段的读写：`W ext(p0) end 12`（读并进写） | 上传 +2，在 `Rrc_EncodeMsg_*` 调用点（`&stCtx` 12 字节）判安全 |
| LV3-07 | `Pkt_PutIes(_OK)` | `astIe[i]` 的读（F-01，`i ∈ [0, ulIeNum − 1]`）：`R ext(p2) end [8, 8*p3]`、守卫 `p3 >= 1` | 上传 +4，入口账本 +2（`entry_unproven`） |

告警（`alerts.json`）不变。

### 2.4 `golden_regen.py`

从局部事实（depth 0 的 Req 及其 `absorbed`、调用点实参、局部区间、守卫）按 `replay.py` 的 `classify` / `absorb` 自底向上重算：上传的 Req（已有的按键保留原 id、`doc`、`absorbed`，
新的补 id 与 `start`）、对已定义被调者的调用点 `results`、入口的账本（`opens` 里挂 Req 的那些）；判定（`reps`）只核对不改写（变了就报错停下）。
默认只打印差异，`--write` 才写回。

## 3. 对召回与误报的影响

召回不变（Req 照生成）；入口上的这类 SUSPECT 换了原因，可单独过滤。LV3-07 补的是金标准漏写的真实访问（多一条入口 SUSPECT）。

## 4. 实现位置

`core/Summary.{h,cpp}`（`Param::size`、原因枚举）、`core/Solver.cpp`（入口记账）、`core/Interp.cpp`（`BuildSummary` 写 `size`）、
`cases/summaries/replay.py`（原因枚举、入口记账）、`summary.schema.json`、金标准 JSON、`oobsum/tools/golden_regen.py`。

## 5. 测试

* `replay.py` ① ② 全过（新的入口账本原因在 ② 里核对）；`alerts.json` 逐条相同。
* 单测：`SolverTest` 的统计与入口账本原因随金标准更新；`SummaryTest` 读写 `params[].size`；`apply-golden.txt` 重新生成。
* `engine_check.py`：LV10-04 的条数对上（只差 `unknown_callee`，S8c）。
* lit `summary-dump.c`：`params` 带 `size`。

## 6. 调试

`replay.py <用例> -v`（入口那一行写出账本原因）；`golden_regen.py <用例>`（不加 `--write`）看重算出来的差异。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-27 | 27 类原因 | 加 `typed_field`（入口上按声明类型的常数偏移访问） |
| D-23 | `params[]`：`name`、`type` | 加可选 `size`（所指类型的字节数） |
| A-80 | 入口：`type_range_only` / `entry_unproven` | 先判 `typed_field`（§2.1） |

## 8. 公司侧验证

可选：看业务代码里 `typed_field` 的条数与 `entry_unproven` 的比例，决定 `SuspectReasons` 的默认值（只带回条数）。

## 9. 待定 / 以后

* `typed_field` 是否默认不报，等公司侧的条数。
