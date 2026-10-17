# SPEC-0104 多维数组每一维、成员数组都查内层界：越出内层、没越出外层记 SUSPECT `subobject_overflow`（用户决定 D22）

状态：第 1 步（FIR `subobj`）、第 2 步（函数内检查）、第 3a 步（内层 Req 往调用方传）、第 3b 步（带窗口的实参）已实现
来源：用户 2026-10-17 决定（HANDOFF D22）："二维数组每个维度越界了就要报出来；成员数组 / 二维数组访问时多查一道内层界，越出内层、没越出外层的记 SUSPECT 带新原因。"

```c
UINT8 a[2][4];        a[0][5] = 0;            /* 越过第 0 行，写到 a[1][1]：原来 SAFE（只查整个 a） */
CTX_S st;             st.aucBuf[20] = 0;      /* aucBuf[16]，st 有 32 字节：原来 SAFE */
ENT_S astEnt[4];      astEnt[0].aucName[8];   /* 越过成员，还在 astEnt[1] 里：原来 SAFE */
```

降级时 `a[i][j]` 变成 `a + 4·i + j`，`st.aucBuf[k]` 变成 `&st + 4 + k`，内层子对象的大小丢了，只剩整个对象的检查。
（形参所指结构体的成员数组 `p->aucBuf` 本来就单独当基址查成员界，SPEC-0022，不变。）

## 2. 设计

### 2.1 FIR：`subobj(指针, 大小)`（第 1 步）

* 新表达式 `subobj`（D-13 追加在 `unk` 之后；JSON `["subobj", 指针, 大小]`，文本 `subobj(…, N)`）：这个指针指向一个 N 字节的数组子对象的起点。
* 降级：`CK_ArrayToPointerDecay` 的操作数（去掉括号）是**成员数组**（`MemberExpr`）或**多维数组的一行**（`ArraySubscriptExpr`），且大小已知、大于 0 →
  地址外面包 `subobj`。整个对象（局部 / 全局数组本身）、柔性 / 零长数组不包。
* 第 1 步里 Interp 把 `subobj` 当恒等（结果不变）。

### 2.2 函数内检查（第 2 步）

* **内层窗口**：指针值（`PtrVal`）带至多 4 个窗口 `(大小 N, 相对窗口起点的偏移)`。
  * `subobj(P, N)` 处推入一个（偏移 0），超过 4 个丢最外层的。形参所指结构体的成员数组本身就是基址（`ParamField`、偏移 0、大小同为 N）时不推：
    基址的界就是它，SPEC-0022 已经在查，而且能判 DEFINITE / POSSIBLE。
  * `padd` / `fld` 的偏移同时加到每个窗口上（`fld` 换成成员基址时窗口照带）。
  * 指针比较收窄（SPEC-0081）：外层偏移与窗口偏移相差一个定值（上下界的差相同）时，窗口偏移同步收窄；否则不动（只会更宽）。
  * 汇合 / 加宽：两边窗口个数与大小都相同 → 逐个汇合 / 加宽；否则丢掉窗口（照旧只查外层，与原来一样）。
* **访问**（读、写、模型的内存操作，`Access`）：每个窗口查 `0 ≤ 偏移下界` 与 `偏移上界 + 字节数 ≤ N`（用守卫与类型范围）；证明不了 →
  账本 `subobject_overflow`（SUSPECT），按（原因, 位置）去重。窗口偏移没有上界（下界）而外层偏移有 → 也记（可靠）；两边都没有 → 外层自己记账，不重复记。
  0 字节的访问不查。
* 外层（整个对象）的检查照旧：越出外层时外层照常判 DEFINITE / POSSIBLE / 记账，同一行另有一条 `subobject_overflow`（日志里都在，不合并）。
* 带窗口的指针作实参交给被调者：本步不查（第 3 步）。

### 2.3 往调用方传（第 3 步）

