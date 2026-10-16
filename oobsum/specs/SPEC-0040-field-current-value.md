# SPEC-0040 形参所指字段的当前值：改过就不用入口符号；同类型形参的别名

状态：已实现
来源：探针（`oobsum/specs/SPEC-0039` 之后的一轮）发现三处错误的 SAFE，根因相同：读形参所指结构体的字段时，格里没有就直接给入口符号 `pK->f`，
不看进函数之后有没有改过。06- A-12（经未知基址的写）、A-28（字段的值）、D-20（`pK->f` 是**入口时**的值）；SPEC-0026（`written` / `writtenAll` /
`clobbered` 记录，原来只用于调用点的 `fields`）；SPEC-0001（只有证明了安全才是 SAFE）。

## 1. 要解决什么

```c
static void Set100(S *p) { p->ulLen = 100; }
static void MidA(S *p) { UINT8 auc[16]; Set100(p); Buf_Clear(auc, p->ulLen); }              /* A：被调者改了字段 */
static void MidC(S *p, UINT32 i) { UINT8 auc[16]; ((UINT32 *)p)[i] = 100; Buf_Clear(auc, p->ulLen); }   /* C：偏移不定的写 */
static void MidD(S *a, S *b) { a->ulLen = 4; b->ulLen = 100; Buf_Clear(a->aucBuf, a->ulLen); }          /* D：a、b 可能是同一个 */
```

调用方都传 `ulLen = 4` 的 `st`：A、C 里 `p->ulLen` 读成入口符号 `p0->ulLen`，调用点代换成 4 → SAFE；D 里 `a->ulLen` 命中格 4 → SAFE。实际是 100。

## 2. 设计

### 2.1 字段的当前值（A、C）

`LoadValue` 读形参所指结构体的字段 `pK->f`（偏移 `off`、`size` 字节）：格里没有时，**只有这块内存进函数后没改过**才给入口符号；否则给 `data`
（整数：类型范围、标签 `data`；指针：未知指针 `data`）。"改过"与 SPEC-0026 调用点 `fields` 用的同一个判断（抽成 `Changed`）：

* `clobbered`（经未知指针写过、把未知指针交给了被调者）；
* 基址在 `writtenAll` 里（偏移不定的写、指针交给了被调者）；
* `written` 里有与 `[off, off + size)` 重叠的格。

### 2.2 同类型形参的别名（D）

经形参 `pK`（或它所指结构体里的成员数组）写时，**所指类型相同**的其它形参 `pJ` 也可能被改：清掉 `pJ` 的格、把 `pJ` 记进 `writtenAll`。

取舍（写进规则边界）：

* 只看所指类型完全相同的结构体形参。`UINT8 *` / `void *` 缓冲区的写不当成会改结构体形参（C 的别名规则允许 `char` 指向任何对象，但
  "先写输出缓冲区、再读输入结构体的长度"这种编码函数写法很常见，全当别名会把它们都变成 SUSPECT）；
* 形参与全局之间（调用方传 `&g_x`）不当别名；全局标量的读取原来就不在调用点代换（一直走到入口记账），本步不改。

## 3. 对召回与误报的影响

**召回**：修掉 A、C、D 三种错误的 SAFE（调用点按入口值代换）。**误报 / 噪声**：被调者拿到过指针的结构体，之后读出来的字段当长度用时，
原来按入口值推到调用方，现在是 `data`（类型范围）→ 记账（SUSPECT `type_range_only`）。被调者到底改不改哪些字段（mod-ref）以后做（S8c-2），
做了之后没改的字段又能用入口值。

## 4. 实现位置

`core/Interp.cpp`：`Changed`（与 `ArgFields` 共用）、`LoadValue`、`Store` 的别名失效（`AliasRoots`：形参号 → 所指类型）。

## 5. 测试

* lit `field-current.test`：A、C、D 三个例子 → 调用方不再是 SAFE（SUSPECT）；对照：`Set100` 之前读的字段、只读不写的形参、不同类型的两个形参
  （写 `out->x` 后读 `in->len`）仍按入口值推到调用方判 DEFINITE / SAFE。
* `run.py csa` 无回退；`replay.py`、`engine_check` 视结果（有变化按 SPEC 有意更新）。

## 6. 调试

`Dump=state`（字段读出来的值）、`Dump=summary`（调用点实参 / Req 里是 `pK->f` 还是区间）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-28 | 形参所指结构体的字段读成 `pK->f` | 只有这块内存进函数后没改过才读成 `pK->f`；改过（被调者拿到过指针、偏移不定的写、同类型形参的写）读成 `data` |

## 8. 公司侧验证

不需要。业务子目录的计数（可选）里看 `type_range_only` 是否明显变多。

## 9. 待定 / 以后

* mod-ref（被调者不改的字段在调用后仍用入口值）：SPEC-0045；形参与全局的别名。全局标量在调用后的当前值：SPEC-0041。
