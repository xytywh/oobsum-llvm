# SPEC-0103 选项 `CharAlias`：`char *` / `void *` 的写当成可能改了任何外部内存（用户 2026-10-17 决定 D20；默认先关，等确认）

状态：已实现
来源：SPEC-0040 §2.2 / SPEC-0091 保留的取舍（"`char` 写不影响别的类型"）可能错判 SAFE：

```c
static void F(UINT8 *p, CTX_S *pst) { UINT8 a[4]; pst->ulLen = 2; p[0] = 200; Use(a, pst->ulLen); }
void T(void) { CTX_S st; F((UINT8 *)&st, &st); }     /* p 就是 pst：ulLen 已被改，原来仍按 2 判 SAFE */
```

用户决定按可靠的做，加选项控制，公司代码上实测噪声后再定默认（HANDOFF D20）。

## 2. 设计

* 选项 `rb.enhance.ipa.OobSummary:CharAlias`（布尔，**默认 `false`**，见 §3），经 `engine::RunInline` / `RunSolve` 传到 `interp::Options::charAlias`。
* 打开时，经外部内存（形参所指、指针字段所指、全局；SPEC-0091 的 `ViewOf`）里**类型键是 `char` / `void` 的**写（store、偏移不定的写、交给被调者、被调者的 `mods`）：
  * 状态记 `externAll`：所有外部内存的格失效（写的那个对象本身之后照常写入新值）；
  * 之后读形参所指的字段、全局标量不再是入口值（`Changed` 对外部基址都为真）；调用点 `gvals` 记 `g:*`（被调者的全局符号改名）；
  * 汇合取或。
* 关闭时照旧（SPEC-0091：`char` 写只影响 `char` / `void` 的指针视角与同类型对象）。

## 3. 对召回与误报的影响

**召回**：打开时可靠（修掉上面这种错误 SAFE）；compiler-rt 压测打开后没有一处报出消失（两处报出位置从调用点挪到被调者的访问点），DEFINITE / POSSIBLE 没有降级。
**噪声（实测，比预想的大）**：不只是编码函数"先写输出缓冲区、再读输入结构体的长度"——**循环界每轮从结构体读、循环体经 char 写**
（`for (i = 0; i < pst->ulLen; i++) pucBuf[i] = 0;`）的界在第一轮之后就丢掉，真越界从调用点的 POSSIBLE 变成被调者访问点的 SUSPECT。
本仓库 89 个 lit 里有 9 个功能用例的结果因此变了（`field-effect`、`cond-write`、`global-effect`、`range-field`、`scalar-pointee` 等）；
`run.py csa` 的 67 个用例不受影响；compiler-rt：SUSPECT 1631 → 1646 条。所以**默认先关**，等用户按公司实测（`CharAlias=true` / `false` 各跑一遍）确认默认值；
另提议"假设不别名、在调用点核对"（§9），可靠且不丢这类循环的界。

## 4. 实现位置

`Checkers.td`（`rb.enhance.*` 块）、`OobSummaryChecker.cpp`（读选项）、`core/Engine.{h,cpp}`、`core/Interp.{h,cpp}`（`Options`、`State::externAll`、`AliasWritten`、
`Changed`、`GlobalsAt`、汇合）。

## 5. 测试

* lit `char-alias.test`：打开 → T 不再 SAFE（SUSPECT）、编码函数与循环界的写法都降成 SUSPECT、`Dump=state` 行尾 `extern-written`；关（默认）→ 照旧，`Dump=options` 里 `CharAlias=false`。
* 单测 `Interp.CharAliasOption`（打开 / 关掉两种结果）。
* `run.py csa`（默认关）：与基线相同；lit 90/90。

## 6. 调试

`Dump=state`（`extern-written`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0091 §2 | `char` / `void` 的写不影响别的类型 | 选项 `CharAlias`（默认打开）：影响所有外部内存；关掉时照旧 |
| 06- §4 选项（I-1x） | — | 新选项 `CharAlias` |

## 8. 公司侧验证

业务子目录上 `CharAlias=true` / `false` 各跑一遍，带回 SUSPECT 条数（按原因）的差。

## 9. 待定 / 以后

* 默认值按公司实测再定（等用户确认）。
* 提议（等用户确认）："假设不别名、在调用点核对"：被调者里 char 写照旧不让别的形参失效，但在摘要里记下"假设 pK 与 pJ（或全局 g）不重叠"；
  调用点上实参是两个不同的对象（不同的局部 / 全局 / 堆）→ 假设成立，结果照用；实参是调用方自己的形参 → 假设换成调用方的、往上传；
  核对不了 → 这个被调者在这里推上来的结果降成 SUSPECT（新原因 `alias_unverified`）。这样既可靠，又不丢 `for (i < pst->ulLen) pucBuf[i]` 这类循环的界。
