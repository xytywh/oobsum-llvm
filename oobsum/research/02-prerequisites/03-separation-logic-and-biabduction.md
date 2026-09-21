# 分离逻辑与 Bi-Abduction（给懂 CSA 符号执行的人）

> 读者假设：懂 CSA 的 `ProgramState` / `SVal` / `MemRegion` / `ConstraintManager` / `ExplodedGraph`。
> 目标：把"分离逻辑"和"双向演绎"讲成你能拿来对照自己方案的东西。
>
> **版本前提（很重要）**：经典 Bi-Abduction 的独立实现（旧 `infer/src/biabduction/`）
> 在本次调研的 commit 中**已被整体移除**；思想保留在 Pulse 里。
> 所以下面分两部分讲：思想本身，以及它在当前版本的落地形态。

---

## 第一部分：分离逻辑

### 1. 问题：CSA 的状态模型在跨过程时为什么别扭

CSA 的 `ProgramState` 大致是：

```
Env:   变量  → SVal
Store: MemRegion → SVal
Constraints: 一组作用于 SVal 的约束
```

这是一个**全局的、单一的状态**：任何时刻只有一份 Store，描述"整个堆"。
两个指针是否别名，要靠 `MemRegion` / `SymbolicRegion` 的等价关系另外维护。

跨过程时这套模型的麻烦在于：**你没法说"这个函数只碰了堆的这一小块"**。
`set(p)` 只写了 `*p`，但它的状态描述的是整个堆，你不知道哪些部分是它真正关心的、
哪些只是恰好在那儿。于是 Inline 成了最省事的选择——把 callee 的语句直接接到 caller 的路径上继续走。

### 2. 分离逻辑的基本想法：让状态只描述"我碰到的那一块"

最基本的原子断言：

```
p |-> v        读作 "points-to"
```

含义是：**当前堆里恰好存在一块由 `p` 指向的内存单元，值为 `v`；
这个断言描述的就是这一块，别的什么都没说。**

关键在**分离合取** `*`：

```
p |-> v1  *  q |-> v2
```

它不只是"p 指向 v1 并且 q 指向 v2"，它**额外断言了 `p` 和 `q` 指向两块不相交的内存**。

用 C 看：

```c
int *p = malloc(sizeof(int));  *p = 1;      /* 堆断言: p |-> 1              */
int *q = malloc(sizeof(int));  *q = 2;      /* 堆断言: p |-> 1 * q |-> 2    */
```

**`*` 把"不别名"这件事直接编码进了逻辑连接词。** 在 CSA 里你得另外去证明 `p != q`，
或者建两个独立 `MemRegion` 再靠 `RegionStoreManager` 维护它们不重叠；
分离逻辑里这是"写下 `*`"的副产品。

### 3. 真正的收益：Frame Rule（局部推理）

```
        {P} C {Q}
──────────────────────────   （C 不修改 R 里提到的变量）
   {P * R} C {Q * R}
```

翻译：**如果我证明了 `C` 在"只有 P 这块堆"的前提下能得到 `Q`，
那么把 `C` 放进任何更大的堆里，多出来的那部分 `R`（叫"帧"）原样保留。**

这就是跨过程摘要的理论基础：

* `set(int *p) { *p = 1; }` 的摘要是 `{p |-> _} set(p) {p |-> 1}`——**只描述它碰到的那一格**。
* 调用方的堆无论多大，调用 `set(q)` 之后，除了 `q` 那一格变成 1，其余部分**不用重新推理**。

对照 CSA Inline：inline 之后 callee 的每条语句都在 caller 的完整状态上重新执行一遍，
caller 已有的路径约束和 callee 内部新产生的约束互相叉乘——**这就是路径爆炸的来源**。

### 4. 数组在分离逻辑里怎么表示

链表那种归纳谓词（`list(n)`）不是我们关心的重点。对数组而言，
Infer 的 BufferOverrun 用的是一个**区间抽象域下的具体实现**：

```
ArrayBlk.t = Allocsite.t → { offset : Itv.t ; size : Itv.t ; stride : Itv.t }
```

每个 `Allocsite`（数组声明点、`malloc` 调用点……）就是堆里一块被 `*` 分离出来的独立区域。
真实输出：

```
a -> (_|_, _|_, { f--746625832-1-1(a) -> offset : 0, size : 10 })
```

**这就是你们方案里想要的 `extent(buf)` / `offset` / `access size` 的真实载体。**

---

## 第二部分：Bi-Abduction（双向演绎）

### 5. 它要解决什么

