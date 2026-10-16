# SPEC-0072 Interp 代入被调者的式子（`ret_int` / `ret_ptr` 的偏移）：字段符号按实参所指对象的当前值换，换不了算未知（错误的 SAFE）

状态：已实现
来源：做 SPEC-0071 时发现。`CallerExpr`（SPEC-0070 从 `CondResult` 提出来；SPEC-0054 的 `ret_int` 符号区间）只把被调者的 `p<k>` 换成确切实参，
检查"换不了"时也只认 `p<数字>`——被调者的字段符号 `p0->ulLen` 原样留下，被当成**调用方自己**的 `p0` 的字段。探针：

```c
static UINT32 GetLen(const CTX_S *c) { return c->ulLen; }          /* ret_int [p0->ulLen, p0->ulLen] */
void Top_TP(CTX_S *pstMine) { UINT8 a[16]; CTX_S st; st.ulLen = 100; if (pstMine->ulLen > 10) return; Put(a, GetLen(&st)); }
```

`GetLen(&st)` 的结果被当成 `pstMine->ulLen`，守卫 `<= 10` 一蕴含 → **"Proven within bounds"——错误的 SAFE**（实际写 `a[100]`）。

## 2. 设计

`CallerExpr` 多带被调者的摘要：

* 被调者 `fields` 表里的 `p<k>->f`：从第 k 个实参所指对象按字段偏移读出**调用前**的当前值（`LoadValue`，同调用方自己读 `st.ulLen`）。确切值 → 换成它；
  实参是调用方自己的形参、字段没改过时读出来就是调用方的 `pJ->f`（别名，SPEC-0026 / 0040 的规则自然成立）。
* 其余没换掉的被调者接口符号按 SPEC-0063 改名（`Substitution::poison`），算"换不了" → 返回值 / 偏移未知。

## 3. 对召回与误报的影响

**召回**：修掉"被调者返回字段值、调用方借用自己同名形参的字段判安全"的错误 SAFE。原来能判的：形参换成确切实参照旧；字段值确切的现在也能换。

## 4. 实现位置

`core/Interp.cpp`（`CallerExpr`、`CondResult`、`RetPtrResult`）。

## 5. 测试

* lit `callee-field-ret.test`：探针 → `Top_TP` 判 POSSIBLE / DEFINITE（`st.ulLen = 100`），不再 SAFE；`Own_TN`（`GetLen(pstMine)`，别名）→ 照旧 SAFE。
* 单测 `Interp.CalleeFieldResult`：手造被调者摘要 `ret_int [p0->ulLen, p0->ulLen]`，调用方写 100 后调用 → 本函数里判出 `a[100]`（末端 101），不出现 `p0->ulLen`。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=state`（调用结果的值）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0054 §2.2 | 被调者形参换成可信的确切实参；代完还含被调者形参 → 未知 | 字段符号按实参所指对象的当前值换；没换掉的被调者接口符号（含字段）→ 未知 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
