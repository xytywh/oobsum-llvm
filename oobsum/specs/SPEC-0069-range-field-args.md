# SPEC-0069 实参所指结构体的字段值是区间时也带进调用点（经调用点的 `locals`）

状态：已实现
来源：汇合探针：

```c
static void Fill(UINT8 *p, const CTX_S *c) { for (i = 0; i < c->ulLen; i++) p[i] = 0; }      /* Req：c->ulLen <= ext(p) */
void Br_TP(UINT32 n) { UINT8 a[16]; CTX_S s; s.ulLen = 5; if (n) s.ulLen = 100; Fill(a, &s); }   /* s.ulLen ∈ [5, 100] */
void Ok_TN(UINT32 n) { UINT8 a[16]; CTX_S s; s.ulLen = 5; if (n) s.ulLen = 16; Fill(a, &s); }    /* s.ulLen ∈ [5, 16] */
```

SPEC-0026 只在字段的当前值是**确切值**时写进实参的 `fields`；汇合后是区间就不写，被调者的 `p1->ulLen` 在调用点不知道（SPEC-0063）→ 两个都记账。
可靠但偏保守：`Br_TP` 本该 POSSIBLE、`Ok_TN` 本该 SAFE。

## 2. 设计

`ArgFields`：整数字段的当前值是两头都有界、来源是代码给出的界（`branch`、`check`…，`IsCodeTag`）的区间 `[lo, hi]`（来源标签 `t`）时，字段值写成调用点局部符号 `_fld<k>_<字段>`（k 是实参下标），
并在调用点记录的 `locals` 里加 `{_fld<k>_<字段>: {range: [lo, hi], bound: t}}`（与实参里用到的调用方局部同一个机制，SPEC-0015）。

只有类型范围的区间（`data` / `type` / `widen`）照旧写成未知（`_unknown_<字段>`）。

汇合（`Join`）：两边都有、值不同的整数内存格原来直接丢掉，调用点上根本没有区间可带；现在按 `JoinValue` 汇合，结果是两头都有界的区间时保留。
循环头上 `WidenState` 丢掉在循环里变了的格（与原来的效果相同，保证收敛）。

判定时它就是一个调用方局部：`apply::Classify` / `replay.py` 的 `classify` 把它换成区间（上界取 `hi`、下界取 `lo`，标签随之），不需要改代换与判定。
区间标签是代码给出的界（`branch`、`check`…）时判 L2，只有类型范围时照旧记账。`_fld…` 不是接口符号，传不上去。

## 3. 对召回与误报的影响

**召回**：分支里给结构体字段赋不同的值再把结构体交下去的写法，按区间判 L2（原来记账）。**误报**：两个分支都在界内的判 SAFE（原来记账）。
区间是可靠的（汇合取并），判 SAFE 要求上界也在界内。

## 4. 实现位置

`core/Interp.cpp`（`ArgFields`、`RecordCall`、`Join` 的内存格、`WidenState`）。

## 5. 测试

* lit `range-field.test`：`Br_TP` → POSSIBLE（末端 100）、`Ok_TN` → SAFE、`Sw_TP`（switch 赋 100 / 4）→ POSSIBLE；对照：外部函数返回的值（`Top`）→ 照旧记账；
  `Dump=summary` 的 `fields` 与 `locals`。
* 单测 `Interp.RangeFieldArg`：菱形分支写 5 / 100 → 调用点 `fields` 是 `_fld0_ulLen`、`locals` 是 `[5, 100] branch`；`Interp.AliasFieldWritten` 守住"读自未知内存的值仍写成未知"。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`calls[].args[].fields`、`calls[].locals`）、`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-25（SPEC-0026） | 实参 `fields` 的整数字段写确切值 | 区间值写成调用点局部符号 `_fld<k>_<字段>`，区间放进 `locals` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 指针字段的区间偏移（`have` 已取最小剩余）；嵌套结构体字段。
