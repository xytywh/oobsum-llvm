# CSA Function Summary 2.0 —— 总体设计

> **先看 `00-implementation-plan.md`**：方案一句话、分几步、优先级、倒排到 12 月 15 日的计划都在那里；本文件是它的设计展开。
> **做哪些场景、承诺到什么程度看 `04-scenario-tiers.md`**（v3.10：Lv1–Lv10 分级，年内承诺 26 个场景）。
> **摘要的形式、取舍、怎么串联、67 个分级用例，看 `05-summary-design-and-cases.md`**（v3.11）；本文件 §3.12 是 v3.11 的设计补充。
> **v3.12：`06-spec.md` 把本设计细化成规格，并修订了 6 处**（其 §13）：跨 TU 由方案 A 改为方案 B（§3.11）、落盘由 SQLite / FlatBuffers 改为 JSON（§3.8）、
> 键由 USR 改为自定义键（§3.2）、宿主回调改为 `check::ASTDecl<TranslationUnitDecl>`（§2）、界同时带 min/max 与守卫（§3.3）、条件统一按字节（§3.5）。
> **实现以 `06-` 为准。**

> 这份文档把报告 §25 / §28 的"方案 C"从**架构建议**细化到**可以开工的设计**：
> 组件边界、数据结构、关键算法、与现有 CSA 的接口、以及每条设计决策的**证伪条件**。
>
> **v3.6 状态：已立项，进入实施阶段。**【立项输入】
> 目标已经确定：**支持现有工具因路径爆炸而不支持的跨多层调用越界场景**（立项统计 2–17 层）。
> 1–2 层现有工具已支持，摘要通道不重复做；"要不要做、做多大"不再由实测决定，见 §0.1。
>
> **重要前提**：我看不到 `rb.enhance.core` / `rb.enhance.ipa` 的代码。
> 文中凡是依赖"你们的 CSA 具备某能力"的地方，都标了 **【假设 An】**，
> 并在 `03-assumptions-and-test-plan.md` 里给出了具体测法。
> **A1（影响 ExtentModel 实现路径的那条）已在 fork 上验证成立**；其余假设不再是开工前置条件，
> 在实施中按需验证。

---

## 0. 一句话设计

**把"生成摘要"做成一个与 ExplodedGraph 完全解耦、对 Clang CFG 做非路径敏感抽象解释的
独立组件（它的"全函数性"是硬约束：任何函数都必须产出合法摘要）；
CSA 的路径敏感能力退到两个位置——消费摘要出告警、以及为已判定的告警补路径解释。**

---

## 0.1 立项目标与范围（v3.6）【立项输入】

| 项 | 内容 |
|---|---|
| **要解决的问题** | 现有 CSA 规则在**跨多层调用**的越界场景上漏报：inline 每深一层路径数成倍增长，**路径爆炸**导致这些场景不支持 |
| **立项统计的深度范围** | **2–17 层**（立项时的大致统计） |
| **1–2 层** | **现有工具已支持**，摘要通道不重复报（见下面"通道分工"） |
| **深度上限** | **不设**。验收语料覆盖到 **20 层**，高于立项统计的 17 层 |
| **不再作为前置条件的测量** | T0-2（单函数可判定比例）、T0-3（深度分布）、T0-4（unsupported 根因）——它们原本回答"值不值得做"，立项已经回答了 |

### 摘要方式到底受不受层数限制

**一句话：成本不受层数限制；精度受"链上每一层做了什么"影响，不受"第几层"影响。**

**成本：与层数无关，与函数个数成线性。**【实测数据】
每个函数只分析一次、生成一份摘要，调用点直接代换被调者的摘要，不展开被调者的函数体。
层与层之间是**加法**，不是 inline 那样的**乘法**。报告 §15：Infer 200 层调用链 **0.76 秒**，
摘要条数严格等于函数个数，内存 178→189 MB；本包自写的 MiniOobChecker 20 层 **0.31 秒**。
路径爆炸在层间被消掉了——这正是立项要解决的那个问题的根因。

**精度：每一层的损失取决于这一层的代码，损失会沿链累积。**

| 这一层做了什么 | 对传上去的条件有什么影响 | 结果 |
|---|---|---|
| 形参原样传递、`i + 1`、`2 * n + 1` 这类线性 / 非线性算术 | **无损**。常数会合并（MiniOob 20 层实测：`arg1+19`，条件长度不随层数增长），非线性项保留为不透明符号项（§3.3） | 到顶层照样能判 L1 |
| 分支合并（`if (x) i += 2;`） | 区间变宽（`i ∈ [k, k+2]`），**每层一次的话会累积** | 可能从 L1 降为 **L2**（仍会报，只是不拦门禁） |
| 循环 | 阈值 widening 后变成区间（§3.4），同上 | 同上 |
| 调用了**没有摘要也没有模型**的函数（外部库、未进 cdb 的模块） | 相关的值变成 Top，**依赖它的条件在这一层断掉** | 漏报 → 靠模型表（§3.7）补 |
| 函数指针调用 | 第一版只处理同函数内的常量赋值（§6） | 其余情况断链 |
| 递归（SCC） | 环上返回保守摘要（§3.9） | 穿过递归的链断掉 |
| 链的**最顶层**（数组大小和下标都已知的那个函数）**不在分析范围内** | 条件一路 `StillOpen` 传到入口也等不来实参 | 不报（这是对的：不能瞎报）→ 分析范围要覆盖整条链 |

**所以"17 层"不是问题，"17 层里有一层调了个没建模的外部函数"才是问题。**
这也是模型表（§3.7）和全量分析范围比"加深度"更重要的原因。

**工程上唯一和层数直接相关的一点**【推断】：§3.9 的按需递归，分析栈的深度 = 调用链深度。
C++ 里几百层递归本身没问题，但每一帧如果在栈上放了大对象就有风险。
实现时二选一：SummaryGen 的状态全部放堆上；或者先对调用图做一次后序遍历，**自底向上排好顺序再逐个分析**，
彻底去掉递归（这也是 M4 并行化要用的调度方式）。`provenance` 计数用 `uint8_t` 会在 255 层饱和，
只影响排查信息，不影响判定。

### 通道分工：摘要通道只报现有规则看不到的

引擎仍然给**所有**函数生成摘要（第 1、2 层的摘要是第 3 层以上判定的积木，不能省），
但**报告**的时候按条件实际穿过的层数过滤（§3.6）：

```
条件从访问点到判定点穿过的调用层数  ≤ inlineDepth   →  不报（现有规则负责，而且它是路径敏感的，更准）
                                    >  inlineDepth   →  报（这正是立项要补的漏报）
```

`inlineDepth` 可配置，默认取你们现有规则的 inline 上限（2）。另留一个**评估模式**：全部都报并打上层数，
用来和现有规则逐条对比（`03-` T1-5），确认两边在 1–2 层上的结论一致、没有互相打架。

