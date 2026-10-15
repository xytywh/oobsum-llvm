# SPEC-0009 降级：条件边（`assume`）、`switch`、`?:` / `&&` / `||` 当值用（06- D-14、A-04）

状态：已实现（S5）
来源：`STATUS.md` S5；06- §5.2 D-14（`if` / 循环条件降成两条边上的 `assume E` / `assume !E`；`switch` 每个 `case` 一条 `assume x == c`、
`default` 一条，`case` 超过 16 个时 `default` 不带 assume；`&&`、`||`、`?:` 由 Clang CFG 拆好）、A-04（`?:`、`&&`、`||` 当值用时，
在前驱块末尾补 `set tmp`，汇合块引用 `tmp`）；SPEC-0007 §2.4（S4b 先导出不带条件的边）。

## 1. 要解决什么

不带条件的边是过近似：引擎会以为 `if (i < n) a[i] = 0;` 里的写在 `i >= n` 时也会发生，于是证明不了安全，只能报 SUSPECT。
加上 `assume`，循环界、校验（F-12 的 `loop` / `check` / `branch` 界）才能进到引擎里。`?:` / `&&` / `||` 当值用时（`m = a < b ? a : b`）
现在是 `unk(cond_value)`，要换成真实的值。

**这一步对召回最敏感**：`assume` 写反或写错一个边，引擎就会剪掉一条真实可走的路径，可能把越界判成安全。所以规则只取 Clang CFG 的
确定约定，拿不准的边一律**不带条件**（过近似，安全）。

## 2. 设计

### 2.1 两路分支的边

Clang CFG 的约定（`CFGBuilder`，与 ExprEngine 的 `processBranch` 一致）：终结语句是 `if`、`while`、`do`、`for`、`?:`、`&&` / `||` 的块，
**后继 0 是"条件为真"、后继 1 是"条件为假"**（`&&` / `||` 的条件是它左边的值）。

* 被测的值：`CFGBlock::getTerminatorCondition()`；它是 `&&` / `||` 时（`if (a && b)` 的第二个块），被测的是**块里最后一个语句元素**
  （与 ExprEngine 的 `ResolveCondition` 相同）。
* 条件表达式 `C`：被测值本身是比较或 `!x` 时就是它；否则 `v != 0`（`cmp ne`）。
* 后继 0 的边 `assume C`；后继 1 的边 `assume ¬C`：比较取反（`==`↔`!=`、`<`↔`>=`、`<=`↔`>`，有无符号不变），`!x` 的反是 `x != 0`，其余 `!C`。
* Clang 已判定不可达的后继（`getReachableBlock()` 为空）照旧不导出，但**按原下标**对应真假，不错位。
* 被测值是常数（`while (1)`）时边不带条件：恒真 / 恒假，Clang 已把不可走的那个后继标成不可达。
* 其它终结语句（`goto`、`break`、`continue`、计算 `goto`、`for (;;)`、没有条件的）：边不带条件。

### 2.2 `switch`

被测值 `v` 是 `switch` 的条件（已带整数提升）。每个后继块看它的标号：

* `case c:` → `assume v == c`（有无符号取 `v` 的类型；`c` 用 `EvaluateKnownConstInt`）。
* `case a ... b:`（GNU 区间）→ 不带条件。
* `default:` 或没有 `default` 时的"`switch` 之后"那个后继 → 有区间、或 `case` 超过 16 个时不带条件；否则
  `assume (v != c1) & (v != c2) & …`（数学整数上的按位与，各项都是 0 / 1；没有 `case` 时不带条件）。

### 2.3 `?:` / `&&` / `||` 当值用（A-04）

它们当值用时（`x = a && b;`、`m = c ? a : b;`），在 CFG 里是汇合块的一个元素；当条件用时（`if (a && b)`）不是元素，只是分支。

