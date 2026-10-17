# SPEC-0106 `CharAlias=verify`：经 char 写外部内存时假设不别名，在调用点核对（用户决定 D25）

状态：已实现（第 1 步：选项、记假设；第 2 步：调用点核对）
来源：SPEC-0103 的两个取值各有缺点——`false` 漏掉真别名（`F((UINT8 *)&st, &st)` 错判 SAFE），`true` 让无别名时的循环界也丢掉
（`for (i < pst->ulLen) pucBuf[i]` 真越界从 POSSIBLE 降成 SUSPECT、安全的也报 SUSPECT）。用户 2026-10-17 决定做第三个取值
（D25：取舍做成选项，`false` / `true` 照留）。

## 1. 语义

被调者里照 `false` 推（不让别的外部内存失效、界不丢），同时记下"假设 X 与 Y 不重叠"；调用点上按实参核对：

| 实参 | 结论 |
|---|---|
| 两个不同的具体对象（局部、全局、堆、字符串） | 成立，被调者的结果照用 |
| 同一个对象 | 不成立 → SUSPECT `alias_unverified`（调用点） |
| 一边是调用方的局部 / 堆，另一边是调用方的形参所指 | 成立（形参进函数时就定了，不可能指向函数里新建的对象） |
| 一边是全局、另一边是调用方的形参所指；两边都是调用方的形参所指（不同的形参） | 换成调用方的假设往上传 |
| 不知道指向哪（未跟踪的指针、整数等） | 不成立 → SUSPECT `alias_unverified` |
| 到入口还没核对掉的 | SUSPECT `alias_unverified`（入口函数声明处） |

`alias_unverified` 只说"这里的别名排除不了，被调者的结果可能不成立"，不改被调者自己的判定。

## 2. 设计

### 2.1 选项（第 1 步）

`CharAlias` 改成字符串选项：`false`（默认）/ `true` / `verify`；别的值报选项错误。`interp::Options::charVerify`。

### 2.2 记假设（第 1 步）

* "根"：外部内存按接口说法记——形参所指（含其成员）`p<k>`、指针字段所指 `p<k>->f`、全局 `g:<名>`。
* `State::charRoots`：本路径上经类型键为 char / void 的外部视角写过的根（`AliasWritten` 里记，之后照 `false` 处理）。汇合取并集。
* 只在 `recording` 那一遍记，记进 `FunctionFacts::assumes`（无序对 `(X, Y)`，`X < Y`）：
  * load 外部内存（基址是形参所指 / 指针字段所指 / 全局）、根 `Y`：对 `charRoots` 里每个 `X ≠ Y` 记 `(X, Y)`。
  * 调用点（含模型函数）：每个指向外部内存的指针实参（根 `Y`）、以及交给被调者的全局值（`GlobalsAt` 里的每个全局 `Y`）：同上。
* **只留起作用的假设**（第 2 步加）：记下了假设的函数再按 `true` 分析一遍，摘要（去掉 `assumes` 与只是类型登记的 `fields`）与 `verify` 的相同
  ——别名改变不了这个函数的任何结果——就把假设丢掉。可靠：两种分析结果一样，有没有别名都对。
  （第 1 步只按"读到 / 交出了别的外部内存"记，compiler-rt 上 329 条 `alias_unverified`，绝大多数不影响任何界。）
* 摘要顶层 `assumes`：`[["p0", "p1"], ["p0", "g:g_stCtx"]]`（没有时不写；`summary.schema.json`、`core/Summary.cpp`）。
* `Dump=state`：行尾 ` char-written:p0,…`；`Dump=summary` 带 `assumes`。

### 2.3 调用点核对（第 2 步）

* `solver::Propagate`：每个函数的假设 = 自己的 `assumes` + 从被调者传上来的。每个调用点、被调者的每条假设，两边各自换成调用方的说法（§1 的表）：
  * 形参根 `p<k>` → 第 k 个实参：指针实参的 `obj`（`local:…` / `g:…` / `heap@…` / `g:__str` 是具体对象；`*p<j>`、`p<j>->m` → 调用方的根 `p<j>`；
    `*p<j>->f` → `p<j>->f`）；不是指针、未跟踪 → 不知道。
  * `p<k>->f` → 第 k 个实参的 `fields["f"]`（指针）的 `obj`；实参是调用方形参 `*p<j>`（`alias`）→ `p<j>->f`；否则不知道。
  * `g:<名>` → 它自己（具体对象）。
* 结论：成立 → 不记；不成立 → `result.aliasOpens`（调用点、被调者、两个根、原因 `same` / `unknown`）；往上传 → 加进调用方的假设。
  入口（没有调用方）：剩下的假设各记一条（原因 `entry`）。
