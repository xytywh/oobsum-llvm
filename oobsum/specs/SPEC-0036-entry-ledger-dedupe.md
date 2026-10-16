# SPEC-0036 入口账本按访问去重：同一入口同一次访问只报一条，日志全记

状态：已实现
来源：SPEC-0035 §3 / §9（成员数组当实参时，入口上同一次访问有成员、外层对象两条 SUSPECT）；SPEC-0001（判定日志记所有判定，不报的也写原因）；
06- A-80（入口账本）、F-34（指纹）。

## 1. 要解决什么

入口函数上推不下去的 Req 各记一条账本（SUSPECT）。同一次访问可能对应几条 Req（成员与外层对象，SPEC-0035；以后的其它拆分也一样），
每条都报出来就是重复告警：复核的人看到的是同一行代码的两条"证明不了"。

## 2. 设计

* 引擎（`core/Engine`）记入口账本时，按 **（入口函数、访问的函数键与位置、读写）** 分组：第一条照常；后面的判定加 `dupOf` = 第一条的指纹。
  原因不看（成员一条常是 `type_range_only`、外层一条是 `entry_unproven`）。
* 策略（`core/Policy` 的 `ClassifyBatch`，Reporter 用它）：先逐条定报不报；带 `dupOf` 的、且它指向的那条**报出了**，才改成不报
  （`not_emitted_because = duplicate_access`）。指向的那条被 `SuspectReasons` / `balanced` 滤掉时，这条按自己的原因照常定。
* 判定日志 / `solved/*.json` 写 `dup_of`；`DecisionFromJson` 读回（solve → report 往返）。
* DEFINITE / POSSIBLE 不动。

## 3. 对召回与误报的影响

同一次访问在同一入口：只要有一条按原规则会报，就至少报一条，所以不丢访问；去掉的只是同一行代码的重复 SUSPECT。日志里每条都在（`dup_of` 指向报出的那条）。

## 4. 实现位置

`core/Decision.h`（`dupOf`）、`core/Engine.cpp`（入口账本分组）、`core/Policy.{h,cpp}`（`ClassifyBatch`）、`core/DecisionLog.cpp`、`report/Reporter.cpp`。

## 5. 测试

* 单测 `Policy.DuplicateAccessIsLoggedNotEmitted`：指向的那条报了 → 不报（`duplicate_access`）；那条被 `SuspectReasons` 滤掉 → 照常报。`Engine`：`dup_of` 往返。
* lit `member-arg.test` 续：入口 `Entry_Msg(MSG *pstMsg, UINT32 n)` 把成员数组传下去 → 两条账本（成员 `type_range_only`、外层 `entry_unproven`），
  报一条、另一条 `dup_of` + `duplicate_access`（`Dump=decisions`）；`SuspectReasons=entry_unproven` 时外层那条照常报。
* `run.py csa`：回退 0；LV10-04 TP / TN 的 `typed_field` 回到 SPEC-0035 之前；LV8-05、LV10-05 的入口原来就有的重复（全局 / 局部结构体成员数组的成员界与外层界，SPEC-0022）各少一条。

## 6. 调试

`Dump=decisions`、`WorkDir/decisions/*.jsonl` 的 `dup_of` / `not_emitted_because`；`tools/decisions.py` 按原因计数会多出 `duplicate_access`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-80 | 入口上推不下去的 Req 各记一条账本 | 同一入口同一次访问（同读写）的账本：第一条报出了，其余只写日志（`dup_of`、`duplicate_access`） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