**一个例外——CSA 的已知盲区不过滤**：T0-1b 在 fork 上实测，`p = &a[2]; p[j]`、`x.m[1][j]` 这类写法（§3.1 #21、#18）
CSA 连地址都算不出来，**现有规则在 1–2 层也收不到回调**。这类条件由 AccessPath 在生成时打上 `csaBlindSpot` 标记，
**不管几层都报**，否则它们会落进两条通道之间的缝里。

---

## 1. 对报告 §25.2 的一处修正（重要）

报告 §25.2 原来写的是：

> **CSA 端**对循环只做有限次展开，采集"下标与循环变量的关系"这类**事实**；
> **FixpointEngine 端**拿这些事实做递推式外推。

**这条现在要改。** 理由来自附录 D 的证据链：

如果**主要事实源**是 CSA 的 ExplodedGraph，那么 CSA 放弃探索的每一个地方——
循环块访问次数到顶、路径预算耗尽、inline 深度到顶、`ProgramState` 被 GC——
都会**没有事实可采**，于是没有摘要，于是又回到 `unsupported`。
**你们现在最痛的失败模式会被原封不动继承过来。**

正确的结构是把主次倒过来：

```
主：独立 AI（对 Clang CFG，非路径敏感）  ——  全函数性是设计约束，任何函数都产出摘要
                                             精度不够时降级为 Top，绝不缺席
副：CSA 路径敏感信息                      ——  可选的精度增强，不参与"有没有摘要"的判定
```

**副的具体用法**：AI 判出一条 `L2（可能越界）`之后，再去问 CSA
"这次访问发生在什么路径约束下"，把结果作为**解释与排序依据**附在告警上
（"仅当 `flag != 0` 时发生"），而**不是**作为判定的必要输入。
拿不到就不附，告警照报。

这样做的代价是显式的：**AI 的区间域是非析取的，拿不到路径相关的精度**，
所以像 `if (f) i = 20; if (!f) a[i];` 这种，AI 会 join 成 `i ∈ [3, 20]` 报 L2，
而现有 CSA 规则能精确判定为安全。**这正是 §25.3 说的"摘要通道精度天然低于 inline 通道"**——
两条通道并存，各报各的，不要试图让摘要通道取代现有规则。

> ~~**证伪条件**：如果实测发现你们的越界 unsupported 里，**循环导致的占比很低**
> （比如 < 20%），那这条修正的前提就不成立，方案应该回到"CSA 主采、AI 辅助"。测法见 T0-4。~~
>
> **v3.6：这条不再需要测。**【立项输入 + 推断】立项明确了现有漏报的根因是**跨多层调用时的路径爆炸**。
> 路径爆炸的结果就是"路径预算耗尽、CSA 放弃探索"——正是上面说的"没有事实可采"的那一类。
> 原来的证伪条件只盯着"循环"，写窄了；真正的判据是"CSA 会不会在这些场景放弃探索"，
> 立项输入已经回答了：会。所以主次倒置成立，**事实源不能是 ExplodedGraph**。

---

## 2. 组件图

```
                        ┌─────────────────────────────────────────┐
                        │  ExtentModel  （P-1，最先做）             │
                        │  AccessPath → 元素个数 / 字节大小          │
                        │  只依赖 ASTContext + 声明类型 + 分配点表达式 │
                        │  不依赖 ProgramState ⇒ AI 和 CSA 都能用     │
                        └───────────────┬─────────────────────────┘
                                        │
   ┌────────────────────────────────────┴───────────────────────────────┐
   │                                                                     │
   ▼                                                                     ▼
┌──────────────────────────────┐                        ┌──────────────────────────────┐
│ SummaryGen（新建，独立 AI）    │                        │ 现有 CSA 引擎（几乎不动）       │
│  · 输入：clang::CFG           │                        │  ExplodedGraph 路径敏感符号执行 │
│  · BoundDomain：区间+符号Bound │                        │  现有几十个 Checker 原样工作    │
│  · 阈值 widening / narrowing  │                        └──────────────┬───────────────┘
│  · 全函数性：必产出摘要         │                                       │
└──────────────┬───────────────┘                                       │
               │ OobSummary                                             │
               ▼                                                        │
┌──────────────────────────────┐                                       │
│ SummaryDB（SQLite）           │                                       │
│  summaries 表：一分析一列       │                                       │
│  deps 表：谁读了谁的摘要         │                                       │
└──────────────┬───────────────┘                                       │
               │                                                        │
               ▼                                                        │
┌──────────────────────────────┐        ┌──────────────────────────┐   │
│ SummaryManager               │        │ SummaryApplier           │   │
│  On-Demand 递归 + 环检测       │───────▶│  subst 形参→实参          │   │
│  两层 LRU 缓存                 │        │  classify L1/L2/S2/Open  │   │
└──────────────────────────────┘        └────────────┬─────────────┘   │
                                                     │                  │
                                                     ▼                  ▼
                                        ┌────────────────────────────────────┐
                                        │ OobSummaryChecker（新建，很薄）      │
                                        │  checkPreCall：取被调摘要、代换、判定  │
                                        │  出告警；可选向 CSA 要路径约束做解释   │
                                        └────────────────────────────────────┘
```

---

## 3. 逐组件设计

### 3.1 ExtentModel —— 优先级 P-1，排在一切之前

**这是附录 D 得出的最重要一条：内存建模的覆盖面比分析算法更决定召回率。**
Infer 4 个漏报里有 3 个出在"某类基址拿不到 size"，不是算法不够强。

**唯一对外接口**：

```cpp
/// 返回该访问路径所指数组的 extent（元素个数）；拿不到就是 Unknown，
/// 但**必须明确区分** Unknown（不知道）与 Symbolic（知道是个符号）。
ExtentResult getExtent(const AccessPath &Base, ASTContext &Ctx);

struct ExtentResult {
  enum class Kind { Concrete, Symbolic, Unknown } kind;
  llvm::APSInt          concreteCount;   // Concrete
  BoundExpr             symbolicCount;   // Symbolic（形参数组、malloc(n*sizeof)）
  CharUnits             elemSize;
  UnknownReason         why;             // Unknown 时必须填，用于统计覆盖率
};
```

**必须覆盖的清单**（这张表就是验收标准，每一行一组测试用例）：

