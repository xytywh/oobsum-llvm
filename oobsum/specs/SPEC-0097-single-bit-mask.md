# SPEC-0097 单个位的掩码 `x & 2^k` 写成 `2^k·mod(div(x, 2^k), 2)`：标志位判断产生守卫（修 DEFINITE 误报）

状态：已实现
来源：找"DEFINITE 判过头"的探针第二批：

```c
static void Bit(UINT8 *p, UINT32 f) { if (f & 0x4) { p[10] = 0; } }
void M5(void) { UINT8 a[4]; Bit(a, 0x3); }      /* DEFINITE——f = 3 时访问不执行 */
```

`AndConst`：低位掩码（`2^k − 1`）写成 `mod(x, 2^k)`，其余掩码只给区间 `[0, m]`（标签 `mask`）。`f & 4` 是区间，`!= 0` 推不出守卫，
访问在摘要里成了无条件的。`if (ulFlags & FLAG_X)` 在业务代码里非常普遍。

## 2. 设计

* `AndConst(x, m)`：`m` 是 2 的幂（单个位）且 `x` 是确切值 → 确切值 `m·mod(div(x, m), 2)`。`div` / `mod` 是向下取整、非负余数（`LinExpr::Div` / `Mod`），
  对有符号数（补码）也是第 k 位，恒等成立。其余照旧。
* 之后 `(f & 4) != 0` / `== 4` / `== 0` 与常数比较按原来的规则推出守卫（`1 − 4·mod(div(p1, 4), 2) ≤ 0` 等，只含接口符号）；调用点代入常数时 `div` / `mod` 当场算出，
  守卫为假 → SAFE `guard_false`（SPEC-0096）。

## 3. 对召回与误报的影响

**召回**：可靠——式子与位运算恒等，守卫只在代成常数为假时让访问走不到。**误报**：传了不含那个标志位的调用方不再报 DEFINITE。

## 4. 实现位置

`core/Interp.cpp`（`AndConst`）。

## 5. 测试

* lit `bit-flag.test`：`if (f & 0x4)`、`if ((f & 0x8) == 0x8)`、`if ((f & 0x2) == 0)` 下的访问，调用方的 `f` 让条件为假 → SAFE `guard_false`、为真 → DEFINITE；
  对照：多位掩码 `f & 0x6` 照旧（无守卫 → DEFINITE）。
* 单测 `Interp.SingleBitMask`。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测：原来报出的一处没少（逐访问对比），多 1 条 `typed_field`。

## 6. 调试

`Dump=state`（`f & 4` 的值）、`Dump=summary`（Req 的守卫）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-24 / A-25（`&`） | 低位掩码 → `mod`；其余 → `[0, m]` | 另：单个位 `2^k` 且确切 → `2^k·mod(div(x, 2^k), 2)` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 多位掩码（`f & 0x6`）：要析取，不做。
