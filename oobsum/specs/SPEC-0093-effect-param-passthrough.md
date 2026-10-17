# SPEC-0093 写效应的值就是被调者的形参时，按实参的值写（实参是区间也行）

状态：已实现
来源：错误 SAFE 排查第七批（没有错误 SAFE，发现一处精度问题）：

```c
static void SetLen(S_S *s, UINT32 n) { s->len = n; }                      /* field p0->len := p1 */
void E4(UINT32 k) { UINT8 x[4]; S_S s; SetLen(&s, k < 3 ? 2 : 100); Put(x, s.len); }   /* SUSPECT type_range_only */
```

调用点应用写效应（SPEC-0073 / 0077）时，值经 `CallerExpr` 代换，只有实参是**忠实的确切值**才代；实参是区间（`[2, 100]`）→ 换不了 → 字段不知道。
setter 函数（`Cfg_SetLen(pst, ulLen)`）在业务代码里很常见，调用方传的往往是校验过的区间。

## 2. 设计

写效应（`field` / `global`）的值**正好是**被调者的一个形参 `p<k>`（没有别的项）：格里写第 k 个实参的值本身（`EvalInt`，确切值、区间都行，带它的标签；
不知道 → 照旧不写）。这与调用方自己写 `s.len = 实参` 完全一样（同一个值、同一套"确切值按数学值"的约定），不需要忠实检查。
其余写法（`p1 + 4`、字段符号…）照旧走 `CallerExpr`。`may`（有路径没写）照旧与调用前的值汇合。

## 3. 对召回与误报的影响

**召回**：可靠——写进去的就是被调者写的那个值。**误报 / 级别**：setter 之后按真实区间判（E4：POSSIBLE，原来 SUSPECT）。

## 4. 实现位置

`core/Interp.cpp`（`FieldWrites`）。

## 5. 测试

* lit `effect-param.test`：`SetLen(&s, k < 3 ? 2 : 100)` → POSSIBLE；`SetLen(&s, k < 3 ? 1 : 2)` → SAFE；全局 setter 同样；对照：`s->len = n + 4` 照旧（区间实参换不了 → SUSPECT）。
* 单测 `Interp.EffectParamPassthrough`（分支汇合出 `[2, 100]` 的实参 → 调用后的访问末端 `[3, 101]`）。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。
* 顺带：单测构建里 `CasesOf` 的聚合初始化少了 `IntCase::writes`（SPEC-0079 加的成员）→ `-Wmissing-field-initializers`，补上。

## 6. 调试

`Dump=state`（调用后的格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0073 §2.2 | 值换成调用方的说法（`CallerExpr`），换不了不应用 | 值正好是 `p<k>` 时直接写实参的值（含区间） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `p<k> + c` 的区间实参（要考虑回绕）。