| # | 形态 | extent 来源 | 备注 |
|---|---|---|---|
| 1 | 局部数组 `int a[10]` | 声明类型 `ConstantArrayType` | |
| 2 | **局部 struct 的字段数组** `S s; s.d[i]` | 递归 `RecordDecl` 字段 | **Infer 在这里漏，我们不能漏** |
| 3 | **`static` 局部 struct 字段数组** | 同上 | 同上 |
| 4 | **全局 struct 字段数组**（有无初始化器都要） | 同上 | 同上 |
| 5 | **嵌套 struct** `o.in.d[i]` | 递归 | 同上 |
| 6 | **struct 数组的元素字段** `arr[1].d[i]` | 递归 | 同上 |
| 7 | union 成员数组 | 递归 | 同上 |
| 8 | 全局数组**无初始化器** `int g[10];` | 声明类型，**不依赖初始化器** | **Infer 在这里漏** |
| 9 | `extern` 声明、别处定义的全局 | 跨 TU 查 USR；查不到 → Unknown(ExternNoDef) | |
| 10 | 形参 `int *p` / `int a[10]` | 符号 extent `extent(param#k)` | 退化为指针，extent 是符号 |
| 11 | 形参 `struct S *p` 的字段数组 | 符号基址 + 字段声明类型 → **具体** | |
| 12 | `malloc(n * sizeof(T))` | 解析分配点实参表达式 | 常见形态优先：`n*sizeof(T)`、`sizeof(T)*n`、`sizeof(S)`。**CSA 要开 `unix.DynamicMemoryModeling` 才把大小放进状态**（T0-1 那次运行没开，T0-1b ②） |
| 13 | `calloc(n, sizeof(T))` | 同上 | |
| 14 | `realloc` | 重新绑定 | |
| 15 | **你们内部的自定义分配器**（`VOS_MemAlloc` 及其封装） | **现有 `config.ini` 的 `MEM_ALLOC` + `alloc_free_identity` 识别结果**（v3.7，见 §3.7）；封装函数的**大小参数由摘要自底向上推出**，不靠参数名 | 工业代码里这条往往比 malloc 重要。**只需给最底层分配器补一个"第几个参数是字节数"** |
| 16 | VLA `int a[n]` | 符号 extent `n` | |
| 17 | 柔性数组成员 `int d[]` / `int d[0]` / `int d[1]` | 单函数内**只能 Unknown**，但要**显式标记**；真实长度通常在**调用者**的 `malloc(sizeof(S)+n*sizeof(T))` 里 → 摘要可补 | 常见的"假数组"，误判会造成大量误报。**CSA 14 现状**【实测】：`[]`/`[0]` → Unknown；`[1]` 默认按 1 个元素算（count=1），需 `consider-single-element-arrays-as-flexible-array-members=true` 才变 Unknown —— 要定一个显式策略 |
| 18 | 二维 / 多维 | 逐维 | **只能自己做**【实测】：CSA 14 的比较基准是整个数组（`x.m[0][5]` 越出第 0 行不报）；内层下标是符号时连地址都没有（见 #21） |
| 19 | 字符串字面量 `"abc"` | 长度 + 1 | |
| 20 | 函数返回的指针 | 查被调摘要的 `ReturnEffect` | 跨过程。**v3.7：分配语义这一半（"返回的是新申请的堆内存，大小 = 某个形参的表达式"）提前到 P0**，是 #15 封装分配器的前提；一般返回值区间仍 P1 |
| **21** | **指向数组中间的指针再用符号下标** `p = &a[2]; p[j]`、`p = a + k; p[j]`、`x.m[1][j]` | 由 **AccessPath** 表示成 `(a, 偏移 2)` / `(a, 偏移 k)`，再与下标相加 | **v3.4 新增，T0-1b 实测（上游 14.0.6 与 fork 均复现）**：CSA 14 在这里返回 `UnknownVal`（`StoreManager::getLValueElement`），`checkLocation` **根本不被调用**；语义相同的 `*(p + j)` 却正常。受影响的恰好是"下标还是符号"的情形 —— 正是摘要要推 `requires` 的情形。**不能读 ExprEngine 的地址，必须自己算** |

**【假设 A1】你们的 CSA（LLVM 14 fork）里 `MemRegionManager::getStaticSize()` 可用，
且对 `FieldRegion` 能给出正确 size。** 若成立，#1–#8、#11 基本可以直接借用
CSA 现成的规则（声明类型 → 大小、柔性数组识别），ExtentModel 只需做 AccessPath ↔ MemRegion 的映射与
#12–#17、#21 的补充，工作量减半。**若不成立，这些要自己从 `ASTContext` 的声明类型重写。**
→ 测法见 `03-assumptions-and-test-plan.md` **T0-1 / T0-1b**。

> **T0-1 进展（2026-09-22）**【实测数据 + 源码确认】：
> * `getStaticSize` 在 fork 里**存在**、签名与上游一致；对象层 **8/8 具体值正确**（含 Infer 漏报的全局无初始化器），
>   fork 的输出与上游 LLVM 14.0.6 **逐行一致**。
> * 字段层（A1 本身）**已在 fork 上实测成立**（T0-1b，2026-09-22）：7 种字段数组形态（局部 / static / 全局 / 嵌套 /
>   struct 数组元素 / 形参 struct 指针 / 非末尾字段）全部返回 `FieldRegion`、32 字节、8 个元素；
>   fork 的 19 行探针输出与上游 LLVM 14.0.6 逐行一致。
> * 同时暴露了 **CSA 借不上的三处**：#18 多维只到整数组级；#21 指向数组中间的指针 + 符号下标连地址都没有；
>   #17 柔性数组 `[1]` 的默认处理会引发误报。三处都已写进上表"备注"列。
> * **边界要写清楚**：这里"借用 CSA"指的是借它**从类型算大小的规则**（`getStaticSize` 的逻辑与柔性数组识别），
>   **不是**借 ExprEngine 算出来的地址 —— #21 说明后者在关键场景下是 Unknown。
>   SummaryGen 本来就不走 ExprEngine（§7），这条边界跟它是一致的。

> **顺带一个对你们有利的事实**：CSA 的 RegionStore **天生**把结构体字段建模为
> `FieldRegion`，带完整类型信息——**这正是 Infer 缺的那一块**。
> 也就是说在"内存建模覆盖面"这个最决定召回率的维度上，
> **你们的起点比 Infer 高**，而不是低。这是方案 C 成立的一条硬理由。

---

### 3.2 AccessPath —— 跨函数稳定的基址表示

一切的地基。要求：**可序列化、跨 TU 稳定、可被实参代换**。

```cpp
struct AccessPath {
  enum class Root : uint8_t { Param, Global, Alloc, Return, Unknown };

  Root      root;
  unsigned  paramIdx   = 0;    // Root::Param —— 形参序号，代换的锚点
  StringRef globalUSR;         // Root::Global —— 用 USR 不用名字，跨 TU 才稳定
  AllocSiteId allocSite;       // Root::Alloc  —— (procUID, stmtId) 二元组

  // Field(name) | Deref | Index(BoundExpr)
  SmallVector<Step, 4> steps;

  uint64_t hash() const;       // hash-cons，DenseMap 的 key
};
```

**关键决策：全局和函数都用 USR 做 key**，不用名字。
Clang 有 `clang::index::generateUSRForDecl()`，这是跨 TU 唯一稳定的标识。
用名字会在 `static` 同名符号、模板实例、匿名命名空间上出错。

**代换（subst）就是在这个结构上做**：调用点 `f(a, k+1)` 处，
把被调摘要里所有 `Param#0` 替换成实参 `a` 的 AccessPath，
`Param#1` 替换成 `k+1` 的 BoundExpr。这正是 Infer
`ConditionSet.subst` 做的事（报告 §12.1），也是 MiniOobChecker 里跑通过的那段。

