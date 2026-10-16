# SPEC-0035 成员数组当实参：调用点记录带外层对象，代换时外层也查

状态：已实现
来源：`STATUS.md` 已知问题"成员指针当实参会丢外层对象"；SPEC-0022（函数内直接访问成员数组时外层对象也查，LV7-03）；06- F-02、D-20、D-25（调用点记录）、D-31；
SPEC-0001（只有证明了安全才是 SAFE）。

## 1. 要解决什么

```c
static void Body_Clear(UINT8 *p, UINT32 n) { (void)memset_s(p, n, 0, n); }
static void Msg_Clear(MSG *pstMsg)         { Body_Clear(pstMsg->aucBody, 512); }     /* aucBody[1024]，偏移 4 */
void Top_TP(void) { UINT8 auc[64]; Msg_Clear((MSG *)auc); }                          /* 实际写到第 516 字节 */
```

`Msg_Clear` 的调用点记录只有 `{obj: p0->aucBody, have: 1024}`：`Body_Clear` 的 Req `n ≤ ext(p0)` 代进来是 `512 ≤ 1024` → **SAFE**，
`Top_TP` 的越界漏掉（错误的 SAFE）。同样的访问写在 `Msg_Clear` 里（SPEC-0022）是能报的。

## 2. 设计

### 2.1 调用点记录（D-25）

指针实参是形参所指结构体里的成员数组（`hasParent`）时，多记外层对象：

```json
{"obj": "p0->aucBody", "have": "1024", "parent": {"obj": "*p0", "have": "ext(p0) - 4"}}
```

`parent.have` = 外层对象大小 −（成员偏移 + 实参在成员里的偏移上界），与 `have` 的算法相同（剩余字节数，取最小的剩余）。

### 2.2 代换（A-60 的调用点判定）

被调者的 Req 的对象是 `*pK`、第 K 个实参带 `parent` 时，先照旧按成员判（结局 M），再把第 K 个实参换成外层对象判一次（结局 O）：

| M | 结果 |
|---|---|
| L1 / L2 / open（已经报或记账） | 只有 M（不重复报） |
| safe | O 不是 safe → 只有 O（外层对象上越界、要上传或记账）；O 也是 safe → 只有 M |
| up | M，再加上 O（O 不是 safe 时）：两条都上传（各自约束不同的对象，缺一条就会在上层漏掉） |

外层对象那一条在 `results` 里标 `"parent": true`（JSON），C++ / Python 两边同一个入口：`apply::ClassifyAll` / `replay.classify_all`。

### 2.3 不做

* 被调者 Req 的对象是 `pK->f`（被调者形参所指结构体里的成员）而第 K 个实参又是调用方的成员数组：嵌套成员，少见，以后。
* 外层对象的外层（成员数组的成员数组）：只记一层。

## 3. 对召回与误报的影响

**召回**：修掉 §1 的错误 SAFE。**误报 / 噪声**：入口函数里成员数组当实参（LV10-04 `Om_SaveCfg(pstMsg->aucBody, …)`）会多一条外层对象的入口账本
（同一次访问两条 SUSPECT：成员一条、外层一条）。以后可在入口按访问合并账本。

## 4. 实现位置

`core/Summary.{h,cpp}`（`PtrArg.parent`、`Result.parent` 的读写）、`core/Interp.cpp`（调用点记录）、`core/Apply.{h,cpp}`（`ParentCall`、`ClassifyAll`、核对 `results`）、
`core/Solver.cpp`（推导）、`oobsum/cases/summaries/replay.py`（`parent_call`、`classify_all`，① ② 两处）、`summary.schema.json`、`oobsum/tools/golden_regen.py`。

## 5. 测试

* lit `member-arg.test`：§1 的例子 → `Top_TP` 报 DEFINITE（外层对象 `auc` 64 字节）、`Top_OK`（真 `MSG`）SAFE；调用点记录带 `parent`（`Dump=summary`）；
  成员本身越界的（`Body_Clear(pstMsg->aucBody, 2048)`）仍按成员报、不重复。
* 金标准：LV10-04 用 `golden_regen.py --write` 重算（两个入口各多一条外层对象的上传 Req 与入口账本）；`replay.py` 0 处不一致；
  C++ / Python 两边的核对（单测 `ApplyTest` 的 golden、`gen_apply_golden.py` 重生成）一致；`engine_check` 不回退。
* 单测 `SummaryTest`：`parent` 的读写往返。

## 6. 调试

`Dump=summary`（实参的 `parent`）、`Dump=apply`（外层对象那一条的结局）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-25 | 指针实参 `{obj, have, alias?, eff_depth?, fields?}` | 加 `parent: {obj, have}`（成员数组实参的外层对象）；`results[]` 的 Req 结局加 `parent: true`（外层对象那一条） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 入口按访问合并账本（同一次访问的成员 / 外层两条 SUSPECT 合成一条）。
