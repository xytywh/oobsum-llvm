# 抽象解释入门（给懂数据流分析的人）

> 读者假设：懂经典数据流分析（到达定值、活跃变量、常量传播），懂 CSA 的符号执行与 ExplodedGraph。
> 目标：把"抽象解释"这四个字从玄学变成你已经会的东西 + 两个你可能没做过的算子。

---

## 1. 一句话：抽象解释 = 你已经在做的数据流分析 + 严格的理论外壳

你写过的常量传播就是抽象解释。对照一下就清楚了：

| 数据流分析的说法 | 抽象解释的说法 | Infer 里的实体 |
|---|---|---|
| 数据流值（每个程序点的 fact） | **抽象状态**（abstract state） | `BufferOverrunDomain.Mem.t` |
| 值的取值格（lattice） | **抽象域**（abstract domain） | `AbstractDomain.S` |
| 传递函数 gen/kill | **Transfer Function** | `TransferFunctions.exec_instr` |
| 汇合运算 meet/join | **join**（`⊔`） | `Domain.join` |
| 迭代到稳定 | **不动点**（fixpoint） | `AbstractInterpreter.compute_post` |
| （经典数据流里常常没有） | **widening**（`∇`） | `Domain.widen` |
| 遍历顺序（RPO / worklist） | WTO（弱拓扑序） | `AbstractInterpreter.MakeRPO` |

所以你**不需要**学新范式，只需要补两件事：

1. 当格的高度是**无限**的时候（比如整数区间），朴素迭代不保证终止 → 需要 **widening**。
2. 抽象状态不是"每个变量一个常量"，而是"每个变量一个**能表达一整类值的对象**"（区间、区间+符号、集合……）。

---

## 2. 核心概念：具体 vs 抽象

程序真实跑起来，某个程序点上 `i` 的取值可能是 `{0, 1, 2, …, 10}`（**具体状态集合**）。
分析器不可能枚举它，于是用一个**抽象值**代表它：

* 常量传播的抽象值：`⊥`（不可达）/ `c`（确定是常量 c）/ `⊤`（不知道）——格只有三层，**有限高**。
* 区间抽象的抽象值：`[0, 10]`、`[0, +∞]`、`[n, n+5]`（可带符号）——格**无限高**（下界能一直降、上界能一直升）。

Infer 的 BufferOverrun 用的就是区间。它的抽象值打印出来长这样（真实输出）：

```
i -> ([0, 10], _|_, { })
     ↑itv     ↑powloc ↑arrayblk
a -> (_|_, _|_, { f--746625832-1-1(a) -> offset : 0, size : 10 })
```

这个三元组 `(itv, powloc, arrayblk)` 就是 `BufferOverrunDomain.Val.t`：
"这个值的数值范围是什么 / 它作为指针可能指向哪些位置 / 它作为数组的 offset 与 size 是什么"。

**对照 CSA**：CSA 的 `SVal` 是"这一条具体路径上的符号值"，配 `ConstraintManager` 存路径约束；
Infer 的 `Val.t` 是"**所有到达这一点的路径合并之后**的抽象值"。这是最根本的差别，下一节展开。

---

## 3. join：Infer 与 CSA 最根本的分歧点

```c
int i;
if (flag) i = 0; else i = 12;
return a[i];          // a 有 10 个元素
```

**CSA 的做法**：ExplodedGraph 分裂成两条路径，一条 `i=0`、一条 `i=12`，各自走到 `a[i]`。
好处：能说出"当 `flag` 为假时必然越界"。代价：路径数随分支指数增长。

**Infer BufferOverrun 的做法**：在汇合点做 `join`，`[0,0] ⊔ [12,12] = [0,12]`，
只保留**一份**状态继续往下走。到 `a[i]` 时看到 `offset ∈ [0,12]` 与 `size = 10` **部分重叠**，
于是报 `BUFFER_OVERRUN_L2`（"可能越界"）而不是 L1。

实测输出正是：

```
c01_if  →  BUFFER_OVERRUN_L2   Offset: [0, 12] Size: 10
```

**这就是"路径敏感 vs 非路径敏感"的全部含义**：
join 之后你不再知道"是哪条分支导致的"，换来的是状态数不随分支增长。

> 报告 §16 的实测把这件事推到极端：一个有 2¹² = 4096 条路径的函数，
> BufferOverrun **一次 join 得到 `Offset: [0, 4095]`，常数代价且检出**；
> 而路径敏感的 Pulse 因为要保留析取状态，触到 `pulse_max_disjuncts=20` 的上限，
> 主动丢弃了 152 条状态（= 欠近似 = 漏报）。

---

## 4. 不动点：循环怎么处理

循环头是一个 join 点：它的前驱有"第一次进入"和"从循环体绕回来"两条边。
所以分析循环就是**反复计算循环头的状态直到不再变化**：

```
X₀ = 初始
X₁ = X₀ ⊔ F(X₀)
X₂ = X₁ ⊔ F(X₁)
...
直到 Xₙ₊₁ ⊑ Xₙ   ← 不动点，停
```

`F` 就是"走一遍循环体"的 transfer function。`⊑` 是格上的偏序（"更精确"）。

对常量传播这种**有限高**的格，这个迭代一定会停（每次要么不变要么往上爬一格，格就那么高）。
**对区间这种无限高的格，它可能永远不停**：`[0,0] → [0,1] → [0,2] → …`。

---

## 5. widening（`∇`）：让无限高的格也能收敛

widening 是一个**故意变不精确**的算子：当发现状态在往上爬时，直接"跳"到一个更宽松的值。

最朴素的区间 widening：

```
[0,0] ∇ [0,1]  =  [0, +∞]     ← 上界在涨，直接跳到无穷
```