---

### 3.3 BoundExpr / Range —— 表示能力优先于求解能力

对标 Infer 的 `Itv` + `Bound.t`（含 `mk_MultB` 保留非线性项，报告 §5.3）。
**设计原则：宁可"表示得出但算不动"，不可"表示不出只好丢弃"。**

```cpp
struct BoundExpr {
  llvm::APSInt konst;                                  // 常数项
  llvm::SmallDenseMap<SymbolId, llvm::APSInt, 4> lin;  // Σ cᵢ·symᵢ  线性主体
  SmallVector<OpaqueTerm, 2> opaque;                   // a*b、a/b、a%b、a<<b、call(…)
  bool top = false, bottom = false;
};

struct Range {
  BoundExpr lo, hi;
  bool loIsNegInf = false, hiIsPosInf = false;
  SmallVector<llvm::APSInt, 4> wideningThresholds;     // ★ 见 §3.4
};
```

`opaque` 那一项是关键：`n * 2` 不要算成 Top，要原样保留成一个不透明符号项，
这样 `2·n + 1 < extent(p)` 这样的条件才能一路传到顶层再判定
（报告 §13.3 的三层代换实测就是这个效果）。

**v3.10 补：条件要能带守卫**（`04-` S3.2 暴露的缺口）。

```c
static void L3(UINT32 *p, UINT32 i) { if (i >= 32) return; p[i] = 0; }
```

访问点上 `i` 的数值区间是 `[0, 31]`，但**条件不能只留这个区间**：写成 `4·31 + 4 ≤ extent(p)` 的话，
链顶传常量 `3`、数组只有 16 个元素的调用点也会被判越界（误报）。两种表示任选其一：

* **守卫蕴含**：`SafetyCondition` 带一个 `guard`（线性约束的合取），条件写成 `i < 32 ⇒ 4i + 4 ≤ extent(p)`；
  代换时先代守卫，守卫为假的直接 `Safe`，为真的再判条件，判不了的连同守卫一起上传。
* **min/max 界**（Infer `Bound.t` 的做法）：`hi = min(i, 31)`，`BoundExpr` 加一个 `MinMax` 变体。

倾向前者：守卫本来就是 CFG 分支条件，收集成本低，而且代换后常常直接变成常量真假。

---

### 3.4 SummaryGen —— 独立抽象解释器

**输入** `clang::CFG`（`AnalysisDeclContext::getCFG()`），**不碰 `ExplodedGraph`**。

**核心循环**（标准抽象解释，报告 §9）：

```
1. 计算 WTO（或退化为 RPO + 识别 loop header）
2. 逐块 transfer，块间 join
3. loop header 上：前 K 次 join，之后 widen
4. widen 到不动点后，narrow 若干轮回收精度
5. 无论如何在 max_iters 后停止 → 结果是合法的（可能很粗的）摘要
```

**必须抄的那 10 行 —— 阈值 widening**（报告 §9.3，`bufferOverrunDomain.ml:392-398`）：

```cpp
// 每当一个值与常量 z 做比较（CFG 的 terminator condition），
// 就把 z-1, z, z+1 收进该值的 widening 阈值集合。
// widen 时外推到"不小于当前值的最小阈值"，而不是直接到 +∞。
void collectThreshold(Range &R, const llvm::APSInt &Z) {
  R.wideningThresholds.push_back(Z - 1);
  R.wideningThresholds.push_back(Z);
  R.wideningThresholds.push_back(Z + 1);
}
```

实测效果（报告 §9.2）：`i: 0 → [0,1] → [0,9] → [0,10] → 不动点`，
而不是 `[0, +∞]`。**这是"循环不用 unsupported"的全部秘密，不到 10 行。**

**全函数性（totality）是硬约束**。任何情况下都必须产出摘要：

| 情况 | 产出 |
|---|---|
| 正常收敛 | `status = Analyzed`，条件集完整 |
| 迭代超上限 | `status = Partial(IterLimit)`，条件集为已算出的部分 |
| CFG 过大 | `status = Skipped(CfgTooLarge)`，**条件集为空但摘要存在** |
| 超时 | `status = Skipped(Timeout)` |
| 有不认识的构造 | 该表达式降为 Top，**其余照常**，`status = Analyzed` |

**"没分析"和"分析了没问题"必须能区分**——这是附录 D 的第 4 条启示，
也是 Infer 做得不好的地方（`--bo-max-cfg-size` 跳过的函数只在日志里留一行，
`report.json` 里毫无痕迹）。`status` 字段落盘，CI 里把 `Skipped` 比例当质量指标跟踪。

---

### 3.5 SummaryIR

```cpp
struct SafetyCondition {          // "requires"：尚未判定的安全义务
  AccessPath base;                // 被索引的数组
  Range      offsetElems;         // 以元素为单位的下标范围
  ExtentResult extent;            // 来自 ExtentModel
  SourceLocation accessLoc;       // ★ 始终指向【真正那次访问】，代换时不变
  ReachCond  reach;               // 可选：可达性/路径约束，用于分级与解释
  uint8_t    provenance;          // 该条件经过了几层代换（= 访问点到当前函数的调用层数），
                                  // v3.6 起也用于通道分工的层数过滤（§0.1、§3.6）；255 饱和
  bool       csaBlindSpot = false;// v3.6：基址形态属于 CSA 已知盲区（§3.1 #18 行级、#21），不做层数过滤
};

struct OobSummary {
  ProcUID  uid;                   // 用 USR
  std::vector<SafetyCondition> openConditions;   // 主体
  ReturnEffect                 ret;              // 返回值范围 / 是否是形参别名
  std::vector<OutParamEffect>  outParams;        // 通过指针形参写出去的
  std::vector<GlobalEffect>    globals;          // ★ 全局一等公民（Infer 的反面教材）
  AliasSummary                 alias;            // 粗集合近似即可
  AnalysisStatus               status;           // ★ Analyzed/Partial/Skipped(reason)
  uint64_t                     cfgStructHash;    // 增量失效判定
};
```

**`accessLoc` 在代换过程中保持不变**——这是报告 §13.3 实测到的关键细节：
`mid_fetch` 的条件写着 `k + 12 < 10`，位置却指向 `access.c:3:44`。
没有这一条，跨三层的告警就无法精确归因。

**`GlobalEffect` 单列**，不要塞进 `outParams`。这是 §25 反面借鉴的直接落地。

---

### 3.6 SummaryApplier —— 代换与分级

