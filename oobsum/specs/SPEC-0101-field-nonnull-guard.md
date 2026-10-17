# SPEC-0101 指针字段判空产生守卫：`nn:p<k>-><f>`（上下文结构体里的可选缓冲区置 `NULL`）

状态：已实现
来源：SPEC-0099 §9 的待定项：

```c
typedef struct { UINT32 ulLen; UINT8 *pucBuf; } CTX_S;
static void Clr(CTX_S *pst) { if (pst->pucBuf != NULL) { (void)memset_s(pst->pucBuf, 100, 0, 100); } }
void F1(void) { CTX_S st; st.pucBuf = NULL; Clr(&st); }      /* SUSPECT untracked_pointer——不会访问 */
```

## 2. 设计

* 接口符号 `nn:p<k>-><f>`：第 k 个形参所指结构体的指针字段 f（入口时的值）不是空指针，0 / 1。
* 被调者（`AssumeNull`）：一边是基址为"指针字段所指"（`*p<k>-><f>`）的指针、另一边是 0 → 同 SPEC-0099 的守卫。
  `Env`：守卫里出现的 `nn:` 符号都给类型范围 `[0, 1]`。
* 调用点（`BuildMap` / `build_map`）：实参记录的字段 f 是常数 → 0 / 1；是指向局部 / 全局 / 字符串的指针 → 1；是调用方形参所指 `*p<j>` → `nn:p<j>`；其余不换。
  实参是调用方自己的形参（`alias`）时，`nn:p<k>-><f>` 按别名改写成调用方的 `nn:p<j>-><f>`（`Substitute` / `subst` 的别名认 `nn:` 前缀）。
* `CallerEnv` / `Env`：调用方摘要 `fields` 表里的指针字段给 `nn:` 的类型范围。

## 3. 对召回与误报的影响

**召回**：可靠——只在字段确实记成常数 0 时守卫为假。**误报**：可选缓冲区置 `NULL` 的调用方不再报（F1 → SAFE，原来 SUSPECT）。

## 4. 实现位置

`core/Interp.cpp`（`AssumeNull`、`Env`）、`core/Apply.cpp`（`BuildMap`、`CallerEnv`）、`core/LinExpr.cpp`（`Substitute` 的别名）、`replay.py`（同步，SPEC-0100 的约定）。

## 5. 测试

* lit `field-nonnull.test`：F1 → SAFE `guard_false`；`pucBuf = a`（4 字节）→ DEFINITE；调用方把自己的形参传下去 → 守卫按别名换成调用方的 `nn:p0->pucBuf`，再上一层传 `NULL` → SAFE。
* 单测 `LinExpr.SubstituteNnAlias`；`replay-sync.test` 加一条字段的情形。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变（逐访问对比）。

## 6. 调试

`Dump=summary`（守卫 `nn:p0->pucBuf >= 1`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §1 接口符号（SPEC-0099） | `nn:p<k>` | 另：`nn:p<k>-><f>`（指针字段不是空指针） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