* 预扫：凡是作为语句元素出现的 `?:` / `&&` / `||`，给它分一个临时局部 `t<k>`；它的值就是 `l<t>`。
* 降完所有块的指令后，在**汇合块的每个前驱块末尾**补一条 `set`：
  * `?:`：`t = <前驱块最后一个语句元素的值>`（就是走到这里的那一支的值）；
  * `&&` / `||`：前驱块的终结语句是 `&&` / `||`（短路的那一跳）→ `t = 1`（走的是后继 0，被测值为真）或 `t = 0`（后继 1）；
    否则（最右边的那个操作数求完直接进汇合块）→ `t = <最后一个语句元素的值> != 0`。
  * 前驱块同时还有别的出边时，这条 `set` 在别的路径上会被后面的块覆盖（每条进汇合块的路径都以一个补了 `set` 的块结束），所以结果正确。
* GNU 的 `a ?: b` 仍是 `unk(cond_value)`（少见，S9 再看）。

### 2.4 例

```c
while (i < n) { … }      →   b6: -> b5 if l7 <u l1        -> b3 if l7 >=u l1
if (p) { … }             →   -> b2 if l0 !=u 0            -> b1 if l0 ==u 0
m = a < b ? a : b;       →   b4: … -> b3 if l0 <u l1  -> b2 if l0 >=u l1;  b3: l3 = l0;  b2: l3 = l1;  b1: l2 = l3
switch (k) { case 1: … case 2: … default: … }  →  -> b4 if l0 ==s 1   -> b3 if l0 ==s 2   -> b2 if (l0 !=s 1) & (l0 !=s 2)
```

## 3. 对召回与误报的影响

`assume` 只会收窄路径，写错就可能漏报；所以：只用 Clang CFG 的确定约定（后继 0 为真）；被测值与 ExprEngine 取法相同；不可达后继按原下标；
`switch` 的区间与超过 16 个 `case` 的 `default` 不带条件；条件里的有无符号取 AST 类型。每种终结语句都有 lit 用例对照 `debug.DumpCFG`
（§5），把方向钉死。

## 4. 实现位置

`OobSummary/lower/Lowering.cpp` 的 `FunctionLowerer`：`LowerBody`（预扫汇合块、补 `set`、`BranchEdges`、`SwitchEdges`）、`AsCondition`、`Negate`。

## 5. 测试

| lit | 覆盖 |
|---|---|
| `fir-branches.c` | `if`（比较、指针、`!x`、整数）、`if/else`、`while`、`do`、`for`、`for (;;)`、`while (1)`（不可达后继按原下标）、`if (a && b)` / `if (a \|\| b)`（第二个块被测的是 `b`）、`switch`（`case`、`default`、没有 `default`、GNU 区间、17 个 `case` 的 `default` 不带条件）、`?:` 当值、`&&` / `\|\|` 当值、嵌套 `(a && b) \|\| c` 当值、条件里有副作用（`while (i++ < n)`）；每个函数的块号与边对照 `debug.DumpCFG` |

另外：67 个用例 `Dump=fir` 无崩溃、自检无错误，`unk(cond_value)` 只剩 GNU `?:`。

## 6. 调试

`Dump=fir` 看边上的 `if …`；对照 `-analyzer-checker=debug.DumpCFG` 的 `Succs (2): B<真> B<假>`（顺序就是后继 0、1）与 `T: …`（终结语句）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-14 | `default` 一条（`case` 超过 16 个时不带 assume） | 同；另：GNU `case` 区间不带条件，有区间时 `default` 也不带；`default` 的条件写成各 `!=` 的按位与 |
| A-04 | 在两个前驱块末尾各补一条 `set tmp` | 推广到汇合块的所有前驱；`&&` / `\|\|` 短路那一跳写常数 0 / 1 |

## 8. 公司侧验证

不需要单独验证；可选：`Dump=fir` 看业务代码里循环的条件边。

## 9. 待定 / 以后

* GNU `?:`（`BinaryConditionalOperator`）的值。
* `MaxBlocks`（A-09）选项。
