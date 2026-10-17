# SPEC-0096 Req 带上全部守卫（含只关于别的形参的路径条件）：调用方让条件为假时访问走不到（修 DEFINITE 误报）

状态：已实现
来源：找"DEFINITE 判过头"的探针：

```c
static void P(UINT8 *p, UINT32 i, UINT32 c) { if (c) { p[i] = 0; } }
void D1(void) { UINT8 a[4]; P(a, 10, 0); }                         /* DEFINITE——c = 0，访问根本不执行 */
static void Q(UINT8 *p, UINT32 i, UINT32 c) { if (c == 5) { p[i] = 0; } }
void D4(void) { UINT8 a[4]; Q(a, 10, 4); }                          /* 同上 */
```

A-44 / SPEC-0016 §2.2：生成 Req 时只带"与 `have` / `end` / `start` 有公共符号的"守卫。`c >= 1`、`c == 5` 只关于别的形参，被丢掉，
调用点上就不知道这次访问要在 `c` 满足条件时才执行。模式开关、可选功能（`if (bFlag) memcpy_s(…)`）这类写法在业务代码里很常见，
传 `FALSE` 的调用方会被报 DEFINITE。

## 2. 设计

* 生成 Req 时带上当前**全部**守卫（与 `have` / `end` / `start` 有公共符号的排在前面；守卫只含接口符号，至多 8 个，A-14）。`forced`（securec 段）照旧补上。
* 调用点（`apply::Classify` ①）照旧：代换后常数为假 → SAFE `guard_false`；常数为真 → 丢掉；还是符号 → 照旧参与判断（往上传）。

## 3. 对召回与误报的影响

**召回**：可靠——只在守卫代换成常数且为假（这条路径在这次调用里走不到）时判 SAFE。compiler-rt 压测：原来报出的 8 处变成 SAFE `guard_false`，
逐条核对都是 `step()` 里只在 `ch` 是字符时才执行的读、调用点传的是 `NOTHING`，确实走不到；其余报出的都还在。
**误报**：传了让条件为假的值的调用方不再报 DEFINITE。**噪声**：守卫集合更细，覆盖合并少一些（compiler-rt 1564 → 1628 条判定，多的主要是 SAFE；
LV10-01 的入口账本从 2 条变 3 条：末端 8 的那次访问多一个路径条件，不再被末端 12 的覆盖）。

## 4. 实现位置

`core/Interp.cpp`（`Record`）。

## 5. 测试

* lit `path-guard.test`：`if (c)` / `if (c == 5)` / `if (c != 0)` 下的访问，调用方传让条件为假的值 → SAFE `guard_false`；传让条件为真的值 → DEFINITE；
  对照：`if (q != NULL)`（指针，没有守卫）照旧 DEFINITE。
* 单测 `Interp.UnrelatedGuardKept`；`Interp.MemcpySecurec`、lit `models.c` 的源读取多一条段 A 的守卫 `p1 <= 128`（有意更新）。
* `run.py csa`：回退 0（LV10-01 账本条数变化，基线有意更新）；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（Req 的 `guard`）、`Dump=apply`（`guard_false`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-44 / SPEC-0016 §2.2 | 带上与 `have` / `end` 有公共符号的守卫原子 | 带上全部守卫（相关的在前，至多 8 个） |

## 8. 公司侧验证

可选：业务子目录上看 `guard_false` 的条数与 SUSPECT 条数的变化。

## 9. 待定 / 以后

* 指针判空的路径条件（`if (q != NULL)`）：引擎不跟踪空指针，传 `NULL` 的调用方仍会被报。
