# SPEC-0095 指针实参的偏移是区间：调用点按"对象起点 + 偏移区间"判，不再拿最小剩余判 L1（修 DEFINITE 误判）

状态：已实现
来源：做 SPEC-0094 时发现，**DEFINITE 判错**（不是漏报，是把"可能越界"判成"明确越界"）：

```c
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }                                     /* p0 + 13 ≤ ext(p0) */
void T1(UINT32 c) { UINT8 a[16]; UINT8 *p = c ? a + 2 : a + 6; Put(p, 12); }        /* DEFINITE "up to byte 13 of 'a' (10 bytes)"——c 为真时写 a[14]，界内 */
void T3(void) { UINT8 a[300]; UINT8 j = GetIdx(); Put(a + j, 12); }                 /* DEFINITE "(-2147483347 bytes)"——j = 0 时界内 */
```

调用点记录指针实参时，偏移是区间就取**最小的剩余**（`have = size − off_hi`，SPEC-0015 起"多报不少报"）。证明安全时这样对；可 F-11 的 L1
（"对所有取值都越界"）要求所需的下界大于剩余的**上界**，拿最小剩余判 L1 就判过了头；偏移的上界只来自类型范围时剩余甚至是负数。

## 2. 设计

* 调用点记录：指针实参的偏移是区间（上下界不同）时，另记 `off_lo` / `off_hi`（调用方的说法；下界不知道时不写 `off_lo`）与 `off_bound`
  （偏移上界的来源标签）。`have` 照旧（最小剩余，兼容老格式与金标准）。
* 代换（`apply::Classify`）：被调者 Req 的 `have` 正好是 `ext(pK) + c`、第 K 个实参带偏移区间时，改按"对象起点"判：
  `have := 实参 have + off_hi`（= 从对象起点算的剩余）、`end := end + [off_lo, off_hi]`，界的标签并入 `off_bound`（取更"差"的）。
  于是 F-10 / F-11 / F-12 照常：最大末端 ≤ 剩余 → SAFE；最小末端 > 剩余 → L1；其余看标签（代码给出的界 → L2，否则记账）。
  偏移下界不知道 → 不判 L1。往上传的 Req 也按这个说法（末端带上偏移区间）。反向（`back`）不变。

## 3. 对召回与误报的影响

**召回**：不变——判 SAFE 的条件与原来相同（最大末端对最小剩余）；原来的 L1 变成 L2 或 SUSPECT，仍然报出。**级别**：T1 → POSSIBLE、T3 → SUSPECT `type_range_only`
（原来 DEFINITE）。正文里的大小是对象的大小（`16 bytes`），末端从对象起点算。

## 4. 实现位置

`core/Summary.{h,cpp}`（`PtrArg::offLo` / `offHi` / `offBound` 读写）、`summary.schema.json`、`core/Interp.cpp`（调用点记录）、`core/Apply.cpp`（`Classify`、`ParentCall`）。

## 5. 测试

* lit `arg-offset-range.test`：T1 → POSSIBLE（"up to byte 19 of 'a' (16 bytes)"）、T3 → SUSPECT、偏移区间都在界内 → SAFE、偏移区间都越界 → DEFINITE；
  经一层中间函数往上传后同样。
* 单测 `Propagate.ArgOffsetRange`（偏移 `[2, 6]` → L2、have 16、末端 19；偏移上界只来自类型 → 记账；老格式只有 `have` → 照旧 L1；读写往返带 `off_*`）。
* `Apply.AllCasesMatchReplay`、`replay.py`（104 份摘要按 schema 校验通过）、`engine_check` 不回退（金标准没有 `off_*`）；`run.py csa` 与基线相同；
  compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=summary`（调用点实参的 `off` / `off_bound`）、`Dump=apply`（代换后的末端与剩余）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-25 指针实参 | `{obj, have, …}`，偏移是区间时 `have` 取最小剩余 | 另记 `off` / `off_bound`；代换时按对象起点 + 偏移区间判 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 已做：成员数组外层对象（`parent`）的判定同样走这条路（`ParentCall` 把 `off_*` 带过去，`parent.have` 一起加 `off_hi`）。
* 被调者 Req 的 `have` 不是 `ext(pK) + c` 的写法（`bext`、多个 `ext`）照旧按最小剩余判。
