# SPEC-0048 `run.py csa`：TP 入口的 L1 / L2 可以判在它调用链上的汇合点

状态：已实现
来源：`run.py csa` 的"级别不同"里，LV1-04（年内承诺，期望 L2）：POSSIBLE 已经报了，报在 `Cell_InitNbrList`——用例头写明"汇合点不在入口，而在第一个知道
`astNbr` 有多大的函数"（06- F-30：位置是汇合点那次调用）。量尺只看归在入口名下的判定（入口上是一条 `typed_field` 的 SUSPECT），于是算成"级别不同"。
SPEC-0047（记账口径看调用链）的同一做法用到 L1 / L2。只改测量，不改 checker。

## 2. 设计

* **TP** 入口：归在入口名下没有报出的 DEFINITE / POSSIBLE 时，看从入口经静态调用能到达的函数（SPEC-0047 的调用图）里报出的 DEFINITE / POSSIBLE，
  取最强的当作这个入口的实际（显示 `DEFINITE@<函数名>` 等），再按原口径对照（L1 ↔ DEFINITE、L2 ↔ POSSIBLE）。
* **TN** 入口不放宽：共用被调者里判出的 DEFINITE / POSSIBLE 不记到 TN 头上（它们照样出现在"其它报出"里）。
* 回退检查不变。

## 3. 对召回与误报的影响

无（测量）。"级别不同"只剩入口调用链上真的只有 SUSPECT 的。

## 4. 实现位置

`oobsum/cases/run.py`（入口的实际）。

## 5. 测试

* `run.py csa`：LV1-04 TP 从"级别不同"变成"通过"；基线有意更新。lit `cases-csa.test` 不变（没有摘要）。

## 6. 调试

`csa-result.json` 的 `actual`（`…@函数名`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0002 §2.2 | 入口的实际：归在入口函数名下的判定（SPEC-0047：记账的 TP 看调用链） | TP 入口名下没有 DEFINITE / POSSIBLE 时，取它调用链上报出的最强 DEFINITE / POSSIBLE |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
