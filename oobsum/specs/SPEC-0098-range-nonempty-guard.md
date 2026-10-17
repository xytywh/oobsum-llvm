# SPEC-0098 条件边收窄出的区间非空：`lo ≤ hi` 记成守卫（`while (n--)` 传 0 时访问走不到）

状态：已实现
来源：找"级别判过头"的探针第二批（多报 POSSIBLE）：

```c
static void Wn(UINT8 *p, UINT32 n) { while (n--) { p[n + 10] = 0; } }   /* 体内 n ∈ [0, p1 − 1]，Req 没有守卫 */
void M3(void) { UINT8 a[4]; Wn(a, 0); }                                  /* POSSIBLE——n = 0 时循环不执行 */
```

循环头 widening 之后 `n ∈ [0, p1]`，条件边 `n != 0` 收成 `[1, p1]`（`n − 1` 之后 `[0, p1 − 1]`）。`!=` 的收窄不推守卫原子（`<` 会推 `lo < x`），
可"体内的区间非空"本身就意味着 `p1 ≥ 1`：调用点代入 `p1 = 0` 时这条路走不到。

## 2. 设计

* `AssumeCmp` 里区间对确切值收窄之后（`Narrow` / `NarrowPtr`），取收窄后的值 `[lo, hi]`：两头都有、差 `lo − hi` 不是常数、在已有守卫与类型范围下也不是已经成立的
  → 记守卫 `lo − hi ≤ 0`。可靠：这条边上值一定在区间里，`lo ≤ hi` 恒成立。
* 在本条边上由比较推出的原子之后再加（否则"已经成立"判断不出来，会多出 `4·div(p1 − 1, 4) >= 0` 这类多余的守卫）。
* 整数约简：各项系数除以公约数 g，常数项向上取整（`Σ a·x + c ≤ 0 ⇔ Σ (a/g)·x + ⌈c/g⌉ ≤ 0`），`4·p1 >= 4`、`4·p1 >= 1` 都写成 `p1 >= 1`。
* 守卫照旧先进先出、至多 8 个（A-14）。

## 3. 对召回与误报的影响

**召回**：可靠（只多一条在这条边上恒成立的守卫；调用点上它为假说明这条路走不到）。**误报**：`while (n--)`、`for (i = n; i != 0; i--)` 这类循环传 0 的调用方不再报。

## 4. 实现位置

`core/Interp.cpp`（`AssumeCmp`）。

## 5. 测试

* lit `range-nonempty.test`：`Wn(a, 0)` → SAFE `guard_false`、`Wn(a, 1)` → POSSIBLE；`Dump=summary` 里 Req 的守卫 `p1 >= 1`。
* 单测 `Interp.RangeNonEmptyGuard`；循环相关的单测（`PointerCompareNarrows`、`NeLoopThreshold`、`NeThresholdFromBody`、`DependentNarrowing`、
  `CongruencePointerLoop`、`WidePointerCompareIsCoarse`）的 Req / 状态多一条 `p1 >= 1`（有意更新）。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变（逐访问对比，没有报出消失）。

## 6. 调试

`Dump=state`（守卫）、`Dump=summary`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-14 守卫的来源 | `assume` 边上的比较（代入确切值后仍含接口符号） | 另：收窄出的区间非空 `lo ≤ hi` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
