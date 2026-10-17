# SPEC-0099 指针形参判空产生守卫：接口符号 `nn:p<k>`（可选出参传 `NULL` 时访问走不到）

状态：已实现
来源：找"级别判过头"的探针（SPEC-0096 §9 的待定项）：

```c
static void Get(UINT8 *pOut, UINT32 n) { if (pOut != NULL) { (void)memset_s(pOut, n, 0, n); } }
void O1(void) { Get(NULL, 100); }                                   /* SUSPECT untracked_pointer——不会访问 */
static void Opt(UINT8 *p, UINT32 i, UINT8 *q) { if (q != NULL) { p[i] = 0; } }
void O3(void) { UINT8 a[4]; Opt(a, 10, NULL); }                     /* DEFINITE——不会访问 */
```

引擎不跟踪空指针：`q != NULL` 不产生信息，访问在摘要里成了无条件的。可选出参（`Get(pIn, NULL)`）在业务代码里很常见。

## 2. 设计

* 接口符号 `nn:p<k>`：第 k 个形参不是空指针（取值 0 / 1；类型范围 `[0, 1]`）。
* 被调者：`AssumeCmp` 里一边是基址为形参所指对象（`*p<k>`，任意偏移）的指针、另一边是常数 0（或空指针）：`!=` → 守卫 `nn:p<k> >= 1`，`==` → `nn:p<k> <= 0`
  （`if (p)` / `if (!p)` 同样）。已有守卫与它矛盾 → 这条边不可达。
* 调用点（`apply::BuildMap`）：`nn:p<k>` 换成第 k 个实参的说法——整数常数 0 → 0、非 0 常数 → 1；指针实参的对象是局部 / 全局 / 字符串 → 1；
  是调用方形参所指 `*p<j>` → 调用方的 `nn:p<j>`；其余（堆、指针字段所指、不跟踪的指针）→ 不换（改名，判不了）。
* `nn:p<k>` 算接口符号：没映射时改名（SPEC-0063 的 poison）、往上传时保留（`IsIface`）。

## 3. 对召回与误报的影响

**召回**：可靠——只在实参确实是空指针（常数 0）、守卫为假时访问走不到；堆对象（可能为空）不当非空。
**误报**：可选出参传 `NULL` 的调用方不再报（O1 → SAFE，原来 SUSPECT；O3 → SAFE，原来 DEFINITE）。

## 4. 实现位置

`core/Interp.cpp`（`AssumeCmp`、`Env`）、`core/Apply.cpp`（`BuildMap`、`CallerEnv`、`IsIface`）、`core/LinExpr.cpp`（`IsCalleeIface`）。

## 5. 测试

* lit `nonnull-guard.test`：O1 / O3 → SAFE `guard_false`；传真实缓冲区 → 照旧 DEFINITE；`if (p == NULL) return;` 之后的访问调用方传 `NULL` → SAFE；
  经一层中间函数（调用方自己的形参传下去）→ 守卫 `nn:p<j>` 往上传。
* 单测 `Interp.NonNullGuard`、`Propagate.NonNullMap`（实参 0 → guard_false；局部数组 → L1；调用方自己的形参 → 守卫换成调用方的 `nn:p1` 往上传）。
* `path-guard.test`（SPEC-0096）里"指针判空照旧 DEFINITE"的对照有意更新为 SAFE `guard_false`。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测：原来报出的 3 处变成 `guard_false`，逐条核对都是
  `__udivmodti4(a, b, 0)` 这类可选余数出参传 0（`if (rem) *rem = …`），确实不写；其余报出的都还在。

## 6. 调试

`Dump=summary`（Req 的守卫 `nn:p<k> >= 1`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §1 接口符号 | `p0`、`ext(p0)`、`p0->f`、`g:…` | 另：`nn:p<k>`（形参不是空指针，0 / 1） |
| A-14 守卫 | 整数比较 | 另：形参指针与空指针比较 → `nn:p<k>` 的守卫 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 指针字段判空（`if (pst->pucBuf != NULL)`）、堆对象判空后的非空（`p = malloc(); if (p == NULL) return;` 之后传下去）。
