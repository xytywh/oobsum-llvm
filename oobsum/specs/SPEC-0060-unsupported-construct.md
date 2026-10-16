# SPEC-0060 S5b-2：访问因不支持的构造而进账本时，原因记 `unsupported_construct` / `struct_by_value` / `inline_asm`

状态：已实现
来源：STATUS S5b-2（"不支持的构造 `unk(<类名>)` 计入判定日志，SUSPECT:unsupported_construct"）；06- A-08（不认识的构造 → `havoc` 或 `unk`，带原因）、
D-27 的运行类原因 `unsupported_construct`、`struct_by_value`、`inline_asm`（白名单里早就有，从来没有产出过）。SPEC-0037 §9 记过它是 S5b 的另一半。

现在：降级遇到不认识的表达式写 `unk(<Clang 类名>)`（或 `cast:<种类>`、`unary:<op>`、`binary:<op>` 等），解释时它是未知值 / 未知指针，原因文本一路带着
（`IntVal::Top(why)`、`UnknownPtr(why)`）。访问用到它时照样进账本（SUSPECT，召回没丢），但原因是笼统的 `untracked_pointer` / `type_range_only`——
复核的人分不清"代码本身没有界"和"引擎不认识这种写法"。

## 2. 设计

`ConstructReason(why)`（`core/Interp.cpp`）把未知值的原因映射成账本原因：

| why | 账本原因 |
|---|---|
| `struct_by_value`、`struct_rvalue` | `struct_by_value` |
| `inline_asm` | `inline_asm` |
| 大写字母开头（Clang 的类名，如 `StmtExpr`、`CompoundLiteralExpr`、`VAArgExpr`）、`cast:*`、`unary:*`、`binary:*`、`decl_ref`、`member`、`lvalue_as_rvalue`、`unknown_var`、`wide_string` | `unsupported_construct` |
| 其它（`float`、`int_to_ptr`、`call`、`data`、`widen`…：引擎认识、只是值不知道） | 不改 |

访问检查（`Access` / `BackAccess`）里原来记 `untracked_pointer`（基址未知）、`type_range_only`（偏移 / 字节数没有界）的地方：基址的 why、偏移或字节数
（是 `Top` 时）的 why 映射得出原因时就记这个原因。只换账本原因，**记不记、报不报都不变**（仍是 SUSPECT）。

why 只在值直接来自那个构造时保留（加常数保留；经过乘法、汇合等可能变成别的原因）：尽力而为，不影响判定。

## 3. 对召回与误报的影响

无（只换 SUSPECT 的原因）。判定日志 / 告警文本更准，`SuspectReasons` 可以单独过滤这几类。

## 4. 实现位置

`core/Interp.cpp`（`ConstructReason`、`OpenReason`、`Access`、`BackAccess`；`SetLocal` 把不支持构造的未知值赋给指针局部时留着原因）、
`core/Engine.cpp`（`LedgerMessage` 的三条文本）。

## 5. 测试

* lit `unsupported.test`：`va_arg` 作下标 → `SUSPECT:unsupported_construct`；指向复合字面量的指针 → `unsupported_construct`；内联汇编写出的下标 → `inline_asm`；
  对照：整数转成的指针 → 照旧 `untracked_pointer`。
* 单测 `Interp.UnsupportedConstructReason`：`VAArgExpr` 下标、`CompoundLiteralExpr` 指针、`havoc(inline_asm)` 下标、对照 `float` 下标（`type_range_only`）。
* `run.py csa` 与基线相同（67 个用例里没有不支持的构造）。

## 6. 调试

`Dump=fir`（`unk(<类名>)`）、`Dump=summary`（`opens`）。

## 7. 对主规格的修订

无（按 A-08、D-27 实现）。

## 8. 公司侧验证

可选：业务子目录的原因计数里看 `unsupported_construct` 有多少、分别是什么构造（`Dump=fir` 的 `unk(…)`），决定要不要补降级。

## 9. 待定 / 以后

* 不经访问的不支持构造（只在条件里、只赋给不用的变量）不记：不影响任何判定。
