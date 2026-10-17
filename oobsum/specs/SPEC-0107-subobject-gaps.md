# SPEC-0107 内层界补漏：指针存进结构体字段、返回成员数组的指针、负偏移、汇合（用户决定 D24）

状态：第 1 步（结构体字段里的指针）、第 2 步（返回成员数组 / 一行的指针）已实现；第 3、4 步见 §2.2
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

* 第 2 步（#2，已实现）：原来 `return p->aucBuf`（形参所指结构体里的成员数组）根本不记 `ret_ptr`（注释是"成员界会丢"），调用方只能记 `untracked_pointer`；
  `return a[r]` 记了 `ret_ptr p0 + 4*p1` 但不带窗口。改为：
  * `ReturnPointer`：成员数组基址换成"形参所指 + 成员偏移"、成员本身当最外层窗口；各条返回的最内层窗口（大小、偏移确切且相同）记进 `ret_ptr` 的
    `sub`（子对象大小）/ `sub_off`（返回的指针在子对象里的偏移，被调者接口符号）。摘要 `effs[]` 加这两个键（`summary.schema.json`、`core/Summary.cpp`）。
  * `RetPtrResult`（调用方）：结果 = 实参 + 偏移，**实参原有的窗口一起平移**（原来没平移，`Next(st.aucBuf)` 返回 `p + 1` 后内层偏移差 1）；
    有 `sub` 时推一个窗口（`sub_off` 换成调用方的值；`Subobject=off` 不推）。
  * 返回成员数组的指针现在跟踪得到：调用方原来的 `untracked_pointer` 换成外层 / 内层都检查（证明了安全的才不报）。
* 第 3 步（#3）：实参另记窗口前面的字节数，被调者的反向 Req 按它再判一次。
* 第 4 步（#4）：汇合时一边有窗口、另一边没有，保留有的那边（那条路上就是越出了子对象，检出优先）。

## 3. 对召回与误报的影响

只多报（内层越界），外层的检查不变。

## 4. 实现位置

第 1 步：`core/Interp.cpp`（`SubRest`、`ArgFields`）、`core/Apply.cpp`（`SubCall`）、`replay.py`（`sub_call`）。
第 2 步：`core/Interp.cpp`（`ReturnPointer`、`RetPtrResult`）、`core/Summary.{h,cpp}`（`Eff::sub` / `subOff`）、`summary.schema.json`。

## 5. 测试

* 第 1 步：lit `subobject-field.test`（`ctx.pucBuf = st.aucBuf; Proc(&ctx, 20)` → SUSPECT、`Proc(&ctx, 16)` → SAFE、`Dump=summary` 的字段值带 `sub`）；
  单测 `Apply.SubObjectFieldArg`。
* 第 2 步：lit `subobject-ret.test`（`Dump=summary` 的 `ret_ptr` 带 `sub` / `sub_off`；`GetBuf(&st)[20]` → SUSPECT、`[15]` → SAFE；`GetRow(a, 0)[5]` → SUSPECT；
  `Next(st.aucBuf)[15]` → SUSPECT、`[14]` 不报）；单测 `Interp.RetPtrMemberWindow`。compiler-rt、上游用例集不变（判定键 1370 → 1370）。

## 6. 调试

`Dump=summary`（字段值的 `sub`）、`Dump=apply`（行尾 `[sub]`）。

## 7. 对主规格的修订

无（沿用 SPEC-0104 的 `sub` 与内层变体）。

## 8. 公司侧验证

随 SPEC-0104 / 0105 一起看 `subobject_overflow` 的条数。

## 9. 待定 / 以后

见 §2.2。
