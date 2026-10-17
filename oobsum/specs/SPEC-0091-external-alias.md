# SPEC-0091 外部内存的别名：形参所指、指针字段所指、全局之间按类型互相失效（修三处错误 SAFE）

状态：已实现
来源：错误 SAFE 排查第三批，**三处错误的 SAFE**：

```c
static UINT8 g_idx[4];
static void A1(UINT8 *p) { UINT8 a[4]; g_idx[1] = 2; p[1] = 200; UseIdx(a, g_idx[1]); }               /* Top1：A1(g_idx) */
static void A2(UINT8 *p, MSG_S *m) { UINT8 a[4]; m->buf[1] = 2; p[1] = 200; UseIdx(a, m->buf[1]); }   /* Top2：A2(b, &m)，m.buf = b */
static void A4(UINT8 *p, UINT32 *q) { UINT8 a[4]; p[0] = 2; *q = 0xC8C8C8C8; UseIdx(a, p[0]); }       /* Top4：A4((UINT8 *)&w, &w) */
```

SPEC-0040 §2.2 只让**所指类型相同的结构体 / 宽整数形参**互相失效，`char` 形参整个排除，形参与全局、形参与指针字段所指的对象之间都不当别名。

## 2. 设计

**外部内存**：形参所指的对象（`P<k>`，它的成员数组 `P<k>.<m>`）、形参结构体里指针字段所指的对象（`PP<k>.<f>`）、全局对象（`G…`）。
局部、本函数分配的堆、字符串常量不会是外部内存的别名。**类型键**：数组取元素类型，其余同 `TypeText`（`rec#号`、`u8`、`u32`、`void`…）；
成员数组同时带外层的键。

经外部内存 X 写时（store、偏移不定的写、交给被调者、被调者的 `mods`），对其它外部内存 Y：

* Y 是**指针视角**（形参所指、指针字段所指）：类型键相同，或 Y 是 `char` / `void`（`char` 指针可以指向任何对象）→ 可能是同一块；
* Y 是**全局对象**：类型键相同 → 可能是同一块（X 也是全局时不算：不同的全局是不同的对象）；
* 不算：X 是 `char` / `void` 而 Y 是别的类型——SPEC-0040 的取舍不变（"先写输出缓冲区、再读输入结构体的长度"很常见）。

可能是同一块的 Y：格全部失效、记进 `writtenAll`（之后读字段 / 全局标量不再是入口值，调用点上 `gvals` 是 `?`）。同一形参自己的成员 / 外层由 SPEC-0090 按范围处理。
顺带：被调者的 `mods` 写了实参所指的对象时，也照 SPEC-0090 失效成员数组 / 外层上重叠的格。

## 3. 对召回与误报的影响

**召回**：修掉 A1 / A2 / A4 三处错误 SAFE。只多失效格，不会少报。
**误报 / 噪声**：经有类型的指针写之后，`char` 缓冲区形参上记着的格失效（这类格很少当长度用）；`char` 缓冲区的写仍不影响结构体形参与非 `char` 的全局。

## 4. 实现位置

`core/Interp.cpp`（`ViewOf`、`TypeKey`、`AliasWritten` 改写；`ApplyMods` 的 `eraseBase` 加 `InvalidateFamily`）。

## 5. 测试

* lit `external-alias.test`：A1 / A2 / A4 不再 SAFE；对照：`char` 输出缓冲区写之后读结构体形参的长度照旧按入口值（`Enc(pucOut, pstIn)` → DEFINITE / SAFE）；
  经 `UINT32 *` 写之后读另一个 `UINT32` 全局照旧（类型不同的全局不算）。
* 单测 `Interp.ExternalAlias`（全局数组与 `UINT8 *` 形参）。
* SPEC-0040 的 `field-current.test` 照旧；`run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变；`perf-chain.py` 2000×300 约 0.9 秒。

## 6. 调试

`Dump=state`（格）、`Dump=summary`（调用点的 `fields` / `gvals`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0040 §2.2 | 只看所指类型相同的结构体 / 宽整数形参 | 外部内存（形参所指、指针字段所指、全局）按类型键互相失效；`char` / `void` 指针视角被任何写影响；`char` 写不影响别的类型 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 结构体里嵌套的成员结构体（`P0` 写 `rec#1`，`P1` 指向它的内层 `rec#2`）不算别名（类型不同）；真遇到再说。