**3a 内层 Req**：第 2 步里"下标是形参"的（`g_aucSched[ulSlot][ulRb]`、`vals.a[index]`）在函数里证明不了，记账就到此为止，调用方给的下标在界内也报。改为：
* `CheckSubs` 的上界证明不了、且 `窗口偏移上界 + 字节数 − N` 含接口符号（形参、`ext(…)`、全局…）→ 记一条 **内层 Req**（`dir: "sub"`）：
  `have` = N（常数）、`obj` = 外层对象、`end` = 窗口偏移 + 字节数、`start` = 窗口偏移下界；走原来的 `Record`（挑守卫、合并、往上传）。
  不含接口符号（常数、局部的区间）→ 结论在本函数里就定了，照第 2 步记账。下界（负下标）证明不了、窗口没有界 → 照第 2 步记账。
* 调用点代换（`apply::Classify`，同 `replay.py` 的 `classify`）：内层 Req 按原规则判，结局 **L1 / L2 一律改判 `open`、原因 `subobject_overflow`**
  （越出内层、没越出外层只记 SUSPECT，D22）；其余 `open` 的原因也换成 `subobject_overflow`；`safe` / `up` 不变。不查外层对象（`ParentCall` 只对正向 Req）。
* 入口推不下去的内层 Req：入口账本原因 `subobject_overflow`。
* 告警 / 判定文字：`Cannot prove the <读写> stays within the array member or row (N bytes) of '<对象>': up to byte E of it may be accessed`；
  安全：`Proven within bounds: up to byte E of the array member or row (N bytes) of '<对象>'`。指纹的对象写 `<对象>#sub`。
* 摘要 JSON 的 `dir` 取 `back` / `sub`（`summary.schema.json`、`core/Summary.cpp`）。

**3b 带窗口的实参**：`Fill(st.aucBuf, 20)` 这样把带窗口的指针交给被调者，被调者的 Req `end ≤ ext(p0)` 在调用点换成外层剩余（28），内层（16）不查。
（`memcpy_s` 等模型函数在调用方自己的 `Access` 里查，第 2 步已覆盖。）
* 调用点记指针实参（D-25）：指针带窗口、最内层窗口不到基址末尾（不是 `tail`）、窗口偏移有上界 → 另记 `sub` = 子对象大小 − 窗口偏移上界（最小的内层剩余）。
* 调用点代换（`apply::ClassifyAll`，同 `replay.py` 的 `classify_all`）：被调者的正向 Req、对象是 `*pK`、`have` 正好是 `ext(pK)`、第 K 个实参带 `sub` →
  外层（原来那条，含 SPEC-0035 的外层对象）的结局是 `safe` / `up` 时，另用"第 K 个实参只剩 `sub`"的调用点（去掉 `alias` / `parent` / `back` / `off_*`）、
  把这条 Req 当内层 Req（`dir: "sub"`）再判一次：`safe` → 不加；`up` → 另往上传一条内层 Req；其余 → `open subobject_overflow`。
  外层已经报 / 记账的不再加内层这条（同 SPEC-0035"成员已经报 → 只有它"）。
* 结局带 `dir`（`apply::Outcome::dir`，replay 里结局的 `dir` 键）：内层这条是 `sub`，往上传与判定日志用它。
* 不覆盖：被调者的反向 Req（负偏移越到成员前面，仍只查外层）；结构体字段里存的指针（`fields`）不记 `sub`。

## 3. 对召回与误报的影响

**召回**：新增一类报出（SUSPECT），原来判 SAFE 的内层越界不再静默；外层的检查不变，原来的报出一条不少。
**噪声**：把二维数组当一维用（`a[0][i]`，`i` 跑过一行）、按成员地址拷整个结构体的写法会多出 SUSPECT。

