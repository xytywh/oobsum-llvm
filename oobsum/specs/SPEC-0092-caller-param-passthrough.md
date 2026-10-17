# SPEC-0092 调用方把自己的形参传给被调者时，被调者的返回值式子照样能代换；文本不露 `_unknown<k>`

状态：已实现
来源：错误 SAFE 排查第五批（没有错误 SAFE，发现一处精度问题与一处文本问题）：

```c
static UINT32 Id(UINT32 x) { return x; }                         /* ret_int [p0, p0] */
static void Mid(UINT8 *p, UINT32 n) { Put(p, Id(n)); }           /* Id(n) 的结果没算出来 → Mid 记 SUSPECT entry_unproven，链断在这里 */
void P1_TP(void) { UINT8 a[4]; Mid(a, 4); }                      /* 实际写 a[4] */
static UINT32 Idx(UINT32 x) { return x + 1; }
static UINT32 Twice(UINT32 x) { return Idx(Idx(x)); }
void R7(void) { UINT8 a[4]; Put(a, Twice(2)); }                  /* 正文 "up to byte _unknown1 + 1 may be accessed" */
```

`CallerExpr`（SPEC-0054 / 0072）代换完检查"结果里还有没有 `p<k>`"来判断被调者的形参没换掉。可调用方把**自己的形参**传进去时，代换出来的就是调用方的 `p0`，
也被当成没换掉 → 未知。SPEC-0063 之后，没换掉的被调者接口符号在代换时已经改名成 `_unmapped:…`，只查它就够了。

文本：调用点上不知道的整数实参写成 `_unknown<k>`，`Readable` 只处理了 `_unknown_<字段>`、`_unknown__0`，`_unknown1` 原样进了告警正文。

## 2. 设计

* `CallerExpr`：结果里有 `_unmapped:` 符号才算换不了（去掉 `p<k>` 的检查）。
* `Readable`：`_unknown<数字>` 写成 `an unknown value`（同 SPEC-0068 的 `_unknown__0`）。

## 3. 对召回与误报的影响

**召回**：不变（`_unmapped:` 照旧把没换掉的被调者符号挡住）。**误报 / 级别**：经包装函数（`Id(n)`、`Half(n)`、`Min(a, b)` 这类）把调用方自己的形参传下去时，
返回值照样代换，调用链判到底（P1 / P2：TP DEFINITE、TN SAFE，原来中间函数记 SUSPECT）。
`Twice` 这种两层 `x + 1`：第二次的实参是 `p0 + 1`，可能回绕、不忠实，照旧不代换（按值约定的可靠做法），R7 仍 SUSPECT，只是正文不露内部名字。

## 4. 实现位置

`core/Interp.cpp`（`CallerExpr`）、`core/Engine.cpp`（`Readable`）。

## 5. 测试

* lit `caller-param-ret.test`：`Mid(a, 4)` / `Mid2(a, 8)` → DEFINITE（2 层链）、`Mid(a, 3)` / `Mid2(a, 6)` → SAFE；`Dump=summary` 里 `Twice` 第二次调用的实参是 `p0 + 1`（原来 `_unknown0`）；
  `R7` 的正文是 "an unknown value + 1"。
* 单测 `Interp.CallerParamPassthrough`（被调者 `ret_int [p0, p0]`，调用方传自己的 `p1` → `end=[p1 + 1, p1 + 1]`）；正文的 `an unknown value` 由 lit 守住。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=summary`（`effs` 的 `ret_int`、调用点实参）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0072 §2 | 没换掉的被调者接口符号 → 未知（并检查结果里的 `p<k>`） | 只按 `_unmapped:` 判；调用方自己的形参可以出现在结果里 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
