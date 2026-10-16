# SPEC-0063 调用点代换：映射表里没有的被调者接口符号不再"变成"调用方的符号（错误的 SAFE）

状态：已实现
来源：查 SPEC-0061 对照里告警对象名是 `'?'` 时发现。代换（`Substitute` / `replay.py` 的 `subst`）对映射表里没有的符号**原样保留**：被调者的 `p0->f`、
`ext(p0->f)`、`bext(p0->f)`（实参没带这个字段的值、也不是别名），空实参的 `p_i` / `ext(p_i)`，代换后仍叫 `p0…`，被当成**调用方自己的**形参符号，
可以继续上传，在更上层被套上别的对象的值。探针：

```c
static void Res_Fill(RES_S *pst, UINT32 i) { Res_Write(pst->pucRes, i); }                 /* Req：i + 1 <= ext(p0->pucRes) */
static void Mid(RES_S *pstOuter, UINT32 i) { RES_S st; st.ulId = pstOuter->ulId; st.pucRes = Ext_GetSmall(); Res_Fill(&st, i); }
void Top(void) { UINT8 aucBig[128]; RES_S outer; outer.pucRes = aucBig; Mid(&outer, 100); }
```

`Mid` 调 `Res_Fill(&st, i)` 时 `st.pucRes` 不知道（外部函数返回），`ext(p0->pucRes)` 没映射 → 原样上传成 `Mid` 的 Req → `Top` 把它代成 `outer.pucRes` 的 128 字节 →
**"Proven within bounds: up to byte 101 of '?' (128 bytes)"——错误的 SAFE**（这处访问连 SUSPECT 都没有）。

## 2. 设计

* `Substitution` 加 `poison`（只有调用点代换 `apply::BuildMap` 打开；`replay.py` 的 `build_map` 同步）：映射表里没有、也不是别名的**被调者接口符号**
  （`p<i>`、`p<i>->f` 及它们的 `ext(…)` / `bext(…)`，嵌在 `div` / `mod` / `wrap` / `mul` 里的也算）改名成非接口符号 `_unmapped:<原文>`。
  全局符号 `g:…` 两边同名同义，不改。改名后的符号不是接口符号：含它的条件既判不出安全，也传不上去（只能记账）。
* 判定：对象大小里含 `_unmapped:` → `open`、原因 `untracked_pointer`（被调者经实参所指对象里的指针访问，而调用方不知道那个指针指向哪里）；
  末端 / 守卫里含它的照原来的路走（落到 `entry_unproven`）。
* 文本：大小写 "size unknown"（`Engine.cpp` 的 `SizeText`）。

## 3. 对召回与误报的影响

**召回**：修掉一类错误的 SAFE（字段指针 / 空实参的符号串到上层的同名形参上）。原来能判的不受影响：映射到的符号、别名改名照旧；只有原来"借用"了调用方同名符号的
判定变成记账。

## 4. 实现位置

`core/LinExpr.{h,cpp}`（`Substitution::poison`、`Substitute`）、`core/Apply.cpp`（`BuildMap`、`Classify`）、`core/Engine.cpp`（`SizeText`）、`replay.py`（`build_map`、`subst`、`classify`）。

## 5. 测试

* 单测 `LinExpr.SubstitutePoison`：没映射的 `p0->f`、`ext(p0->f)`、`bext(p1)`、`wrap` 里的 `p2` 改名；映射到的、别名、`g:` 不变；`poison` 关着时照旧。
* lit `unmapped-symbol.test`：探针 → `Mid` 调用点 `open (untracked_pointer)`、"stays within '*pst->pucRes' (size unknown)"；`Top` 上不再有那条 SAFE。
* `run.py csa` 不回退（LV5-02、LV10-05 的同一条 SUSPECT 从入口声明处挪到调用点：原来是泄漏的字段符号一路传到入口）；`replay.py`、`engine_check`、
  `Apply.AllCasesMatchReplay` 不回退。`out-new-field.test` 的对照按新原因更新。

## 6. 调试

`Dump=apply`（"→ open (untracked_pointer)"）、`Dump=summary`（上传的 Req 里不再出现被调者的字段符号）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-61（代换） | 形参、字段、`ext` 按调用点记录代换 | 另：映射表里没有的被调者接口符号改名成非接口符号，不能当成调用方的同名符号 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 调用点记录里实参所指对象的指针字段不知道时，也可以写占位符（同 SPEC-0062），原因更细。
