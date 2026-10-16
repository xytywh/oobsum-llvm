# SPEC-0064 调用前改过的全局：调用点记录带上全局的当前值，被调者的 `g:K` 不再当成调用方入口的值（错误的 SAFE）

状态：已实现
来源：SPEC-0063 之后按同一思路查代换里的全局符号。被调者 Req 里的 `g:K` 是**进被调者时**全局 K 的值；调用点代换按 06- A-61 写 `g:K ↦ g:K`，即调用方**入口**的值。
调用方在调用之前改过 K 时这就错了。探针：

```c
UINT32 g_ulLen;
static void Fill(UINT8 *p) { for (i = 0; i < g_ulLen; i++) p[i] = 0; }                       /* Req：g:g_ulLen <= ext(p0) */
void Write_TP(void)  { UINT8 a[16]; if (g_ulLen > 16) return; g_ulLen = 100; Fill(a); }        /* 原来：SAFE（guard_entailed）——错 */
void Callee_TP(void) { UINT8 a[16]; if (g_ulLen > 16) return; SetLen(100);  Fill(a); }        /* 原来：SAFE——错 */
```

调用点守卫 `g:g_ulLen <= 16`（入口值）蕴含了被调者的条件，**两处真越界都判成了 SAFE**。SPEC-0041 只管了本函数里**读**全局（调用过可能写全局的函数之后不用入口值），
没管把全局符号**代给被调者**。

## 2. 设计

### 2.1 调用点记录 `gvals`（D-25 加字段）

`{ "g:名": 当前值 }`：调用前已经不是入口值的整数全局标量。

* 本函数写过、格里是确切值 → 那个值（调用方的符号写法，同实参）；
* 可能被改过、值不知道（`Changed`：写过范围值、经不定偏移写、指针交给过被调者、`mods` 里有它）→ `"?"`；
* 调用过可能写任何全局的函数（`State::called`）或内存整个被清（`clobbered`）→ 再加 `"g:*": "?"`。

没被改过的全局不写（照旧 `g:K ↦ g:K`）。

### 2.2 代换

`apply::BuildMap` / `replay.py` 的 `build_map`：`gvals` 里有值的 `g:K ↦` 那个值；`"?"` 的 `g:K ↦ _unmapped:g:K`（SPEC-0063 的非接口符号）；有 `g:*` 时表里没有的
`g:…` 都改名（`Substitution::poisonGlobals`）。于是含它的条件判不出安全、也传不上去：值确切时照常判（`Write_TP`：末端 [1, 100]、界来自循环 → POSSIBLE），不知道时记账。

### 2.2a 原因与文本

末端里有调用点上不知道的全局 → 原因 `global_value`（D-27），其余没界的照旧 `entry_unproven`（C++ `Classify` 与 `replay.py` 同步）。告警文本里去掉内部前缀
`_unmapped:`（`Engine.cpp` 的 `Readable`）："up to byte g:g_ulLen may be accessed"。

### 2.3 Interp 里代入 `ret_int`

`CondResult` 的符号区间（SPEC-0054）含全局符号、而那个全局在调用点已经不是入口值 → 返回值未知（原来只查了形参符号）。

## 3. 对召回与误报的影响

**召回**：修掉"调用前改过全局、被调者按全局判界"的错误 SAFE（业务里"先设全局长度 / 个数，再调处理函数"的写法）。原来能判的不受影响：没被改过的全局照旧代成自己。

## 4. 实现位置

`core/Summary.{h,cpp}`（`Call.gvals`）、`core/Interp.cpp`（`GlobalsAt`、`RecordCall`、`CondResult`）、`core/LinExpr.{h,cpp}`（`poisonGlobals`）、`core/Apply.cpp`（`BuildMap`、
`Classify` 的 `global_value`）、`core/Engine.cpp`（`Readable`）、`replay.py`（`build_map`、`subst`、`classify`）、`summary.schema.json`。

## 5. 测试

* lit `global-at-call.test`：`Write_TP` → POSSIBLE（末端 100）；`Callee_TP` → `SUSPECT:global_value`；`Guard_TN`（没改全局）→ 照旧 SAFE；`Dump=summary` 的 `gvals`
  （`{"g:g_ulLen":"100"}` / `{"g:g_ulLen":"?"}`）。
* 单测 `LinExpr.SubstitutePoison` 加 `poisonGlobals`；`Summary.GvalsRoundTrip`（读写往返、坏表达式报错）。
* 顺带：`Summary.cpp` 的 `OptKey` 临时变量值初始化（`T` 是 `bool` 时 GCC 报 `-Wmaybe-uninitialized`）。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`calls[].gvals`）、`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-61 | `g:K ↦ g:K` | 调用点记录的 `gvals` 里有的按它（确切值 / 不知道）；有 `g:*` 时没写的全局都不知道；都没有才 `g:K ↦ g:K` |
| D-25 | 调用点记录的字段 | 加 `gvals` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 指针全局（`g_pucBuf = aucLocal; Use();`）：被调者经全局指针访问时的对象（D-27 `global_ptr_target`）。
* 全局符号按名字（`g:<名字>`），不同 TU 同名的 `static` 全局会混在一起（与键无关的老问题）。
