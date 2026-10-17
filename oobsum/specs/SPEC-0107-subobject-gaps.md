# SPEC-0107 内层界补漏：指针存进结构体字段、返回成员数组的指针、负偏移、汇合（用户决定 D24）

状态：第 1 步（结构体字段里的指针）已实现；其余见 §2
来源：D24"不管哪个维度，每个越界都告警"。SPEC-0104 之后还有几处只查整个对象、内层越界不报（SPEC-0104 §2.3 末尾列的"不覆盖"与之后发现的）。

## 1. 漏洞

| # | 写法 | 原来 |
|---|---|---|
| 1 | `ctx.pucBuf = st.aucBuf; Proc(&ctx);`，`Proc` 里 `pctx->pucBuf[i]` 越出 `aucBuf` | 字段里的指针不记内层剩余，只查 `st` |
| 2 | `UINT8 *GetBuf(CTX_S *p) { return p->aucBuf; }`，调用方 `GetBuf(&st)[20]` | 返回的指针不带窗口，只查 `st` |
| 3 | `Fill(st.aucBuf, …)` 里 `p[-1]`（越到成员前面、还在 `st` 里） | 被调者的反向 Req 只查外层 |
| 4 | `p = c ? st.aucBuf : (UINT8 *)&st;` 之后 `p[20]` | 两边窗口对不上，汇合时丢掉 |

## 2. 设计

### 2.1 结构体字段里的指针（第 1 步）

* 调用点记实参所指结构体的字段值（`ArgFields`，SPEC-0026）：字段值是带窗口的指针时同样记 `sub`（同 SPEC-0104 3b 的实参）。
* `apply::SubCall`（同 `replay.py` 的 `sub_call`）：被调者的正向 Req、对象是 `*p<k>->f`、`have` 正好是 `ext(p<k>->f)`、第 k 个实参的 `fields[f]` 带 `sub` →
  把这个字段值换成"只剩 `sub`"再判一次（内层变体，同 3b）。

### 2.2 其余（后续步骤）

* 第 2 步（#2）：`ret_ptr` 效果带子对象的窗口——待查现在 `return p->aucBuf` 记成什么。
* 第 3 步（#3）：实参另记窗口前面的字节数，被调者的反向 Req 按它再判一次。
* 第 4 步（#4）：汇合时一边有窗口、另一边没有，保留有的那边（那条路上就是越出了子对象，检出优先）。

## 3. 对召回与误报的影响

只多报（内层越界），外层的检查不变。

## 4. 实现位置

第 1 步：`core/Interp.cpp`（`SubRest`、`ArgFields`）、`core/Apply.cpp`（`SubCall`）、`replay.py`（`sub_call`）。

## 5. 测试

* 第 1 步：lit `subobject-field.test`（`ctx.pucBuf = st.aucBuf; Proc(&ctx, 20)` → SUSPECT、`Proc(&ctx, 16)` → SAFE、`Dump=summary` 的字段值带 `sub`）；
  单测 `Apply.SubObjectFieldArg`。

## 6. 调试

`Dump=summary`（字段值的 `sub`）、`Dump=apply`（行尾 `[sub]`）。

## 7. 对主规格的修订

无（沿用 SPEC-0104 的 `sub` 与内层变体）。

## 8. 公司侧验证

随 SPEC-0104 / 0105 一起看 `subobject_overflow` 的条数。

## 9. 待定 / 以后

见 §2.2。
