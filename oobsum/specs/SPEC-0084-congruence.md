# SPEC-0084 区间带同余信息（值 ≡ 锚点 mod 步长）：宽元素指针循环、步长大于 1 的整数循环判准

状态：已实现
来源：SPEC-0083 / STATUS 的已知局限：

```c
static void W1(UINT32 *p, UINT32 n) { UINT32 *e = p + n; while (p < e) { *p = 0; p++; } }        /* W1(a, 4)：SUSPECT（SPEC-0083） */
static void E1(ENT_S *p, UINT32 n) { ENT_S *q; for (q = p; q < p + n; q++) { q->b = 0; } }        /* 同上 */
static void I4(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i += 4) { *(UINT32 *)(p + i) = 0; } }  /* I4(a, 16)：POSSIBLE 误报 */
```

区间域假设区间里的每个值都可能取到：`p.off ∈ [0, 4n − 1]` 其实只取 4 的倍数，最大 `4n − 4`。

## 2. 设计

### 2.1 值

`IntVal` 的区间加 `stride`（≥ 2 时有效）与 `anchor`（线性式）：区间里的值都 ≡ `anchor`（mod `stride`）。`stride` 为 0 = 不知道；
确切值本身就是"锚点 = 值"。锚点规范化：系数与常数都按步长取正余数（`x` 是整数，`c·x ≡ (c mod s)·x`），同余类相同的写法相同。
**默认不知道**：新建的区间都没有同余信息，只在下面几处显式带上——没改到的地方自动丢掉，保守。

### 2.2 运算

| 运算 | 同余 |
|---|---|
| 汇合 / widen `a ⊔ b` | 步长 = gcd(`sa`, `sb`, 锚点之差的常数与各系数)；≥ 2 才记，锚点取 `a` 的 |
| `a + b`（含指针加偏移、成员偏移） | 步长 = gcd(`sa`, `sb`)，锚点 = 两个锚点之和（确切值步长按 0） |
| `a × c` / `a << k` | 有同余：步长 × \|c\|、锚点 × c；没有：步长 \|c\|、锚点 0 |
| 无符号加 / 乘可能回绕（SPEC-0017 §2.4 那一支） | 步长是 2 的幂且不超过 2^W 才保留（回绕减 2^W 不改余数） |
| 其余（除、取余、位运算、类型转换给类型范围…） | 丢掉 |

### 2.3 收窄

条件边收窄（`NarrowRange`，整数与指针共用）时，候选的界先取到同余类里，再与原来的界比：

* 上界向下：`anchor + s·div(hi − anchor, s)`（`div` 是向下取整，任何符号都对）；差的各系数都能被 `s` 整除时直接减常数余数（`4n − 1` → `4n − 4`）。
* 下界向上：只做差的各系数都能被 `s` 整除的情形。
* 收成空（`x ∈ {0, 8}` 且 `x == 3`）→ 不可达。
* 上界取整成功时，SPEC-0083 的宽元素 `coarse` 不再需要（界已经是同余类里最大的值），标签照常 `loop` / `check`。

### 2.4 打印

`Dump=state` 里区间后面加 `%步长`（锚点不是 0 时 `%步长~锚点`）：`p=*p0+[0, 4*p1 - 4]:loop%4`。

## 3. 对召回与误报的影响

**召回**：可靠——同余只在能证明时记，取整只去掉同余类以外取不到的值。`ENT_S *pEnd = (ENT_S *)(pucBuf + ulLen)` 这类结束指针不是步长的倍数时，
取整成 `16·div(ulLen − 1, 16)`，`ulLen = 40` 时末端 48 > 40 照样报出。
**误报**：宽元素指针循环、`i += 4` 循环的界内访问判 SAFE、越界的判 POSSIBLE（原来 SUSPECT / POSSIBLE 误报）。

## 4. 实现位置

`core/Interp.h`（`IntVal::stride` / `anchor`、相等、打印）、`core/Interp.cpp`（`CongOf` / `SetCong` / `JoinCong` / `RoundDown` / `RoundUp`、
`AddVals`、`ScaleVal`、`JoinInt`、`WidenInt`、`Bin` 的回绕分支、`NarrowRange`）。

## 5. 测试

* lit `congruence.test`：`W1(a, 4)` SAFE、`W1(a, 5)` POSSIBLE；结构体指针 `E1` 同样；`I4(a, 16)` SAFE、`I4(a, 17)` POSSIBLE；
  结束指针是字节长度（`(ENT_S *)(buf + len)`）：`len = 40` POSSIBLE、`len = 48` SAFE；`Dump=state` 体内 `%4`。
* 单测 `Interp.CongruencePointerLoop`（指针加常数、汇合、widen、常数余数取整）、`Interp.CongruenceStepLoop`（`i += 4`、回绕分支保留、`div` 取整）、
  `Interp.CongruenceBranchJoin`（两个常数汇合出 `%8`）；`Interp.WidePointerCompareIsCoarse` 改成按 1 字节走的 `UINT32 *`（没有同余，仍标 `widen`）。
* SPEC-0083 的 lit（`pointer-compare.test`）按新结果更新：`W1(a, 4)` SUSPECT → SAFE、`W1(a, 5)` SUSPECT → POSSIBLE（同一位置判得更准）；
  coarse 改用按 1 字节走的 `W2` 守住。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变；`perf-chain.py` 耗时不变。

## 6. 调试

`Dump=state`（`%步长`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-10 整数值 | `[lo, hi]` | 区间可带同余（`≡ anchor mod stride`），只在 §2.2 的运算里保持 |
| SPEC-0083 §2 | 宽元素指针收窄出的上界标 `widen` | 上界按同余取整成功时照常标 `loop` / `check` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 下界的符号取整；`!=` 循环步长大于 1（SPEC-0082 的阈值要求起点到 `c` 的差是步长的倍数，现在证明不了）。