第 2 步实测：
* compiler-rt builtins（177 个文件）：几乎没有成员数组 / 二维数组（FIR 里只有 1 处 `subobj`），判定 1631 → 1631，没有报出消失。
* 上游 `clang/test/Analysis/*.c`：213 个正常 / 21 个编译错误 / 0 崩溃 / 0 超时（与原来相同）；`subobject_overflow` 出现在 3 个文件、43 条：
  `pr22954.c` 19 条（`(int *)a3.s1` 读 `dest[1..3]`、`memcpy(i18.s1, input, 100)` 等真越出成员，`J0.s1[i]`、`m27.s3[i]` 下标是形参）、
  `initialization.c` 3 条（`glob_arr2[2][-2]` 等真越出一行）、`array-struct-region.c` 21 条（全是 `vals.a[index]`，`index` 是形参）。
  **下标是形参的，第 2 步在函数里证明不了、按规则记 SUSPECT；第 3 步改成 Req 往调用方传，调用方给的下标在界内就不再报。**
* `run.py csa`：回退 0；LV7-02（二维数组行级越界）的 `Sched_Mark` 多一条 SUSPECT `subobject_overflow`（在被调者里，用例头的 `later=L2` 是旧计划的逐维口径，
  按 D22 记 SUSPECT；第 3 步之后应落到入口 `Sched_OnSlot_TP`、TN 不报）。

第 3a 步实测：
* `run.py csa`：回退 0；LV7-02 的 SUSPECT 从被调者 `Sched_Mark` 挪到 TP 链上的 `Sched_Alloc`（`Sched_MarkRbs(ulSlot, 8)` 代进来、行内末端 9 > 8 的地方），
  TN 链（`r < ulRbNum`）证明了安全、不再报。
* 上游 `clang/test/Analysis/*.c`：213 / 0 崩溃 / 0 超时；`subobject_overflow` 43 → 30 条。少掉的 13 行**都是被同一对象上更大的访问覆盖的**
  （175 行读 `vals.a[index].x` 与 173 行写同一地址；`J0.s1[i].s1[0]`、`[1]` 被 352 行 `memcpy(J0.s1[i].s1, input, 2)` 覆盖），
  按 A-44 覆盖合并（SPEC-0014 §2.4，外层 Req 一直如此）并进代表那条，代表那条照样报 SUSPECT——不是漏报。
* compiler-rt：判定键 1370 → 1370，没有报出消失。

第 3b 步实测：`run.py csa`、上游用例集、compiler-rt 与 3a 相同（这些代码里几乎没有把成员数组 / 一行交给自定义函数的写法，`memcpy_s` 等模型函数第 2 步已查）；
效果见 lit `subobject-arg.test`。**公司实测项**：业务子目录上 `subobject_overflow` 的条数，抽查是真越出成员 / 行，还是"把二维数组当一维用"之类的有意写法。

## 4. 实现位置

第 1 步：`core/Fir.{h,cpp}`（`ExprOp::SubObj`、`Expr::SubObj`）、`core/FirJson.cpp`、`core/FirText.cpp`、`lower/Lowering.cpp`、`core/Interp.cpp`（恒等）、06- D-13。

第 3b 步：`core/Summary.{h,cpp}`、`summary.schema.json`（`PtrArg::sub`）；`core/Interp.cpp`（调用点记 `sub`）；`core/Apply.{h,cpp}`（`Outcome::dir`、`SubCall`、`ClassifyAll`）；
`core/Solver.cpp`（往上传用结局的 `dir`）；`core/Engine.cpp`（判定日志与 `Dump=apply` 用结局的 `dir`）；`replay.py`（`sub_call`、`classify_all`、`derive`）；`tools/golden_regen.py`。

第 3a 步：`core/Interp.cpp`（`CheckSubs` 记 `dir: "sub"` 的 `RawAccess` → `Record`）；`core/Apply.cpp`（`Classify` = `ClassifyPlain` + 内层改判）；
`core/Solver.cpp`（入口账本原因）；`core/Engine.cpp`（`SuspectMessage` / `SafeMessage` 的内层文字）；`core/Alert.cpp`（负偏移文字只认 `back`）；
`core/Summary.cpp`、`summary.schema.json`（`dir` 取 `back` / `sub`）；`replay.py`（`classify` = `classify_plain` + 改判、入口原因、`alert_msg`）；`tools/golden_regen.py`（入口原因）。

