# SPEC-0079 只在某个返回值下成立的写效应（`ret == OK ⇒ pst->ulLen := …`）

状态：已实现
来源：STATUS S8c-2（LV5-02，原计划 2027H1）。初始化函数的典型写法：

```c
static UINT32 Enc_Init(ENC_CTX_S *pst, UINT32 ulLen) {
    pst->pucBuf = Pub_MemAlloc(ulLen); if (pst->pucBuf == NULL) return VOS_ERR;   /* 出错路径没写 ulBufLen */
    pst->ulBufLen = ulLen + 4; return VOS_OK;
}
void Rrc_EncodeSib_TP(void) { ENC_CTX_S st; if (Enc_Init(&st, 128) != VOS_OK) return; Enc_Encode(&st, …); }
```

SPEC-0073 推出 `ulBufLen := p1 + 4, may`：出错路径没碰，调用方取"旧值与新值的并"，可 `st` 没初始化，旧值不知道，并出来也不知道 → 只能记账。
实际上调用方只在 `ret == VOS_OK` 的分支上用它，而那条分支上 `ulBufLen` 一定是 `132`。

## 2. 设计

### 2.1 推导（被调者）

记录那一遍里每条返回路径同时记下返回值是不是常数（`RecordReturn`）。某个写效应候选（SPEC-0073 / 0077）在无条件规则下没有确切值（有路径没碰 / 不知道 / 值不同）时，
按返回常数分组：返回 `c` 的所有路径都写了同一个确切值 `V` → `{kind: field / global, target, value: V, when: "ret == c"}`。有一条返回不是常数就不分组。

### 2.2 调用点

* 被调者有 `ret_cond`（返回值都是常数，SPEC-0025）时，调用结果带"情形"（`IntCase`）。对每条 `when: "ret == c"` 的写效应，按调用前的状态算出新值（`CallerExpr`），
  挂到情形 `c` 上（`IntCase::writes`：格、值、**调用刚结束时这一格的值**）。无条件的部分照旧（`mods` 失效、`may` 汇合）。
* 条件边按返回值筛情形（`FilterCases`）之后：剩下的情形都写了同一格同一个值、而这一格的当前值与调用刚结束时相同（中间没被改过）→ 写成这个值。
* 情形汇合（`MergeCases`）时写入不同的丢掉（变弱，仍可靠）。

### 2.3 读写

`Eff.when` 本来就有（`ret_cond` 用），schema 不用改。

## 3. 对召回与误报的影响

**召回**：初始化函数在成功分支上写的长度 / 个数，调用方在成功分支上按真实值判（LV5-02：原来记账，现在 TP L1、TN SAFE）。
可靠：只在返回 `c` 的每条路径都写了同一个确切值时记；调用方只在筛出 `ret == c`、且格没被改过时用。

## 4. 实现位置

`core/Interp.h`（`IntCase::writes`）、`core/Interp.cpp`（`RecordReturn`、`Effects`、调用点、`FilterCases`、`MergeCases`）。

## 5. 测试

* lit `cond-write.test`：`Init` 成功才写长度 → `field p0->ulLen := 64, when ret == 0`；调用方 `if (Init(&st) != 0) return;` 之后按 64 判 POSSIBLE；
  对照：调用后先改了这个字段再判断 → 不写回；不判断返回值直接用 → 照旧记账。
* 单测 `Interp.ConditionalWriteEffect`。
* `field-effect.test`、`global-effect.test`、`out-new-field.test` 的摘要多出带 `when` 的那一条，按新输出更新。
* `run.py csa`：LV5-02（TP 期望记账、later=L1）从记账变成 DEFINITE（超出预期），TN 从"通过+S"变成 SAFE；不回退，基线有意更新。

## 6. 调试

`Dump=summary`（`effs` 的 `when`）、`Dump=state`（筛选后的格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 `field` / `global`（SPEC-0073 / 0077） | 无条件 | 可带 `when: "ret == c"`（只在返回 `c` 时成立），调用方在筛出这个返回值的分支上应用 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 返回值不是常数（指针）的条件（`ret != NULL ⇒ …`）。
