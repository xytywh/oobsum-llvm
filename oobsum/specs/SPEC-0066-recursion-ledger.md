# SPEC-0066 递归调用点记账 `recursion`：经递归传上来的条件不再悄悄丢掉（错误的 SAFE）

状态：已实现
来源：探针（递归）：

```c
static void A(UINT8 *p, UINT32 n) { B(p, n + 1); }
static void B(UINT8 *p, UINT32 n) { if (n < 20) A(p, n); p[n] = 0; }                 /* 下标一路涨到 20 */
void Mut_TP(void) { UINT8 a[16]; A(a, 0); }                                           /* 原来：SAFE（up to byte 2）——错 */
static void Walk(UINT8 *p, UINT32 n, UINT32 d) { if (d == 0) { p[n] = 0; return; } Walk(p, n + 4, d - 1); }
void Walk_TP(void) { UINT8 a[16]; Walk(a, 0, 8); }                                    /* 原来：SAFE（up to byte 1）——错，实际写第 32 字节 */
```

自底向上算摘要时，同一个强连通分量（递归）里先算的函数看不到后算的被调者的摘要；`Propagate` 不在环上求不动点，经递归调用传上来的 Req 丢了，
剩下的部分判成 SAFE。06- D-27 有账本原因 `recursion`，但从没产出过。

## 2. 设计

所有函数的摘要算完之后、推到底之前（`Engine.cpp` 的 `RunProgram`）：按摘要的调用点记录（被调者的键，`CalleeOf`）建调用图，求强连通分量（Tarjan）；
**分量内**的每条调用边（含自己调自己）在调用方的账本里记一条 `recursion`（位置是调用点，同一调用点一条）。这条账本在调用方的函数里报 SUSPECT，
文本"the call is part of a recursion; accesses deeper in the recursion are not checked"。

不在环上求不动点（递归在业务代码里少，编码规范多半禁止）：环外的调用照旧判；环内已经能判的照旧判（`Rec(a, 16)` 的第一层仍是 DEFINITE），
只是额外记一条账本，保证"深处的访问没检查"不会被一条 SAFE 盖住。

## 3. 对召回与误报的影响

**召回**：递归函数里经递归才到达的访问至少有一条 SUSPECT（原来可能整条判 SAFE）。噪声：每个递归调用点一条 SUSPECT（安全的递归也有），可用 `SuspectReasons` 过滤。

## 4. 实现位置

`core/Engine.cpp`（`RecursionLedger`、`LedgerMessage`）。

## 5. 测试

* lit `recursion.test`：自递归、互相递归、带深度参数的递归 → 每个递归调用点 `SUSPECT:recursion`；`Rec_TP` 仍 DEFINITE；非递归的调用不记。
* 单测 `Engine.RecursionLedger`：在真实降级的 FIR 上加一条自己调自己的调用 → 它的账本与判定里有 `recursion`，别的函数没有。
* `run.py csa` 不回退（"其它报出"多 2 条：用例里递归函数的 `recursion` 账本）；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`opens` 里的 `recursion`）、`Dump=callgraph`（SCC）。

## 7. 对主规格的修订

无（按 D-27 实现）。

## 8. 公司侧验证

可选：业务子目录里 `recursion` 有多少（决定要不要做环上的不动点）。

## 9. 待定 / 以后

* 环上的不动点（带 widening）：递归深度由形参控制的写法能判出级别。
