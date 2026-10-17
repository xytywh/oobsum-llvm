# SPEC-0107 内层界补漏：指针存进结构体字段、返回成员数组的指针、负偏移、汇合（用户决定 D24）

状态：已实现（第 1…4 步）
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
* 第 3 步（#3，已实现）：内层反向 Req（`dir: subback`）。
  * 调用点记实参（与字段值）时另记 `sub_back`：从指针往前到最内层子对象起点的字节数（窗口偏移下界；窗口从基址起点开始时不记）。
  * `apply::SubCall`：被调者的反向 Req（对象 `*p<k>` / `*p<k>->f`、`have` 正好是 `bext(…)`）→ 实参的 `back` 换成 `sub_back` 再判一次（`dir: subback`），同 3b 的内层变体。
  * `CheckSubs`：下界（负下标）证明不了、式子里有接口符号（或 `Subobject=label`）→ 记内层反向 Req（`have` 0、`end` = 越到子对象前面的字节数），往调用方传；
    其余照旧记账。
  * `subback` 与 `sub` 同样：L1 / L2 改判 SUSPECT（`label` 下照常）、入口账本原因 `subobject_overflow`、不按 `InlineDepth` 过滤；文字
    `Cannot prove the write … stays within the array member or row of 'st' (0 bytes before the pointer): up to 1 bytes before it may be accessed`。
  * 摘要：`dir` 加 `subback`、实参加 `sub_back`（`summary.schema.json`、`core/Summary.cpp`）；`replay.py` 同步（`sub_call`、`classify`、入口原因、`alert_msg`）。
* 第 4 步（#4，已实现）：`JoinSubs`——大小（与 head / tail）相同的窗口两两汇合 / 加宽；对不上的两边都留下（至多 4 个，丢最外层的）。
  每个窗口对应一条路径，访问越出它就是那条路上越出了子对象（检出优先）；外层检查不变。收敛：窗口个数有上限，下一轮配上对的照常加宽。

## 3. 对召回与误报的影响

只多报（内层越界），外层的检查不变。

## 4. 实现位置

第 1 步：`core/Interp.cpp`（`SubRest`、`ArgFields`）、`core/Apply.cpp`（`SubCall`）、`replay.py`（`sub_call`）。
第 2 步：`core/Interp.cpp`（`ReturnPointer`、`RetPtrResult`）、`core/Summary.{h,cpp}`（`Eff::sub` / `subOff`）、`summary.schema.json`。
第 3 步：`core/Interp.cpp`（`SubBack`、`CheckSubs`）、`core/Apply.{h,cpp}`（`InnerDir`、`SubCall`、`ClassifyAll`）、`core/Solver.cpp`、`core/Policy.cpp`、
`core/Engine.cpp`、`core/Alert.cpp`、`core/Summary.{h,cpp}`、`summary.schema.json`、`replay.py`、`tools/golden_regen.py`。
第 4 步：`core/Interp.cpp`（`JoinSubs`）。

## 5. 测试

* 第 1 步：lit `subobject-field.test`（`ctx.pucBuf = st.aucBuf; Proc(&ctx, 20)` → SUSPECT、`Proc(&ctx, 16)` → SAFE、`Dump=summary` 的字段值带 `sub`）；
  单测 `Apply.SubObjectFieldArg`。
* 第 2 步：lit `subobject-ret.test`（`Dump=summary` 的 `ret_ptr` 带 `sub` / `sub_off`；`GetBuf(&st)[20]` → SUSPECT、`[15]` → SAFE；`GetRow(a, 0)[5]` → SUSPECT；
  `Next(st.aucBuf)[15]` → SUSPECT、`[14]` 不报）；单测 `Interp.RetPtrMemberWindow`。compiler-rt、上游用例集不变（判定键 1370 → 1370）。
* 第 3 步：lit `subobject-back.test`（实参的 `sub_back`；`Prev(st.aucBuf)` 里 `p[-1]` → SUSPECT、`Prev(st.aucBuf + 1)` 不报、`Prev(a[1])` → SUSPECT；
  `SetRow(0)`（`a[1][i - 1]`）→ SUSPECT、`SetRow(1)` 不报；`Dump=summary` 的 `subback` Req）；单测 `Apply.SubObjectBackArg`；`subobject-option.test` 的 `label` 下
  常数负下标 → DEFINITE（原来 SUSPECT，有意的）。上游用例集 `subobject_overflow` 30 → 24：少掉的行都是重复读写同一元素、按 A-44 并进覆盖它的那条，
  同一访问的上下界两条只报一条（SPEC-0036 `duplicate_access`，日志里都在）；compiler-rt 判定键 1370 → 1370。
* 第 4 步：lit `subobject-join.test`（`c ? st.aucBuf : (UINT8 *)&st` 之后 `p[20]` → SUSPECT、`p[15]` 不报；`c ? st.aucBuf : st.aucPad` 的两个窗口都留下，
  `Dump=state` 的 `{sub 16@0}{sub 12@0}`）；单测 `Interp.SubWindowKeptAcrossJoin`。compiler-rt、上游用例集不变。

## 6. 调试

`Dump=summary`（字段值的 `sub`）、`Dump=apply`（行尾 `[sub]`）。

## 7. 对主规格的修订

无（沿用 SPEC-0104 的 `sub` 与内层变体）。

## 8. 公司侧验证

随 SPEC-0104 / 0105 一起看 `subobject_overflow` 的条数。

## 9. 待定 / 以后

见 §2.2。
