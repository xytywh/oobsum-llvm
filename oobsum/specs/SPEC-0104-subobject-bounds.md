# SPEC-0104 多维数组每一维、成员数组都查内层界：越出内层、没越出外层记 SUSPECT `subobject_overflow`（用户决定 D22）

状态：第 1 步（FIR `subobj`）、第 2 步（函数内检查）已实现；第 3 步（经实参传给被调者）见 §2.3
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

### 2.3 经实参传给被调者（第 3 步）

* 调用点记录指针实参时，带内层窗口的另记内层的剩余；调用点代换时内层不满足、外层满足 → SUSPECT `subobject_overflow`。

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

## 4. 实现位置

第 1 步：`core/Fir.{h,cpp}`（`ExprOp::SubObj`、`Expr::SubObj`）、`core/FirJson.cpp`、`core/FirText.cpp`、`lower/Lowering.cpp`、`core/Interp.cpp`（恒等）、06- D-13。

第 2 步：`core/Interp.h`（`SubWindow`、`PtrVal::subs`）；`core/Interp.cpp`（`Eval` 的 `SubObj` / `PAdd` / `Fld`、`JoinValue`、`WidenValue`、`NarrowPtr`、
`Access` → `CheckSubs`、`ShowState`）；原因 `subobject_overflow` 登记在 `core/Summary.cpp`、`summary.schema.json`、`replay.py`、06- D-27；
告警文字 `core/Engine.cpp` 的 `LedgerMessage`。

## 5. 测试

* 第 1 步：lit `fir-subobj.test`（`Dump=fir`：`a[0][j]`、`st.aucBuf[k]`、`astEnt[0].aucName[k]` 的地址带 `subobj(…, 4)` / `subobj(…, 16)` / `subobj(…, 8)`；整个数组 `a[k]` 不带）；
  单测 `FirJson` / `FirText` 往返带 `subobj`；`run.py csa` 与基线相同（只是 FIR 多了一层）。
* 第 2 步：lit `subobject-overflow.test`（`a[0][5]`、`st.aucBuf[20]`、`astEnt[0].aucName[8]`、循环 `a[0][i]`（`i < 8`）、`memset_s(st.aucBuf, …, sizeof(st))`、
  `st.aucBuf[-1]`、指针步进 → SUSPECT `subobject_overflow`；对照：`a[1][3]`、`st.aucBuf[15]`、`memset_s(…, sizeof(st.aucBuf))`、`for (p = st.aucBuf; p < st.aucBuf + 16; p++)`
  → 不记；越出外层的 `a[2][0]` → 外层 DEFINITE；`Dump=state` 的窗口 `{sub 4@…}`）；单测 `Interp.SubObjectWindow`；`run.py csa`：回退 0，新增的报出逐条列出。

## 6. 调试

`Dump=fir`（第 1 步）；`Dump=state`：指针值后面带窗口 `{sub N@偏移}`，访问证明不了时一行 `subobject <位置> <R|W> off <窗口偏移> bytes <字节数> of N`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-13 表达式 | …、`psub`、`unk` | 另：`["subobj", P, size]`（数组子对象的起点，SPEC-0104；已在 06- D-13 注明） |

## 8. 公司侧验证

第 2 步之后：业务子目录上 `subobject_overflow` 的条数。

## 9. 待定 / 以后

无。