```cpp
// 调用点：f(a, k+1)
SubstMap M = buildSubst(CallSite);       // Param#0 ↦ AccessPath(a), Param#1 ↦ BoundExpr(k+1)

for (auto &C : CalleeSummary.openConditions) {
  SafetyCondition C2 = subst(C, M);      // 代换；accessLoc 不变；provenance++
  switch (classify(C2)) {
    case Safe:      break;                            // 丢弃
    case L1:        reportIfDeep(C2, Severity::Definite);   // 确定越界
    case L2:        reportIfDeep(C2, Severity::Possible);   // 区间部分重叠
    case S2:        reportIfDeep(C2, Severity::Symbolic);   // 符号可疑（谨慎开）
    case StillOpen: CallerSummary.openConditions.push_back(C2);  // ★ 继续往上传
  }
}
```

```cpp
// v3.6：通道分工（§0.1）。1–2 层交给现有规则，摘要通道只报它看不到的
void reportIfDeep(const SafetyCondition &C, Severity S) {
  if (Cfg.evalMode) { report(C, S); return; }          // 评估模式：全报 + 标层数，用于和现有规则对比
  if (C.provenance <= Cfg.inlineDepth && !C.csaBlindSpot) return;   // 默认 inlineDepth = 2；CSA 盲区例外
  report(C, S);
}
```

**v3.10 补：访问点先符号化简、再代入区间**（`04-` S3.7 暴露的缺口）。

```c
memcpy_s(b + off, total - off, s, n);   /* 条件：total - off <= extent(b) - off */
```

生成条件时，局部变量（这里是循环变量 `off`）**先作为符号保留**，两边同类项抵消后得到 `total ≤ extent(b)`，
**再**把剩下的局部符号换成它们在这个程序点的区间。反过来先代区间（`off ∈ [0, +∞)`）的话，两边的 `off` 失去关联，
"按剩余长度拷贝"这种最常见的安全写法会误报成片。形参符号照常保留、往上传。

**`StillOpen → 继续往上传` 这一条是精确率的命根子。** 附录 D 实测：
Infer 默认档精确率 100%，靠的就是 `SymbolicIssue(propagate=true, 不报告)`——
**拿不准的不是丢掉，是继续往上传，等上层有了实参再判**。
这恰恰是"有限层次 inline"结构上做不到的事。

**分级定义**（对标 Infer `bufferOverrunProofObligations.ml`，报告 §12.2）：

| 级别 | 判据 | 默认 |
|---|---|---|
| `Safe` | `offset` 区间完全落在 `[0, extent)` 内 | 不报 |
| `L1` | `offset` 区间**完全**落在 `[0, extent)` 外 | **报，可拦门禁** |
| `L2` | 区间与 `[0, extent)` **部分重叠** | **报，不拦门禁** |
| `S2` | 含符号项，代入后仍无法判定，但**有具体调用点** | 默认关，专项排查开 |
| `StillOpen` | 含符号项且依赖更上层的实参 | **不报，上传** |

---

### 3.7 模型表（库函数 / 内部工具函数 / 自定义分配器）

对标 `bufferOverrunModels.ml`（报告 §12.3）。**性价比最高的一块**：
比让引擎"读懂"实现划算一个数量级，而且是纯配置，非核心开发者也能加。
**v3.6 §0.1 的结论让它更重要了**：多层链上最常见的断链原因就是"调了一个没摘要也没模型的函数"。

#### 3.7.1 v3.7：申请 / 释放函数 —— 直接复用你们已有的两份输入【立项输入】

**输入 ①：开发配置 `config.ini`**（就是 bxen `-rbchecker-config` 指向的那份）

```ini
MEM_ALLOC=VOS_MemAlloc,-1      ; 函数名,哪个位置是申请到的内存：-1 = 返回值（只有申请函数会用 -1）
MEM_FREE=VOS_MemFree,1         ; 形参从 0 开始编号：1 = 第 2 个形参是被释放的内存
```

**输入 ②：规则 `alloc_free_identity` 自动识别出的封装函数**（对开发配置查缺补漏，JSON）

```json
{ "func_name": "VISPADP_IFMNG_Malloc", "func_type": "alloc", "inner_wrapped_func": "VOS_MemAlloc",
  "para": -1, "offset": [], "file": ".../adp_vispadp_ifnet.c", "line": 985,
  "reason": "... call stack is: VOS_MemAlloc(return value is alloc memory)" }
{ "func_name": "BurstPktCntFeature::ReportResult", "func_type": "free", "inner_wrapped_func": "VOS_MemFree",
  "para": 0, "offset": [228],
  "reason": "... PeakTrafficFeature::ClearStatus(in para: 0 ..., offset is 228) --> LRO_MemPool::clear(...offset is 228)
             --> VOS_MemFree(in para: 1 is free memory, offset is 36)" }
```

| 字段 | 在越界摘要里怎么用 |
|---|---|
| `func_name` + `func_type=alloc` | 这是一个申请函数：调用结果是**新的堆对象**，AccessPath 根为 `AllocSite(调用点)` |
| `para` | `-1` → 返回值是新内存（`ReturnEffect`）；`k` → 第 k 个形参带出新内存（`OutParamEffect`，如 `Alloc(void **pp, …)`） |
| `offset` | 新内存挂在**结构体成员**上：`para` 所指对象偏移 `offset` 字节处的字段。转换成字段路径（`Param#k → field`）后，同一调用链下游对 `p->buf[i]` 的访问就能拿到它的大小 |
| `inner_wrapped_func` | 最终落到哪个底层分配器 —— 用来**交叉校验**摘要推出来的结果（见 3.7.2） |
| `func_type=free` / 配对关系 | **越界分析用不到**。释放信息和申请 / 释放配对留给以后的 UAF / 泄漏 / 重复释放类规则（SummaryDB 已经为其它分析预留了列，§3.8）；配对识别需要时再结合大模型 Agent |

**缺的那一块：申请了多大。** 两份输入都只说了"哪个位置是内存"，没说"哪个参数是长度"。
**我们的解法是不去逐个识别封装函数的长度参数，而是只给最底层分配器补这一条，其余由摘要自底向上推出来：**

```
底层模型（人工，全工程只有少数几个）：
    VOS_MemAlloc(…, size)          →  返回 fresh heap，extent = size 字节
    （需确认：VOS_MemAlloc 的第几个形参是字节数。从 MEM_FREE=VOS_MemFree,1 看，
      第 0 个形参很可能是 PID；以头文件为准）【推断】

封装函数（不用配，SummaryGen 分析它的函数体时自动得到）：
    void *VISPADP_IFMNG_Malloc(UINT32 ulLen) { return VOS_MemAlloc(PID, 0, ulLen); }
        →  ReturnEffect: fresh heap，extent = param#0 字节
    void *XX_AllocWithHdr(UINT32 n) { char *p = VOS_MemAlloc(PID, 0, n + sizeof(HDR)); return p + sizeof(HDR); }
        →  ReturnEffect: fresh heap，extent = param#0 + sizeof(HDR)，返回指针偏移 sizeof(HDR)
           （带头部的内存池分配常见这种写法；靠参数名猜长度在这里必错，靠摘要推是对的）
```

这就是 §3.4 的 `ReturnEffect` 和 §3.3 的 `BoundExpr` 本来就要做的事，**多层封装（封装的封装）自动逐层推上去**，
正好也是立项要解决的"多层"——只不过这里多层的是分配而不是访问。

