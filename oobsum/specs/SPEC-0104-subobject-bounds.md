# SPEC-0104 多维数组每一维、成员数组都查内层界：越出内层、没越出外层记 SUSPECT `subobject_overflow`（用户决定 D22）

状态：第 1 步（FIR `subobj`）已实现；第 2 步（函数内检查）、第 3 步（经实参传给被调者）见 §2.2 / §2.3
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

* 指针值带"内层窗口"：`subobj` 处相对偏移 0、大小 N；之后加偏移同时加到相对偏移上；汇合时两边窗口大小相同才保留。
* 访问：相对偏移的上界 + 字节数 > N，或下界 < 0（确知）→ 记账 `subobject_overflow`（SUSPECT）。整个对象的检查照旧（越出外层照常判 L1 / L2 / 记账）。

### 2.3 经实参传给被调者（第 3 步）

* 调用点记录指针实参时，带内层窗口的另记内层的剩余；调用点代换时内层不满足、外层满足 → SUSPECT `subobject_overflow`。

## 3. 对召回与误报的影响

**召回**：新增一类报出（SUSPECT），原来判 SAFE 的内层越界不再静默。**噪声**：把二维数组当一维用（`a[0][i]`，`i` 跑过一行）、按成员地址拷整个结构体的写法会多出 SUSPECT。

## 4. 实现位置

第 1 步：`core/Fir.{h,cpp}`（`ExprOp::SubObj`、`Expr::SubObj`）、`core/FirJson.cpp`、`core/FirText.cpp`、`lower/Lowering.cpp`、`core/Interp.cpp`（恒等）、06- D-13。

## 5. 测试

* 第 1 步：lit `fir-subobj.test`（`Dump=fir`：`a[0][j]`、`st.aucBuf[k]`、`astEnt[0].aucName[k]` 的地址带 `subobj(…, 4)` / `subobj(…, 16)` / `subobj(…, 8)`；整个数组 `a[k]` 不带）；
  单测 `FirJson` / `FirText` 往返带 `subobj`；`run.py csa` 与基线相同（只是 FIR 多了一层）。

## 6. 调试

`Dump=fir`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-13 表达式 | …、`psub`、`unk` | 另：`["subobj", P, size]`（数组子对象的起点，SPEC-0104；已在 06- D-13 注明） |

## 8. 公司侧验证

第 2 步之后：业务子目录上 `subobject_overflow` 的条数。

## 9. 待定 / 以后

无。