这保证有限步收敛，但精度全丢了 —— `i ∈ [0,+∞]` 对判断数组越界毫无用处。

**Infer 的做法（这是最值得抄的一条）**：不跳到无穷，而是跳到**阈值集合里的下一个值**。
阈值从哪来？**从程序里出现过的比较常量来**。

真实源码（`bufferOverrunDomain.ml:392-398`，在 prune/分支裁剪的路径上）：

```ocaml
let itv_thresholds =
  Option.value_map (Itv.get_const y.itv) ~default:x.itv_thresholds ~f:(fun z ->
      x.itv_thresholds
      |> ItvThresholds.add Z.(z - one)
      |> ItvThresholds.add z
      |> ItvThresholds.add Z.(z + one) )
```

读作：**每当一个值和常量 `z` 做比较，就把 `z-1, z, z+1` 塞进这个值的 widening 阈值集合。**

于是对 `for (int i = 0; i <= 10; ++i)`，因为有 `i <= 10` 这个比较，阈值集合含 `{9, 10, 11}`，
真实的迭代过程是（从 `--debug` 的逐节点 HTML 里抓的原始输出）：

```
node5 (loop head) JOIN     i -> (0) ⊔ (1)          =>  i -> [0, 1]
node5 WIDEN(num_iters = 1)  prev [0,0]  next [0,1]  =>  i -> [0, 9]    ← 跳到阈值 9，不是 +∞
node5 JOIN                                          =>  i -> [0, 10]
node5 WIDEN(num_iters = 2)  prev [0,9]  next [0,10] =>  i -> [0, 10]   ← 下一个阈值
node5 [exec NARROWING]                                  Fixpoint reached.
```

**只访问 loop head 三四次就收敛，而且保住了 `i` 恰好到 10 这个关键精度**
（于是 `[0,10]` vs `size=10` → 报 L2）。

> 这段十行不到的机制，是报告给出的"今天就能抄进 CSA"的第一条建议。

---

## 6. narrowing（`Δ`）：把 widening 丢掉的精度捞回来一点

widening 冲过头了怎么办？收敛之后再做几轮**不带 widening 的**迭代，
状态只会往回收（因为已经是不动点的上界了），这叫 narrowing。
上面输出里的 `[exec NARROWING]` 就是这一阶段。

Infer 里由 `Config.max_narrows` 限制轮数。

---

## 7. 安全阀：Infer 把"必须终止"当硬约束

`infer/src/absint/AbstractInterpreter.ml` 第 24-32 行：

```ocaml
let succ visit_count =
  let visit_count' = visit_count + 1 in
  if visit_count' > Config.max_widens then
    L.die InternalError
      "Exceeded max widening threshold %d. Please check your widening operator or increase the \
       threshold" Config.max_widens ;
  visit_count'
```

翻译：**如果 widening 之后还不收敛，直接崩掉报错，而不是死循环。**
这是一种设计表态——抽象域的 `widen` 实现如果不保证收敛，那就是 bug，要修，不是绕。

---

## 8. 抽象解释保证什么、不保证什么

| 保证 | 不保证 |
|---|---|
| **终止**（有 widening + 安全阀） | **精确**（区间可能宽到 `[-∞,+∞]`） |
| **可靠的过近似**（真实值一定在抽象值里，所以"证明安全"是可信的） | 不漏报（Infer 在多处主动做欠近似换性能，见报告 §11） |
| 每个函数**一定**产出一个类型合法、可被调用者消费的摘要 | 这个摘要一定有用 |

**最后一行是与你们当前方案最关键的对比**：

* 你们现在的失败模式是 **`opaque` / `unsupported`** —— 摘要不存在，只能退化为 inline 兜底。
* Infer 的失败模式是 **精度降级** —— 摘要存在，只是可能宽到查不出真 bug。

后者可以靠调参（`--bo-debug 4` + 启用 L4/L5）把精度换回来一部分，前者不能。
报告 §14 的 13 类场景矩阵就是这条结论的实测支撑。

---

## 9. 术语与符号速查

| 符号 | 读法 | 含义 | Infer |
|---|---|---|---|
| `⊑` | leq / 偏序 | "更精确或相等" | `Domain.leq ~lhs ~rhs` |
| `⊔` | join | 合并两个状态（取上确界） | `Domain.join` |
| `⊓` | meet | 取下确界（Infer 里用得少） | — |
| `⊥` | bottom | 不可达 / 空集 | 输出里打印成 `_\|_` |
| `⊤` | top | 什么都不知道 | 输出里打印成 `T` 或 `[-oo, +oo]` |
| `∇` | widen | 外推，保证收敛 | `Domain.widen ~prev ~next ~num_iters` |
| `Δ` | narrow | 收敛后回收精度 | narrowing 阶段 |

看 Infer 输出时：`_|_` 就是 ⊥，`[-oo, +oo]` 就是整数域的 ⊤。

---

## 10. 想继续深入

* Cousot & Cousot, *Abstract Interpretation: A Unified Lattice Model…*, POPL 1977 —— 原始论文。
* Bourdoncle, *Efficient chaotic iteration strategies with widenings*, 1993 —— WTO 遍历顺序的来源，
  Infer 的 `[WEAK TOPOLOGICAL ORDER]` 输出就是它。
* 动手：`05-experiments/01-thirteen-scenarios/c02_for.c` 加 `--debug` 跑一遍，
  照 `06-practice/02-observability-handbook.md` §2.5 的脚本把逐节点 HTML 提取出来，
  自己看一遍 JOIN / WIDEN / Fixpoint reached 的完整序列。**这比读十篇论文都直观。**