* `engine`：核对不成立的 → SUSPECT `alias_unverified`，**一个调用点（入口）只报一条**（同一个对象的那对优先写，其余写 `(and N more pairs)`），
  位置是调用点（入口的在函数声明处），文字：
  `Cannot prove the accesses in 'F' stay within bounds: they assume '*p' and '*pst' do not overlap, but here both point to 'st'`
  （`unknown`：`…, which is not known here`；入口：`Cannot prove the accesses stay within bounds: they assume '*p' and '*pst' do not overlap, which cannot be checked at an entry function`）。
* `Dump=apply`：`T @ t.c:7 F assume p0&p1 → same (local:st)`。
* 原因 `alias_unverified` 登记在 `core/Summary.cpp`、`summary.schema.json`、`replay.py`、06- D-27。
* `replay.py` 不镜像核对（金标准用默认 `false`，摘要里没有 `assumes`）。

## 3. 对召回与误报的影响

与 `true` 一样不漏真别名（不成立时在调用点报 SUSPECT）；与 `false` 一样不丢无别名时的界（POSSIBLE / SAFE 照旧）。
入口函数上核对不了的假设各报一条 SUSPECT（`true` 下这些函数的界整个丢掉、报得更多）。

实测（三个取值的对照，判定条数 / 其中 SUSPECT）：

| | `false` | `true` | `verify` |
|---|---|---|---|
| compiler-rt builtins（177 个文件） | 1559 / 516 | 1574 / 535 | 1606 / 563（其中 `alias_unverified` 47） |
| 上游 `clang/test/Analysis`：`alias_unverified` | — | — | 3 条（只留起作用的假设之前 16 条） |
| `run.py csa`（67 个用例） | 回退 0 | 回退 0 | 回退 0（与 false 相同） |

compiler-rt 上 `verify` 的 47 条大多在 regex 引擎：`lfast(m, …)` 里经 `m->fresh`、`m->empty` 等指针字段所指的 char 数组写，之后读 `*m`——调用方
`m = &mv`，这些字段的值没记下来，核对不了，按语义报 SUSPECT。三个取值报出的位置都没有"原来报、现在不报"（判定键 1370 → 1397，消失 0）。
**这类"结构体里一堆指针字段"的代码上 `verify` 不一定比 `true` 少报**；公司业务代码（上下文结构体 + 一两个 char 缓冲区）上以实测为准。

## 4. 实现位置

第 1 步：`Checkers.td`（rb.enhance 块）、`OobSummaryChecker.cpp`；`core/Interp.{h,cpp}`（`Options::charVerify`、`State::charRoots`、`RootOf`、`NoteAssume`、
`FunctionFacts::assumes`、`BuildSummary`、`ShowState`）；`core/Summary.{h,cpp}`、`summary.schema.json`；06- I-26。
第 2 步：`core/Solver.{h,cpp}`、`core/Engine.cpp`、原因登记。

## 5. 测试

* 第 1 步：lit `char-alias-verify.test`（`Dump=summary` 的 `assumes`：`F` 有 `["p0","p1"]`、`Fill` 有 `["p0","p1"]`、`Enc` 有 `["p0","p1"]`、
  经全局的 `["p0","g:g_stCtx"]`；`Dump=state` 的 `char-written:p0`；判定与 `false` 相同）；单测 `Interp.CharAliasVerifyAssumes`；`Summary` 往返带 `assumes`。
* 第 2 步：单测 `Apply.CheckAssume`（各种实参组合的结论）、`Interp.CharAliasVerifyDropsIneffective`（读了却没用到 → 假设丢掉）；同一 lit 加判定——`T`（同一个 `st`）→ SUSPECT `alias_unverified`；`F(b, &st)` → 照 `false`；`L_TP` → POSSIBLE（不降级）；
  经中间函数 `Mid` 往上传：`Mid(b, &st)` 不报、`Mid((UINT8 *)&st, &st)` 报；全局：`H(b)` 不报、`H((UINT8 *)&g_stCtx)` 报；入口 `G(p, pst)` 报一条。
  单测 `Propagate.AliasAssumptions`。

## 6. 调试

`Dump=state`（`char-written:`）、`Dump=summary`（`assumes`）、`Dump=apply`（核对结论）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| I-26 `CharAlias` | bool，`false` / `true` | string，`false` / `true` / `verify` |
| D-23 顶层 | … | 加 `assumes`（SPEC-0106） |
| D-27 账本原因 | … | 加 `alias_unverified` |

## 8. 公司侧验证

业务子目录上 `CharAlias=false` / `true` / `verify` 各跑一遍，比 SUSPECT 条数与 `alias_unverified` 的条数，抽查 `alias_unverified` 是否真有别名。

## 9. 待定 / 以后

无。