#### 3.7.2 两份输入与摘要推导的关系

| 情形 | 处理 | 置信度 |
|---|---|---|
| 封装函数的函数体在分析范围内 | **以摘要推导为准**；`alloc_free_identity` 的结果用来**交叉校验**：JSON 说是申请函数、摘要却没推出 fresh heap（或反过来），记一条校验日志 | 高 |
| 函数体不在分析范围内（库、未进 cdb），但在 `config.ini` 或 JSON 里 | 当作申请函数，**extent 记为符号 `extent(ret)`（未知大小）**：知道它是堆、不知道多大，**不会误报** | —— |
| 同上，且想要大小 | **P1**：① 按形参名启发（`ulSize` / `ulLen` / `uiLength`…）；② 交给大模型 Agent 识别。结果写进单独的补充配置、标注来源（`heuristic` / `llm`），**人工确认后才参与 L1 判定**，未确认的只用于 L2 | 中 / 低 |
| JSON 的 `offset` | 按**被分析代码同一目标 ABI** 的结构体布局，把字节偏移换成字段路径 | 见下 |

**一个要提前对齐的坑：`offset` 是按哪个 ABI 算的？**【推断】目标是 aarch64_be + ILP32，
上游 clang 14 会把这个目标按 LP64 布局（T0-1c）。结构体里只要有指针或 `long`，
ILP32 与 LP64 下的成员偏移就不同。如果 `alloc_free_identity` 与摘要引擎用的布局不一致，
字节偏移会对到错误的字段上。**建议识别结果里同时带上字段名路径**（如 `"field": "->stPool.pBuf"`），
字节偏移只作校验。

#### 3.7.3 访问类模型：安全函数族（P0）

你们业务代码统一使用 `memcpy_s` / `memset_s` / `memmove_s` 这类安全函数，
**越界访问经常就发生在它们内部，而且缓冲区和长度往往是被一层层传下来的**——正是多层场景。
所以它们的模型是 P0，不是 P1：

| 函数 | 安全条件（字节） | 说明 |
|---|---|---|
| `memcpy_s(dest, destMax, src, count)` | `destMax ≤ extent(dest)`；`count ≤ extent(src)` | `count > destMax` 运行时会被拦下返回错误，**真正的越界是 `destMax` 报大了**，或读 `src` 读过头 |
| `memmove_s` | 同上 | |
| `memset_s(dest, destMax, c, count)` | `destMax ≤ extent(dest)` | |
| `strcpy_s` / `strncpy_s` / `strcat_s` | `destMax ≤ extent(dest)`（`src` 需字符串长度建模，P1） | |
| 你们内部的封装（如有） | 同上，按参数位置配置 | 由 T1-3 的统计决定补哪些 |

#### 3.7.4 配置格式

**不另起一套配置**，读现有 `config.ini`，只为底层分配器新增一种键（旧解析器若不认识未知键会报错，需先确认）：

```ini
; 已有
MEM_ALLOC=VOS_MemAlloc,-1
MEM_FREE=VOS_MemFree,1
; v3.7 新增：底层分配器的"字节数"在第几个形参（只需给最底层的几个，封装函数由摘要推出）
MEM_ALLOC_SIZE=VOS_MemAlloc,2          ; 下标待确认
MEM_ALLOC_SIZE=malloc,0
MEM_ALLOC_SIZE=calloc,0*1              ; 两个形参相乘
```

访问类模型（3.7.3）和其它库函数模型条目较多，放单独的 `summary_models.yaml`：

```yaml
- name: memcpy_s
  kind: access
  conditions:
    - { base: param0, bytes: "param1" }          # destMax ≤ extent(dest)
    - { base: param2, bytes: "param3" }          # count   ≤ extent(src)
- name: memset_s
  kind: access
  conditions:
    - { base: param0, bytes: "param1" }
```

**【假设 A2】你们代码里对内存的访问有相当比例是经由内部封装函数 / 安全函数而不是裸下标。**
安全函数这一半已由编码规范确认；内部封装的比例仍可用 **T1-3** 统计，用来决定补哪些模型。

---

### 3.8 SummaryDB

```sql
-- 对标 Infer 的 specs 表：一种分析一列，加新分析不改表结构、不影响旧分析
CREATE TABLE summaries (
  proc_uid       TEXT PRIMARY KEY,     -- clang USR
  proc_name      TEXT,
  source_usr     TEXT,
  cfg_hash       INTEGER,              -- 逐过程 CFG 结构哈希，用于增量
  status         INTEGER,              -- Analyzed/Partial/Skipped(reason)
  oob_summary    BLOB,                 -- ← 本期只做这一列
  taint_summary  BLOB,                 -- 以后加分析就加列
  npd_summary    BLOB
);

-- 对标 Dependencies.record_pname_dep：记录【实际读取】关系，不是静态调用图
CREATE TABLE deps (
  caller_uid TEXT, callee_uid TEXT, kind INTEGER,
  PRIMARY KEY (caller_uid, callee_uid, kind)
);
CREATE INDEX deps_callee ON deps(callee_uid);
```

**三条必须照抄的经验**（报告 §17 实测踩过的坑）：

1. **依赖边记录的是"谁真的读了谁的摘要"**，不是静态调用图。静态调用图会
   把大量实际没用上的边也算进去，失效范围爆炸。埋点位置只有一处：
   `SummaryManager::get()` 成功返回时。
2. **变更判定用逐过程 CFG 结构哈希**，不是文件哈希。改注释、改同文件里
   别的函数都不该触发重算。对标 `Procdesc.mark_if_unchanged`。
3. **CI 里不要断言"失效了几条"**。报告 §17.2 实测：同一份改动会得到
   `6 of 8` 或 `7 of 8`，取决于历史。**要断言的是"顶层告警是否重现"。**

**序列化格式建议用 FlatBuffers 或 Protobuf，不要用 `Marshal` 式的裸内存 dump。**
Infer 用 OCaml `Marshal`（魔数 `0x8495A6BE`），结果是外部工具完全无法消费
（报告 §7.2）。你们的摘要将来大概率要被 LLM pipeline、报表、IDE 插件读，
**跨语言可读性值钱**。代价是几百行 schema 与转换代码。

---

### 3.9 SummaryManager

对标 `ondemand.ml`（报告 §6）。**不需要全局拓扑排序**，按需递归即可：

```cpp
const OobSummary *SummaryManager::get(ProcUID Callee, ProcUID Caller) {
  if (auto *S = L1Cache.lookup(Callee))  { recordDep(Caller, Callee); return S; }
  if (auto *S = L2Cache.lookup(Callee))  { recordDep(Caller, Callee); return S; }
  if (auto S = DB.load(Callee))          { cache(S); recordDep(Caller, Callee); return S; }

  if (Active.contains(Callee))                       // ★ 环检测
    return recursionFallback(Callee);                //   返回保守摘要，不递归
  if (Active.size() >= MaxChainDepth)                // 可选的深度阀
    return conservativeSummary(Callee);

  Active.insert(Callee);
  auto S = SummaryGen::run(Callee);                  // 递归分析
  Active.erase(Callee);
  DB.store(Callee, S); cache(S); recordDep(Caller, Callee);
  return &S;
}
```