自底向上分析有个根本问题：**分析 callee 时，你不知道 caller 会传什么进来。**

```c
void set(int *p) { *p = 1; }
```

单独分析 `set`，进入函数体时对 `p` **一无所知**，堆断言是空的 `emp`。
执行到 `*p = 1`（一次 `Store`）时，分离逻辑要求"写之前这块内存必须存在"——可它不在断言里。

三种选择：

1. **报错**："空指针解引用"——荒谬，`set` 本身没错。
2. **放弃**：标记 `unsupported`——**这正是你们当前方案的失败模式**。
3. **溯因（abduce）**：反推出"要让这次写安全，前置条件里必须有 `p |-> _`"，把它**加进前置条件**。

Bi-Abduction 选 3。"Bi"（双向）指的是同时推两个未知量：

```
当前状态  *  [缺失堆]   ⊨   callee 前置条件  *  [帧]
             ↑ 要推的                            ↑ 要推的
```

* **缺失堆（missing / anti-frame）**：还差什么才能满足前置条件 → 补进前置条件。
* **帧（frame）**：callee 用不到、调用后原样保留的部分。

### 6. 走一遍完整例子

```c
void set(int *p) { *p = 1; }
void caller(void) {
    int *q = malloc(sizeof(int));
    set(q);
}
```

**第一步：单独分析 `set`**

| 程序点 | 前置条件（累积溯因出来的） | 后置状态 |
|---|---|---|
| 入口 | `emp` | `emp` |
| `*p = 1` 之前 | 要写 `p`，但堆里没有 → **溯因** `p \|-> _` | — |
| `*p = 1` 之后 | `p \|-> _` | `p \|-> 1` |

摘要：`{pre: p |-> _}  set(p)  {post: p |-> 1}`

**第二步：分析 `caller`**

1. `q = malloc(...)` → 状态 `q |-> _`
2. 调用 `set(q)`：把摘要里的形参 `p` 代换成实参 `q`，得到 `{pre: q |-> _} … {post: q |-> 1}`
3. 检查蕴含：当前状态 `q |-> _` 能否满足 `q |-> _`？**能，完全匹配**。
   缺失堆 = 空，帧 = 空。
4. 应用后置条件 → `caller` 在调用点之后的状态是 `q |-> 1`。

**注意第 3 步是一次代数操作**（蕴含检查 + 代换），**不重新走 `set` 的函数体**。
计算量与 `set` 内部有多少条路径**无关**。这就是"预先压缩掉被调者的状态空间"的含义。

### 7. 在当前版本里它长什么样（实测）

`infer/src/pulse/PulseAbductiveDomain.mli` 的模块注释原文：

> Propagate operations on the current state to the pre-condition when necessary. This is the
> "abductive" part of the domain and follows the principles of biabduction as laid out in
> *Compositional Shape Analysis by Means of Bi-Abduction*, JACM, 2011. Simply put, **a read from a
> memory location that is reachable from the precondition for the first time is taken to mean that
> this memory location had better been allocated since the beginning of the function for the
> operation to be safe, hence we "abduce" that this was the case and add it to the pre-condition**.

对 `int read_value(int *buf, int i) { return buf[i]; }` 实跑
`infer debug --procedures-summary`，真实输出：

```
Pulse: 1 pre/post(s)
#0: ContinueProgram
    Current post: buf[i]->return  &return: { Initialized, WrittenTo (7, ) }

    Inferred pre: buf[i]->-                      ← ★ 溯因出来的前置条件
    &buf:   { MustBeInitialized(, t=0), MustBeValid(, None, t=0) }
    &i:     { MustBeInitialized(, t=0), MustBeValid(, None, t=0) }
    buf:    { MustBeInitialized(, t=3), MustBeValid(, None, t=3) }
    buf[i]: { MustBeInitialized(, t=3), MustBeValid(, None, t=3) }

Raw state: { roots={ &i|read_value=v2, &buf|read_value=v1, &return|read_value=v7 };
             mem  ={ v1 -> { * -> v3 },      (* *buf   *)
                     v2 -> { * -> v4 },      (* *i     *)
                     v3 -> { [v4] -> v5 },   (* buf[i]  ← ArrayAccess 边 *)
                     v5 -> { * -> v6 },
                     v7 -> { * -> v6 } };    (* return = buf[i] *)
             attrs={ v7 -> { Initialized, WrittenTo (7, ) } };}
```

逐句读：

* `Inferred pre` 里的 `MustBeValid` / `MustBeInitialized` **就是被溯因出来的前置条件**：
  "要让 `buf[i]` 这次读安全，`buf` 和 `buf[i]` 这两个地址从函数入口起就必须有效且已初始化"。
