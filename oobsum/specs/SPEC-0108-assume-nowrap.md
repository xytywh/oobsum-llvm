# SPEC-0108 选项 `AssumeNoWrap`：检查里的运算按数学整数（不回绕），局部线性关系与访问末端的抵消（A-13 / A-41；用户 2026-10-17 同意）

状态：已实现（第 1 步：选项、局部关系；第 2 步：访问点使用）
来源：LV3-07、LV8-01 一直"级别不同"。两个用例的检查都是 `if (ulOff + len > total) break;`，`ulOff` 被加宽到 `[0, +∞)`，`ulOff + len` 可能回绕；
SPEC-0016 §2.1 定过这种检查可能被绕过、不当守卫不收窄（召回优先）。用户同意做成**默认关**的选项（D25：取舍做成选项）。

## 1. 语义

`AssumeNoWrap=true`：假设程序里参与检查与地址计算的整数运算不回绕（按数学整数）。默认 `false`，与原来完全相同。
打开后可能把"检查本身被回绕绕过"的缺陷判成安全——这正是默认不开的原因；只给"业务代码里这类检查确实不会回绕"时用。

## 2. 设计

### 2.1 局部的符号（第 1 步）

* 整数局部的当前值：确切值就用它（接口符号的线性式）；否则用局部符号 `$l<id>`（它的界是这个局部此刻的区间，没有时取类型范围）。
  指针局部的偏移：确切就用它，否则 `$o<id>`。`SymOf` / `SymPtr` 只走 `+`、`−`、乘常数、取负、类型转换（不回绕的假设下值不变）、`padd`、`subobj`；其余没有符号式。
* `State::rels`：局部线性关系 `L ≤ 0`（含至少一个局部符号，至多 8 条，先进先出）。
  * 来源：打开选项时的每个比较（`AssumeCmp`），两边的符号式之差按比较符号写成原子（`<`、`<=`、`>`、`>=`、`==`；`!=` 不写）；指针两边要是同一个对象，比偏移。
  * 失效：局部被赋值（`SetLocal`、`havoc`）时，含它的关系去掉；收窄、按定义式重算不改变值，关系保留。
  * 汇合 / 加宽：取两边都有的。
* `Dump=state` 行尾 ` rels: …`（局部符号写成 `$名字`）。

### 2.2 访问点（第 2 步）

* `Access`：选项打开、地址有符号偏移、字节数有符号式时，末端 `E = 偏移 + 字节数` 先同类项抵消（A-41：`ulOff + (ulPktLen − ulOff) = ulPktLen`），
  再消去局部符号（`Bound`）：用关系（上界 `E − k·R`、下界 `E + k·R`，`k > 0` 使某个局部符号系数为 0；至多两轮）与局部的区间
  （`AtomBound`：按系数符号取上 / 下界；没有界用类型范围，无符号的下界 0 是确切的事实）。几个候选里取在守卫下能证明最紧的；用了关系的标签 `check`。
  得到的上界比原来的更紧（或原来没有）就用它；最小末端同理（模型的"至多 N 字节"按 1）。
* 负偏移检查（`BackAccess`）：偏移没有下界时，也用符号式求下界。
* 模型访问（`memcpy_s` 等）：基址实参的符号偏移；字节数的式子把实参换成实参的符号式（用到的实参都要有符号式）。
* 比较的"忠实"判断（SPEC-0016 / 0017）在选项打开时不做：检查按数学整数，接口符号的守卫与收窄照常产生；模型实参都当忠实；
  `memcpy_s` 的 destMax 不再分"回绕后照样写"那一段（SPEC-0020 §2.1 的段 B）。

## 3. 对召回与误报的影响

默认不变。打开后：只多证明安全 / 把 SUSPECT 改判 POSSIBLE（LV3-07 TP、LV8-01 TP → POSSIBLE；两个 TN → 不报），代价是"检查被回绕绕过"的缺陷可能漏。

实测（第 2 步）：
* 默认：`run.py csa` 与原来相同（回退 0）、`engine_check` 14/16。
* `AssumeNoWrap=true`：`run.py csa` 通过 108 → 110（LV3-07、LV8-01 的"级别不同" → 通过；LV3-10 TN 由检查证明安全），TN 的 DEFINITE / POSSIBLE 仍是 0；
  `engine_check --config AssumeNoWrap=true` 15/16：LV3-07、LV8-01 与金标准逐条一致，LV2-03 告警相同、上传的 Req 少 4 条（destMax 的回绕段不再有，有意的）。
  `run.py` 标的 2 条"不再报出"都是 A-44 合并：LV8-01 第 30 行的读（末端 `p1`）并进第 34 行（`p1 + 2`，报 POSSIBLE）；LV8-02 第 45 行的读与 `Tlv_SumLen`
  第 31 行的要求完全相同（末端 `p1`、`ext(p0)`），到入口合并成一条照样报。打开选项时 `run.py` 按访问点比，会把这类合并标成回退。
* 上游 `clang/test/Analysis`：打开后 213 正常 / 0 崩溃 / 0 超时（最慢 50 ms）。compiler-rt：SUSPECT 516 → 519，`cmp.py` 标的 6 处"消失"都在 `atomic.c`：
  `memcpy(ptr, desired, size)` 的 `int size` 原来字节数只有类型范围、记账 `type_range_only`（没有对象），打开后字节数是 `p0`，同一行报 SUSPECT `entry_unproven`（对象 `*ptr`）
  ——仍然报出，只是对象 / 原因变了。

## 4. 实现位置

第 1 步：`Checkers.td`（rb.enhance 块）、`OobSummaryChecker.cpp`；`core/Interp.{h,cpp}`（`Options::assumeNoWrap`、`State::rels`、`SymOf`、`SymPtr`、`NoteRel`、
`DropRels`、`ShowState`）；06- §4 I-28。
第 2 步：`core/Interp.cpp`（`SymAccess`、`SymAt`、`Bound`、`AtomBound`、`Access`、`BackAccess`、`ApplyAccessModel`、`AssumeCmp` 的忠实判断）；
`oobsum/tools/engine_check.py`（`--config K=V`）。

## 5. 测试

* 第 1 步：lit `assume-nowrap.test`（`Dump=state` 的 `rels:`：`if (ulOff + ulLen > ulTotal) break;` 之后 `$ulOff + $ulLen - p1 <= 0`；赋值后去掉；默认没有 `rels:`；
  判定与关闭时相同）；单测 `Interp.LocalRelations`。
* 第 2 步：同一 lit 加判定（打开：`Tlv` / `Put` → POSSIBLE、`Tlv_OK` / `Put_OK` 不报、没有 `type_range_only`；默认：照旧记账、没有 POSSIBLE）；
  单测 `Interp.AssumeNoWrapAccess`（关系消去局部 → `[1, p1 + 1]`、`memset_s(p + off, n − off, …)` 抵消成 `[p1, p1]`、默认记账）；
  `run.py csa --config AssumeNoWrap=true`、`engine_check --config AssumeNoWrap=true`（见 §3）；默认 `run.py csa` 不变。

## 6. 调试

`Dump=state`（`rels:`）；`Dump=options`（`AssumeNoWrap=`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §4 选项表 | …、I-27 `Subobject` | 加 I-28 `AssumeNoWrap` |
| A-13 定义式 | `defs: 局部 → Expr` | 另：选项打开时的局部线性关系 `rels`（SPEC-0108） |

## 8. 公司侧验证

业务子目录上 `AssumeNoWrap=false` / `true` 各跑一遍，比 SUSPECT 条数；抽查打开后不再报的是否确实不会回绕。

## 9. 待定 / 以后

无。