**默认不设最大深度**。报告 §15 实测：Infer 的 `ondemand_callchain_limit` 是可选项、
无默认值，200 层调用链 0.76 秒、摘要数与过程数严格线性、内存几乎不增长。
**深度不是瓶颈，不要先入为主地加限制。**

**递归（SCC）的处理**：第一版用最保守的办法——环上返回"什么都不保证"的摘要
（所有条件 `StillOpen`、所有 effect 为 Top）。报告 §13 实测 Infer 在简单递归上
能报 L1，靠的就是 `ActiveProcedures` 环检测 + 保守回退，**不需要求 SCC 的不动点**。

---

### 3.10 消费侧：OobSummaryChecker

一个很薄的 CSA checker，**不改现有任何 checker**：

```cpp
class OobSummaryChecker : public Checker<check::PreCall, check::EndOfTranslationUnit> {
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
    if (!FD) return;                                     // 函数指针：见下
    const auto *S = Mgr->get(usrOf(FD), currentProcUID(C));
    if (!S) return;
    auto Subst = buildSubst(Call, C);                    // 实参 → BoundExpr/AccessPath
    for (auto &Cond : S->openConditions)
      handle(subst(Cond, Subst), C);
  }
};
```

**为什么是 `checkPreCall` 而不是别的**：这里能同时拿到 `CallEvent`（实参的 `SVal`）
和 `CheckerContext`（当前 `ProgramState`），既能做代换，又能顺手拿到路径约束
去做 §1 说的"精度增强"。

**函数指针**（Infer 的第三个机制性漏报，报告 §14.4）：
第一版只做最保守的处理——**同一函数内被常量赋值的函数指针**，
沿 `ProgramState` 反查 `SVal` 是不是 `FunctionTextRegion`，是就取那个 `FunctionDecl`。
这能覆盖相当一部分实际代码，成本很低。真正的指向集合分析留到后面立项。

**告警指纹**：报告附录 C 把"issue hash / reportdiff"列为**高优先级未展开项**。
CI 落地必须有稳定指纹。建议 `hash = H(checkerName, 归一化后的 accessLoc, 归一化的消息模板, 函数 USR)`，
**不含行号**（行号会因无关改动漂移），用"函数内第几个同类访问"代替。

---

### 3.11 跨 TU 怎么跑（v3.7 补：落地时绕不开的执行模型）

§3.9 的按需递归有一个隐含前提：**分析调用者时，被调者的函数体拿得到**。
在一个 TU 里成立；但 Clang 是一个 TU 一个 TU 解析的，**被调者在另一个 `.c` 里时，它的 AST 根本不在内存里**。
多层链跨文件是常态（本包三文件 demo 就是），这件事必须在 M0 定下来。两条路：

| | 方案 A：多轮 TU 扫描 + 摘要库驱动的工作表 | 方案 B：先导出中间表示，再离线求解 |
|---|---|---|
| 做法 | 每轮对"待算"的 TU 跑一遍（`check::ASTCodeBody` 型 checker，只建 CFG 不走 ExprEngine）；同 TU 的被调者按需递归，**跨 TU 的被调者查 SummaryDB**，查不到就把调用者记为 `Pending(callee)`；一轮结束后，从 DB 算出"被调者已就绪的 Pending 函数所在的 TU"作为下一轮的工作表，直到清空或达到轮数上限 | 第一遍逐 TU 把每个函数的简化 IR（访问、形参流向、调用及实参表达式、循环头）序列化进库；第二遍一个独立进程读库、建全局调用图、按后序自底向上求解，不再碰 Clang |
| 对标 | 相当于把 Infer 的按需分析按 TU 边界拆成多轮 | 就是 Infer 的 capture → analyze |
| 新增工作 | 一个驱动脚本（生成过滤后的 `compile_commands.json` 喂给 bxen 全量模式）+ DB 里的 `Pending` 状态 | 一套 IR schema、序列化 / 反序列化、离线求解器 |
| 轮数 / 代价 | 轮数 ≈ 链上**跨 TU 的**层数（17 层链里通常只有一部分跨文件）；第 2 轮起只跑工作表里的 TU，规模快速收缩 | 解析只一遍；求解纯内存 |
| 能复用什么 | bxen、`Checkers.td` 注册、现有全量运行流水线全部复用 | 需要新的运行入口 |
| 与增量（M4）的关系 | 够用，但每次都从解析开始 | 天然适合增量（只重导出改过的 TU） |

**建议：12 月 15 日前走方案 A，方案 B 放到 2027 年和增量分析一起做。**【推断】
理由是方案 A 几乎不需要新基础设施，能最快让"跨文件多层"跑起来；它的代价（多轮解析）可以先量出来——
你们现在一次全量 CSA 运行的耗时就是上界（摘要这一遍只建 CFG、不做路径敏感探索，只会更快）。
**W04 第一件事就是量一次"只建 CFG 的全量空跑"要多久**，据此确定轮数上限。

**报告从哪出**：由 §3.6 的 Applier 在**摘要生成这一遍**里判定、写入 DB 的 `reports` 表（按 §3.10 的指纹去重，
多轮里重复判出的同一条只留一份），最后一步统一导出成 bxen 现有告警格式。
§3.10 的 CSA 侧 `OobSummaryChecker`（`checkPreCall`）退到 P1：只用来给已报出的告警补路径解释，
**不参与"报不报"** —— 否则顶层调用者自己路径爆炸时，调用点根本走不到，又回到老问题。

```sql
-- §3.8 的 SummaryDB 为方案 A 增加的两处
ALTER TABLE summaries ADD COLUMN pending_callees TEXT;   -- status = Pending 时，等的是谁（USR 列表）
ALTER TABLE summaries ADD COLUMN tu_file TEXT;           -- 这个函数定义在哪个 TU，用来生成下一轮工作表
CREATE TABLE reports (
  fingerprint TEXT PRIMARY KEY,  -- §3.10 的告警指纹
  severity INTEGER, depth INTEGER, csa_blind_spot INTEGER,
  access_loc TEXT, report_proc TEXT, trace BLOB, first_round INTEGER
);
```

---

### 3.12 v3.11 补充：效果词表、条件效果、报告口径、账本

来自 `05-` 的分级用例，补四条设计约定：

1. **效果（Eff）用固定词表**，自底向上总是算这几类，不等调用方来要：
   返回 / 出参指针指向的新对象及其大小；整数返回值、出参的取值范围或与形参的线性关系；
   经形参能到达的结构体字段（深度 ≤ 2）被写成什么；被写的全局。按需计算（调用方有未决条件引用到某个输出时再算）留作优化（`05-` §5 方案 C）。