* `mem` 是一张**抽象地址 → 边**的图（`v1 -> { * -> v3 }` 读作"v1 解引用得到 v3"）。
  **两个变量是否别名，就看 `roots` 里它们是不是映射到同一个抽象地址** ——
  比 CSA 的 `MemRegion` 等价关系直接得多。
* `v3 -> { [v4] -> v5 }` 就是 `buf[i]`：对 v3 做下标为 v4 的数组访问。

### 8. 和 CSA Inline 的本质区别（一张表）

| | CSA Inline 符号执行 | Bi-Abduction / 摘要 |
|---|---|---|
| callee 被处理几次 | **每个调用点一次**（且在 caller 的路径上下文里） | **一次**，与调用点无关 |
| caller 端的操作 | 继续走 callee 的每条语句、每个分支 | 蕴含检查 + 形参→实参代换 + 应用后置 |
| 代价与 callee 路径数的关系 | 成正比（并与 caller 路径数相乘） | 无关（只与摘要的析取分支数有关） |
| 状态的生命周期 | 绑定在当前这条 ExplodedGraph 路径上 | **路径无关、可序列化、可任意实例化** |
| 遇到"没约束过的内存" | 沿用 caller 状态（inline 时它已知） | **溯因出一条新前置条件** |

最后一行是**你们方案真正缺的那一块**：
CSA 的状态模型里没有"前置条件"这个概念——因为 inline 时永远有一个具体的 caller 状态可用。
一旦要做摘要，"遇到不知道的东西怎么办"就必须有答案，而 Bi-Abduction 给的答案是
**把它记成一条 requires，留给调用者去满足**。

### 9. BufferOverrun 用的是同一个思想的简化版（这个对你们更实用）

Pulse 的完整溯因很重，但 BufferOverrun 做的事本质一样、实现轻得多：
它把"没法判定的安全条件"直接留在摘要里往上传。真实输出（三层调用链）：

```
leaf:   base.offset + idx    < base.length   @ sub.c:1:39
mid:    arr.offset  + k + 1  < arr.length    @ sub.c:1:39     ← 代换 base↦arr, idx↦k+1
top:    p.offset + 2.n + 1   < p.length      @ sub.c:1:39     ← 代换 arr↦p,  k↦2·n
caller: { }  （全部判定完毕）+ 报 BUFFER_OVERRUN_L1 Offset: 15 Size: 10
```

源码里对应 `bufferOverrunProofObligations.ml`：

* `ArrayAccessCondition = {offset; idx; size; last_included; void_ptr}` —— 一条 requires。
* `check` 判级：证明安全 → 丢弃；必然越界 → L1；很可能 → L2；
  **符号且判不了 → `SymbolicIssue` + `propagate = true`**（不报，但继续往上传）。
* `ConditionSet.subst` —— 在调用点做形参→实参代换。
* `ConditionSet.for_summary` —— 只保留 `propagate = true` 的条件进摘要。

**这三个函数就是一个可直接照搬的"摘要 = 未判定的 requires 集合"设计。**
报告 §25 建议的 CSA Summary 2.0 里的 `PreCondition` / `SummaryApplier`，对标的就是它们。

---

## 10. 记住这三句就够了

1. **分离逻辑 = 让状态只描述"我碰到的那块堆"**，`*` 顺带编码了不别名，于是有 Frame Rule，
   于是"函数摘要"在理论上站得住。
2. **Bi-Abduction = 遇到没约束过的内存就溯因出一条前置条件**，而不是报错、也不是放弃。
   它是"自底向上分析怎么起步"这个问题的答案。
3. **你们真正要抄的不是完整的分离逻辑**，而是 BufferOverrun 那个轻量版：
   摘要的主体是一组"还没判定的 requires"，调用点做代换，判得了就判、判不了就继续往上传。

---

## 参考

* Calcagno, Distefano, O'Hearn, Yang. *Compositional Shape Analysis by Means of Bi-Abduction*.
  JACM 58(6), 2011. https://doi.org/10.1145/2049697.2049700 —— Pulse 源码注释直接引的这篇。
* Reynolds. *Separation Logic: A Logic for Shared Mutable Data Structures*. LICS 2002 —— 分离逻辑原始论文。
* 动手：本包 `05-experiments/00-basic-summary/`，跑
  `infer run --pulse-only -- gcc -c example.c` 然后
  `infer debug --procedures --procedures-summary --select all`，
  自己看一遍 `Inferred pre` 长什么样。
