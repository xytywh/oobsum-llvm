# SPEC-0011 取值范围与蕴含：`core/Range`（符号环境、`range_of`、守卫收窄、`entails_le0`）

状态：已实现（S6b）
来源：06- §6.5（A-50…A-52 蕴含判定）、§6.6 A-62（调用点代换后的判定）、D-22（来源标签）、F-12（L2 只认代码给出的界）；
参考实现 `replay.py` 的 `Env`（`_type`、`add_guard`、`bound`、`range_of`）与 `entails_le0`（A-92：C++ 与它逐项一致）。

## 1. 要解决什么

调用点判定（`replay.py` 的 `classify`，S8a 移植）在代换之后要回答两件事：一个线性式在调用方的已知条件下**最大 / 最小能取到多少、这个界来自哪里**
（类型范围？校验？循环？——决定能不能判 L2，F-12）；以及**守卫是否蕴含"末端 ≤ 对象大小"**（证明安全，A-50）。这两件事与 Clang 无关，
放在 `core/`，与 `replay.py` 同语义。

**顺序调整**：`STATUS.md` 原计划的 S6b 是带 `min` / `max` 的符号界、合并与 widening（A-10、A-15…A-17）；它们要和解释器的转移函数一起设计，
移到 S7。本步先做调用点判定要用的数值范围与蕴含，接着 S8a 把调用点判定接上并在 104 份金标准摘要上对照，再做 S7 的解释器。不改任何已定决策。

## 2. 设计

### 2.1 来源标签（D-22）

`Tag`：`type widen data check guard mask loop branch ret const exact`。`range_of` 用到多个界时，结果的标签取**这个顺序里最靠前的**
（`type` 最"差"——只要有一个界只来自类型范围，整体就只来自类型范围；同 `replay.py`）。`IsCodeTag`：`exact const check mask branch loop ret guard`（F-12 能判 L2 的）。

### 2.2 符号环境 `SymEnv`

* 每个符号（`p0`、`p1->ulLen`、`g:g_x`……按名字）可以有下界、上界，各带标签。
* `SetTypeRange(名, lo, hi)`：类型范围，标签 `type`。类型名的表与 `replay.py` 相同（`TypeRangeOf`）：`UINT8` `UINT16` `UINT32` `size_t`
  `CHAR` `INT32`；指针（以 `*` 结尾）与不认识的类型没有范围。
* `AddGuard(L, tag)`（`L ≤ 0`，同 `add_guard`）：只用**单符号**的守卫 `c·s + k ≤ 0`：`c > 0` → `s ≤ ⌊−k / c⌋`，`c < 0` → `s ≥ ⌈k / −c⌉`；
  新界更紧、或原来没有界、或原来的界只来自类型范围时替换（**后者即使新界更松也替换**——守卫给出的界是"代码给出的"，同 `replay.py`）。
  不是单符号的守卫、`ext(…)` 与不透明项上的守卫不记。

### 2.3 原子与线性式的界（同 `bound` / `range_of`）

| 原子 | 上界 | 下界 |
|---|---|---|
| 符号 | 环境里的上界 | 环境里的下界 |
| `ext(…)` | 没有 | `0`（`type`） |
| `div(e, n)` | `⌊e 的上界 / n⌋`，标签同 e | `⌊e 的下界 / n⌋` |
| `mod(e, n)` | `n − 1`（`mask`） | `0`（`mask`） |
| `wrapN(e)` | e 的范围落在 `[0, 2^N)` 里时为 e 的上界，否则 `2^N − 1`（`type`） | 同理为 e 的下界或 `0`；标签都取 e **上界**的标签（同 `replay.py`） |
| `mul(a, b)` | `a 的上界 × b 的上界` | `a 的下界 × b 的下界`；标签取第一个非空的（同 `replay.py`；P0 的 `mul` 只出现在非负量上） |

`RangeOf(L, upper)`：常数加上各项 `c × (c > 0 ? 同向界 : 反向界)`；有一项没有界就整体没有界；int64 溢出也算没有界（以后只会导向"判不了"）。

### 2.4 蕴含（A-50，同 `entails_le0`）

`EntailsLe0(d, guards, env)`：`RangeOf(d, 上界) ≤ 0` → 成立；否则对每个守卫 `g`、`d` 与 `g` 共有且系数同为正的原子 `s`（`d` 里系数 `c`、`g` 里 `b`）：
`rest = b·d − c·g` 的上界 ≤ 0 → 成立（`replay.py` 用有理数 `d − (c/b)·g`，乘以 `b > 0` 不改变符号，这里全用整数）；都不成立 → 判不了。
06- A-50 第 3 步（守卫两两组合）`replay.py` 没有做，这里也不做（与参考实现一致；要加另写 SPEC，并同时改 `replay.py`）。

## 3. 对召回与误报的影响

本步只提供计算，不产生判定。召回相关的设计：没有界、溢出都返回"没有界"，于是蕴含判不了、只会走向 SUSPECT / 上传，不会误判安全。

## 4. 实现位置

`OobSummary/core/Range.{h,cpp}`（不依赖 Clang）。

## 5. 测试

* 单元测试 `oobsum/unittests/core/RangeTest.cpp`：标签优先级、类型表、单符号守卫（上界 / 下界、向下 / 向上取整、替换类型界）、多符号守卫不记、
  六种原子的界、溢出、蕴含（区间直接成立、一次线性组合成立、判不了）。
* **金标准** `oobsum/unittests/core/data/range-golden.txt`：`gen_range_golden.py` 里列出的一组查询（环境 = 形参 / 字段类型 + 带标签的守卫；
  查询 = 某个式子的上 / 下界与标签，或某组守卫是否蕴含 `d ≤ 0`），用 `replay.py` 的 `Env` / `entails_le0` 算出结果；C++ 逐条一致。

## 6. 调试

S8a 的 `Dump=apply` 会把每次判定用到的范围与标签打出来（同 `replay.py -v`）。单测失败时打印查询、C++ 与 `replay.py` 的结果。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-50 第 3 步 | 守卫原子两两组合再做一次 | P0 不做（与参考实现 `replay.py` 一致） |
| D-22 | 来源标签 | 合并多个界时标签取 `type widen data check guard mask loop branch ret const exact` 里最靠前的 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* S7：带 `min` / `max` 的符号界、合并、widening、化简（A-10、A-15…A-17）。