2. **条件效果**：`ret = 常量 ⇒ 关于形参的线性约束`（校验函数，S4.6）。生成：返回语句所在路径上的守卫投影到形参；
   应用：调用方在"对返回值分支"之后的那一侧收窄实参的区间。可以沿返回值继续上传（`Cell_CheckPara` → `Cell_IsValidId`）。
3. **报告口径**（与 `04-` §1.3 一致，在此固化成规则）：L2 只认**代码给出的有限上界**——校验、掩码、分支常量、循环界、返回值范围、守卫。
   以下几种**不报、记账**：上界只来自类型范围（`UINT16` 的 65535）；上界是 widening 出来的 ∞（指针步进、跨轮累计）；条件含不透明的非线性项；
   条件到入口仍含符号。
4. **账本（Open）原因枚举**：`fptr`、`unknown_callee`、`recursion`、`msg_edge`、`global_value`、`global_ptr_target`、`field_written_by_callee`、
   `alloc_size_unknown`、`flexible_array`、`struct_overlay`、`pointer_stepping`、`string_length`、`nonlinear`、`type_range_only`、`entry_unproven`、
   `cond_truncated`。W10 上真实代码后按这个枚举统计，决定 2027 先补什么；也是 AI + 程序分析的输入。

---

## 4. 借鉴清单 —— 每条对应 Infer 的哪里

| # | 借鉴什么 | Infer 出处 | 报告章节 | 优先级 |
|---|---|---|---|---|
| 1 | Requires 式摘要（安全条件作主体） | `ConditionSet` | §12.1 | **P0** |
| 2 | 形参→实参代换 + 位置保持不变 | `ConditionSet.subst` | §13.3 | **P0** |
| 3 | **StillOpen 继续上传，不丢弃** | `for_summary` / `SymbolicIssue` | §12.2 | **P0** |
| 4 | 比较常量 → widening 阈值 | `bufferOverrunDomain.ml:392-398` | §9.3 | **P0** |
| 5 | 含符号/非线性项的区间域 | `Itv` / `Bound.mk_MultB` | §5.3 | **P0** |
| 6 | Payload-per-Analysis（一分析一列） | `PayloadId.database_fields` | §4.2 | P1 |
| 7 | On-Demand + 环检测 + 两层缓存 | `ondemand.ml` | §6 | P1 |
| 8 | 依赖边记录**实际读取** + 传递失效 | `record_pname_dep` | §17 | P1 |
| 9 | 逐过程 CFG 结构比对判断变更 | `Procdesc.mark_if_unchanged` | §17.3 | P1 |
| 10 | L1/L2/S2 分级告警 | `bufferOverrunProofObligations.ml` | §12.2 | P1 |
| 11 | 库函数模型表 | `bufferOverrunModels.ml` | §12.3 | P1 |
| 12 | 不设最大调用深度 | `ondemand_callchain_limit` 无默认 | §15 | P0（省事） |

**反面借鉴**（Infer 做错的，我们不要重蹈）：

| # | 教训 | 证据 |
|---|---|---|
| R1 | **extent 必须是内存模型的一等属性、在声明处建立**，不能绑在初始化器过程的摘要上 | 全局数组漏报，报告 §14.3 |
| R2 | **声明处不能对聚合类型止步**，必须递归进 struct/union 字段 | `decl_local_loc` 的 `Tstruct` 分支，附录 D §3.1 |
| R3 | 函数指针要专门建模，不能降级成"未知调用" | 报告 §14.4 |
| R4 | **"没分析"要和"分析了没问题"在产物里分开**，不能只写日志 | `--bo-max-cfg-size` 静默跳过，附录 D |
| R5 | 摘要磁盘格式要跨语言可读 | OCaml `Marshal`，报告 §7.2 |
| R6 | 不要用放宽报告等级去换召回 | 附录 D：+1 TP 赔 13 个文件 FP |

---

## 5. 假设清单（每条都要先验证）

| 编号 | 假设 | 若不成立 | 测法 |
|---|---|---|---|
| **A1** | CSA 的 `getStaticSize()` 对 `FieldRegion` 可用且正确 | ExtentModel 工作量 ×2 | ✅ **已验证**（T0-1 + T0-1b，2026-09-22）：fork 上对象层 8/8、字段层 7/7 正确，19 行输出与上游 LLVM 14.0.6 逐行一致 |
| **A2** | 内存访问有相当比例经由内部封装函数 | 模型表收益下降，降到 P2 | T1-3 |
| ~~**A3**~~ | ~~越界 unsupported 里循环占主要比例~~ | — | **v3.6 不再需要**：立项输入明确根因是跨多层时的路径爆炸，§1 的主次倒置据此成立（见 §1 末尾） |
| ~~**A4**~~ | ~~真实越界有相当比例跨 ≥3 层调用~~ | — | **v3.6 不再需要**：已立项，目标即 2–17 层的多层场景，与占比无关 |
| **A5** | 你们有稳定的 `compile_commands.json` 覆盖全量代码 | 全量分析路径受阻，要先解决构建集成 | ✅ **已验证且已跑通 capture**：10232 个唯一源文件 **100% 落库**、328602 个过程（T1-1） |
| **A6** | 现有 CSA 告警可以按函数 USR 聚合并与摘要通道去重 | 两条通道会出重复告警，需要额外去重层 | T1-5 |

---

## 6. 明确不做的（第一版）

写清楚边界，避免范围蔓延：

* ❌ **不做 SCC 不动点**——递归环上直接保守回退。
* ❌ **不做真正的指向集合分析**——函数指针只做同函数内常量赋值。
* ❌ **不做 C++**——模板、容器、引用、虚函数全部不在第一版范围。
  （附录 D 的全部实测也都是 C。）
* ❌ **不做路径敏感的摘要**——摘要是非析取的。要路径精度就走现有 CSA 规则那条通道。
* ❌ **不追求"摘要通道精度不低于现有规则"**——见 §25.3，这是设计前提不是遗憾。
* ❌ **不替换现有 CTU**——先并存。CTU 是"跨 TU inline"，摘要是另一条路，
  等摘要通道数据好看了再谈是否收掉 CTU。

---

## 7. 与现有 CSA 的关系（一句话版）

| 现有的东西 | 会不会被动 |
|---|---|
| 几十个现有 Checker | **完全不动** |
| `SVal` / `SymExpr` / `ConstraintManager` | 复用，不改 |
| `MemRegion` / RegionStore | **复用"从类型算大小"的规则，且是我们相对 Infer 的优势**（T0-1 / T0-1b：fork 上对象层与字段层均已验证）；**不复用 ExprEngine 算出的地址**（#21：指向数组中间的指针 + 符号下标时是 Unknown） |
| ExplodedGraph / ExprEngine | 不改；摘要生成完全不走这里 |
| `Checkers.td` / bxen / CI | 加一条新 checker 的注册，其余不动 |
| CTU | 并存，暂不动 |