第 2 步：`core/Interp.h`（`SubWindow`、`PtrVal::subs`）；`core/Interp.cpp`（`Eval` 的 `SubObj` / `PAdd` / `Fld`、`JoinValue`、`WidenValue`、`NarrowPtr`、
`Access` → `CheckSubs`、`ShowState`）；原因 `subobject_overflow` 登记在 `core/Summary.cpp`、`summary.schema.json`、`replay.py`、06- D-27；
告警文字 `core/Engine.cpp` 的 `LedgerMessage`。

## 5. 测试

* 第 1 步：lit `fir-subobj.test`（`Dump=fir`：`a[0][j]`、`st.aucBuf[k]`、`astEnt[0].aucName[k]` 的地址带 `subobj(…, 4)` / `subobj(…, 16)` / `subobj(…, 8)`；整个数组 `a[k]` 不带）；
  单测 `FirJson` / `FirText` 往返带 `subobj`；`run.py csa` 与基线相同（只是 FIR 多了一层）。
* 第 2 步：lit `subobject-overflow.test`（`a[0][5]`、`st.aucBuf[20]`、`astEnt[0].aucName[8]`、循环 `a[0][i]`（`i < 8`）、`memset_s(st.aucBuf, …, sizeof(st))`、
  `st.aucBuf[-1]`、指针步进 → SUSPECT `subobject_overflow`；对照：`a[1][3]`、`st.aucBuf[15]`、`memset_s(…, sizeof(st.aucBuf))`、`for (p = st.aucBuf; p < st.aucBuf + 16; p++)`
  → 不记；越出外层的 `a[2][0]` → 外层 DEFINITE；`Dump=state` 的窗口 `{sub 4@…}`）；单测 `Interp.SubObjectWindow`；`run.py csa`：回退 0，新增的报出逐条列出。
* 第 3a 步：lit `subobject-req.test`（`Dump=summary` 的内层 Req；`Mark(1, 8)` → SUSPECT、`Mark(1, 7)` → SAFE、`Mark(4, 0)` 外层 DEFINITE 内层 SAFE、
  `pst->stIn.ausY[i]` 同理、入口自己的形参下标 → 入口账本 `subobject_overflow`；没有 POSSIBLE）；单测 `Apply.SubObjectReqIsSuspectOnly`。
* 第 3b 步：lit `subobject-arg.test`（`Dump=summary` 的实参 `sub`；`Fill(st.aucBuf, 20)` → SUSPECT、`Fill(st.aucBuf, 16)` → SAFE、`Fill(st.aucBuf, 40)` 外层 DEFINITE、
  不加内层；`Fill(a[1], 5)`（二维数组的一行）→ SUSPECT；经中间函数 `Mid(p, n) { Fill(p, n); }`：`Mid(st.aucBuf, 20)` → SUSPECT、`Mid(st.aucBuf, 16)` → SAFE）；
  单测 `Apply.SubObjectArg`。

## 6. 调试

`Dump=fir`（第 1 步）；`Dump=state`：指针值后面带窗口 `{sub N@偏移}`，访问证明不了时一行 `subobject <位置> <R|W> off <窗口偏移> bytes <字节数> of N`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-13 表达式 | …、`psub`、`unk` | 另：`["subobj", P, size]`（数组子对象的起点，SPEC-0104；已在 06- D-13 注明） |
| D-23 Req 的 `dir` | `back`（反向 Req） | 另：`sub`（内层 Req：`have` 是子对象大小、`end` 从子对象起点算，违反只判 SUSPECT `subobject_overflow`；已在 06- D-23 注明） |
| D-27 账本原因 | … | 另：`subobject_overflow`（已在 06- D-27 注明） |

## 8. 公司侧验证

业务子目录上 `subobject_overflow` 的条数与抽查（真越出成员 / 行，还是"把二维数组当一维用"、"按成员地址清整个结构体"之类的有意写法）。
有意写法多时，可加选项按写法关掉（例如结构体最后一个成员、`memset_s(&st.first, …, sizeof(st))`），等用户按实测决定。

## 9. 待定 / 以后

无。
