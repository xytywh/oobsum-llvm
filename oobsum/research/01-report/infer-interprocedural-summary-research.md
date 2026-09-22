# Infer 跨过程分析与函数摘要机制深度调研
## ——面向 CSA 内存越界分析的技术路线评估（独立重做版 v2）

> **v3.7 阅读指引：这是调研阶段的报告，回答"选哪条路"。项目已立项，实施方案与计划以
> `08-summary2-design/00-implementation-plan.md` 为准。** 本报告 §25 / §28 的架构主次与优先级、§28 的中期目标、
> §29 的 PoC 计划写于立项之前，已被那份计划取代（对照表见其 §10）；§5–§7、§9、§12、§13、§15、附录 D 仍是实施时的参考。

> **事实层次标注规则（全文强制）**
>
> | 标记 | 含义 |
> |---|---|
> | **【源码确认】** | 可在下文指明的仓库 commit 中逐行核实，给出文件路径/行号/符号名 |
> | **【实测数据】** | 本次调研在沙箱中**实际执行** Infer 得到的输出，命令与原始输出均给出 |
> | **【文档/Changelog】** | 来自仓库内 `INSTALL.md` / `Changelog.md` / `infer/src/labs/README.md` 或论文 |
> | **【推断/建议】** | 基于以上证据做出的工程判断，**不代表 Infer 已实现的能力** |
> | **【未确认】** | 本次调研没有取得足够证据，如实声明 |
>
> 本版是按"完全重做一份独立报告"的要求，从零重新 checkout 源码、重新设计并重新运行**全部**实验后写成的，
> 不沿用任何早先结论。凡与早先版本结论不一致处，以本版标注的证据为准（§31.1 列出了本版纠正的若干常见误解）。

---

## 0. 调研环境记录（Repository / Branch / Commit / Date）

| 项目 | 值 |
|---|---|
| 仓库 | `https://github.com/facebook/infer` |
| 源码 checkout | `main` 分支，`git clone --depth 1`（浅克隆，故 `git describe` 不可用） |
| **Commit** | **`3d38229b04b649024fee065621f6a4c838f6d029`** |
| Commit 日期 | **2026-09-10 16:15:56 +0100** |
| 实验用可执行文件 | 官方预编译 `infer-linux-x86_64-v1.3.0.tar.xz`（276 MB），`infer --version` → `Infer version v1.3.0` |
| 二进制内置 clang | `clang version 21.1.6 (https://github.com/facebook/infer e09d41e67fc4b14da7deeff59364247cba66db38)` |
| 调研执行日期 | 2026-09-11（UTC） |
| 实验环境 | Linux 6.18 容器，Ubuntu 24.04.4，**2 vCPU / 7.8 GB RAM / 30 GB 可写磁盘**，出网经统一代理（策略详见 §20.7） |

**版本基线说明**：源码 checkout 的 HEAD 比 v1.3.0 二进制新若干提交。本报告所有"源码确认"类结论均针对上表 commit；
凡是无法用 v1.3.0 二进制直接复现的结论，会单独注明。**所有"实测数据"均来自 v1.3.0 二进制。**

### 0.0 本文档的附录（第二轮增补）

正文之后有五个附录，回应五个具体问题：

* **附录 A — 执行流程与可观测点速查**：每个阶段落盘什么、怎么看、SQL 怎么查。
  完整版在调研包 `06-practice/02-observability-handbook.md`，配一键 dump 脚本 `observe.sh`。
* **附录 B — 全量工程分析（compile_commands.json）**：capture 按文件、analyze 一次全量；
  含"全量 vs 逐文件"的对照实测。完整版在 `06-practice/01-whole-program-analysis.md`。
* **附录 C — 技术点覆盖自查**：诚实列出已展开、未充分展开、以及必须用真实代码库才能回答的部分。
* **附录 D — 能力边界与误报漏报（v2.2）**：47 个文件的正交四象限语料上测出的混淆矩阵，
  以及"哪些漏报是机制性的（调参救不回来）、哪些是参数性的（可以调，但有代价）"。
  **本附录更正了正文 §13 场景 #6 的结论**，并把 §27 的 P0 优先级往前推了一格。
  完整版在 `07-capability-boundary/01-capability-boundary-fp-fn.md`，配一键复现脚本 `run-boundary.sh`。
* **附录 E — 方案 C 的落地设计（v2.3）**：把 §25 的架构建议细化成可开工的设计。
  含一处对 §25.2 的架构修正（谁是主要事实源）、ExtentModel 提到一切之前的理由、
  以及 99–149 人日的分里程碑工作量估算（v3.4 按 T0-1 实测修正，原 98–151）。
  完整版在 `08-summary2-design/` 三篇，**每条设计决策都挂了证伪条件**。

另外，调研包新增 `02-prerequisites/` 四篇（OCaml 最小知识、抽象解释入门、分离逻辑与 Bi-Abduction、
CSA↔Infer 术语对照表），面向"懂 CSA 和数据流分析但没读过 OCaml"的读者，
本报告里看不懂的概念基本都能在那里找到带实例的展开。

### 0.1 本版最重要的四个新增证据（相对任何只读 README 的调研）

1. **真正把 BufferOverrun 的函数摘要打印出来了**，并且展示了一条三层调用链上**形参→实参代换**的完整过程（§13.2–§13.3）。
   这是本报告的核心证据，它直接给出了贵团队想要的"`requires 0 <= idx < extent(p)`"在 Infer 中真实的表示形态。
2. **抓到了循环 widening 的逐轮真实状态转移**（`i: 0 → [0,1] → [0,9] → [0,10] → 不动点`），并在源码中定位了
   `[0,9]` 这个"魔数"的来源（§9.2–§9.3）。这是回答"为什么 Infer 遇到循环不需要 unsupported"最硬的证据。
3. **源码级定位了全局数组漏报的根因**，并用一组对照实验（有初始化器 vs 无初始化器）证明了它（§14.3）。
4. **发现了一个对技术选型有决定性影响的事实**：`BufferOverrun` 这条分析在 v1.3.0 中**已被官方标记为 deprecated**
   （`Changelog.md` + `Checker.ml` 双重确认），而官方给出的替代者 Pulse **没有任何一个数组越界类 issue type**（§12.4）。

---

## 1. 调研背景与决策问题

贵团队现状：以 CSA 为核心的 C/C++ 静态分析体系，路径敏感符号执行（`ProgramState` / `SVal` / `MemRegion` /
`ConstraintManager` / `ExplodedGraph` / Checker 回调 / 函数 Inline）在单函数与浅调用场景成熟；但调用层次一深，
状态空间膨胀直至路径爆炸，因此不得不限制单文件分析、限制 IPA、限制 Inline 深度与循环展开——代价是跨函数信息
无法传播，内存越界类规则漏报。今年目标是把"内存读写越界 / 数组越界 / 指针访问越界"的有效分析深度从约 2 层
提升到十几层甚至更深。

已尝试的 CSA Function Summary 原型在真实业务代码上暴露的问题是：**大量函数无法生成有效摘要**——遇到 loop /
while / for / switch / 指针别名 / 多级指针 / 指针算术 / 非线性表达式 / PHI / 复杂 CFG / 间接调用 / 函数指针 /
递归 / 复杂结构体 / union / 全局变量 / 宏 / 复杂内存修改，实现往往只能 `opaque` / `unsupported` / 退化为 inline。

**本次调研要回答的唯一决策问题**：

> 为了让基于 CSA 的 C/C++ 内存越界静态分析从有限层次的 Inline 分析演进到可扩展的跨过程分析，
> 我们究竟应该 **(A) 自己继续构建 Function Summary**、**(B) 直接使用 Infer**、还是 **(C) 借鉴 Infer 重新设计分析框架**？

---

## 2. Infer 项目概览

Infer 是 Meta 开源的跨语言静态分析框架。分析主体（IR、调度器、抽象解释框架、各 Checker）用 **OCaml** 编写；
C/C++/Objective-C 的语法解析复用一个基于 **定制 Clang/LLVM 的插件**（`facebook-clang-plugins`），把 Clang AST
导出为 Infer 自己的 IR。

**【实测数据】** 本次 checkout 中 `infer/src` 各子目录的 `.ml + .mli` 文件数（一次性统计，反映各子系统工程体量）：

| 目录 | 文件数 | 作用 |
|---|---:|---|
| `pulse/` | 176 | 当前默认的内存/值分析引擎（分离逻辑风格、析取状态） |
| `IR/` | 107 | Infer 中间表示：`Sil`、`Exp`、`Cfg`、`Procdesc`、`Typ`、`Tenv`、`Dependencies` |
| `base/` | 105 | 基础设施：`Config`、`Checker`、`PayloadId`、`Database`、`DBWriter`、`IssueType` |
| `absint/` | 92 | **通用抽象解释框架**：`AbstractInterpreter`、`AbstractDomain`、`TransferFunctions`、`InterproceduralAnalysis`、`Reporting` |
| `integration/` | 84 | 与构建系统集成（Make/CMake/Buck/编译命令捕获、增量捕获） |
| `clang/` | 84 | Clang AST → Infer IR 的前端翻译 |
| `istd/` | 65 | 标准库补充（`PrettyPrintable` 等） |
| `checkers/` | 65 | 其余独立 Checker（RacerD、SIOF、Liveness…） |
| `backend/` | 52 | **调度与摘要机制核心**：`ondemand.ml`、`Summary.ml`、`Payloads.ml`、`callbacks.ml`、`CallbackOfChecker.ml`、`registerCheckers.ml`、`AnalysisDependencyGraph.ml`、`InferAnalyze.ml` |
| `bufferoverrun/` | 46 | **数组越界专用分析**：`itv.ml`、`bounds.ml`、`arrayBlk.ml`、`bufferOverrunDomain.ml`、`bufferOverrunProofObligations.ml`、`bufferOverrunModels.ml` |
| `llvm/` | 34 | LLVM bitcode 前端（Swift 实验性支持） |
| `labs/` | 4 + 6 组练习 | **官方"如何写一个新 Checker"教程**，是 §21 MiniOobChecker 的直接模板 |

**【源码确认】** `infer/src/biabduction/` 目录**不存在**；经典 Bi-Abduction 实现已整体移除。
`Changelog.md`（v1.3.0）原文：

> - [Biabduction](…) has been removed (previously deprecated in 1.2.0). Use [Pulse](…) instead.
> - [Quandary](…) has been removed (previously deprecated in 1.2.0). Use Pulse's taint analysis instead.
> - **The following checkers are now deprecated and may be removed in a future release: SIOF, BufferOverrun, Impurity, Purity, LoopHoisting.**

最后一条对本次选型至关重要，§14.4 会展开。

---

## 3. Infer 整体架构（结合真实代码路径）

```
C/C++/ObjC 源码
   │  facebook-clang-plugins：定制 Clang 21.1.6 插件，把 Clang AST 导出为 biniou/ATD
   ▼
infer/src/clang/*            前端翻译：Clang AST → Infer IR
   │                          产物落盘到 infer-out/capture.db（procedures 表：proc_attributes / cfg / callees）
   ▼
infer/src/IR/*               Sil.instr（5 条三地址指令）、Procdesc.t（过程）、Cfg.t、Typ.t、Tenv.t
   │
   ▼
infer/src/backend/InferAnalyze.ml       决定"分析哪些源文件"（默认全部；--changed-files-index 时只分析列出的）
   │
   ▼
infer/src/backend/ondemand.ml           ★ 调度核心：analyze_proc_name / analyze_callee
   │   不是"先拓扑排序再批处理"，而是按需：分析到 Call 时就地递归分析被调者（若其摘要不存在）
   ▼
infer/src/backend/callbacks.ml          iterate_procedure_callbacks：把每个已注册 Checker 的回调依次作用到 Summary.t
   │
   ▼
infer/src/absint/AbstractInterpreter.ml 过程内抽象解释：WTO 弱拓扑序 + widen/narrow 两阶段不动点
   │   （每个 Checker 提供自己的 TransferFunctions.exec_instr）
   ▼
infer/src/backend/{Summary,Payloads}.ml 结果封装为 Payloads.t（每分析一个字段），
   │                                     经 OCaml Marshal 序列化写入 SQLite results.db 的 specs 表对应列
   ▼
Caller 再次调用 analyze_proc_name → 内存 LRU 缓存 → SQLite specs 表 → 都没有才真正分析
   │
   ▼
infer/src/backend/Issue*.ml → report.json / report.txt
```

与"单向瀑布流"设想最大的不同：**Infer 没有"先把所有函数按调用图拓扑序批量分析一遍"这一步**。
默认调度器按源文件遍历（`Config.scheduler` 默认 `File`），遇到函数体就 `analyze_proc_name_for_file_analysis`；
Transfer Function 一旦遇到 `Call`，就同步、递归地触发被调者的分析。`CallGraph.ml` / `SyntacticCallGraph.ml`
只在 `--scheduler callgraph`（多进程并行）时用于任务分配，避免多个 worker 争抢同一个尚未分析完的 Callee。

---

## 4. Infer IR / CFG / CallGraph

### 4.1 Sil.instr —— 只有 5 条指令

**【源码确认】** `infer/src/IR/Sil.mli`：

```ocaml
type instr =
  | Load of {id: Ident.t; e: Exp.t; typ: Typ.t; loc: Location.t}
      (** [id = *e:typ]，e 是一个堆地址表达式 *)
  | Store of {e1: Exp.t; typ: Typ.t; e2: Exp.t; loc: Location.t}
      (** [*e1:typ = e2] *)
  | Prune of Exp.t * Location.t * bool * if_kind
      (** 分支裁剪：exp 求值为 1 时继续，否则该路径阻塞；与 CFG 结构一起编码 if/while *)
  | Call of (Ident.t * Typ.t) * Exp.t * (Exp.t * Typ.t) list * Location.t * CallFlags.t
      (** [ret_id = e_fun(arg_ts)] *)
  | Metadata of instr_metadata
      (** 作用域/循环边界等元信息，不影响语义 *)
[@@deriving compare, equal, normalize]
```

对比 CSA：CSA 的 Checker 要面对完整的 Clang AST（几百种 `Stmt` 子类）+ CFG element + `ExplodedNode`；
Infer 的 Checker 只需对 **5 个构造子**做模式匹配。**这是 Infer 写 Checker 比写 CSA Checker 心智负担低的最主要原因**，
代价是前端翻译层（`infer/src/clang/`，84 个文件）承担了全部降级复杂度。

### 4.2 数组访问在 IR 中长什么样（真实 SIL，非示意）

**【实测数据】** 对下面三个函数运行 `infer run --bufferoverrun-only --debug` 后，从
`infer-out/captured/<file>/nodes/*.html` 提取的**真实指令序列**：

```c
int leaf(int *p, int i) { return p[i]; }
int caller(void) { int a[10]; return leaf(a, 20); }
int direct(void) { int a[10]; int j = 12; return a[j]; }
```

```
leaf   node3: n$0=*&p:int*          // Load {e = Lvar p}     读形参变量 p 的值（指针本身）
              n$1=*&i:int           // Load {e = Lvar i}
              n$2=*n$0[n$1]:int     // Load {e = Lindex (Var n$0, Var n$1)}   ★ 通过指针的数组读
       node4: *&return:int=n$2

caller node3: n$0=_fun_leaf(&a:int*,20:int)    // Call (ret, Const (Cfun leaf), [Lvar a; Const 20], …)
       node4: *&return:int=n$0

direct node5: VARIABLE_DECLARED(j:int);  *&j:int=12
       node3: n$0=*&j:int
              n$1=*&a[n$0]:int      // Load {e = Lindex (Lvar a, Var n$0)}    ★ 直接对局部数组读
```

要点（对 §21 自写 Checker 至关重要）：
* `p[i]` 统一被降级成 **`Load { e = Lindex (base, idx) }`**，base 可能是 `Lvar`（局部数组本身）或 `Var`（已载入的指针值）。
* 局部数组 `int a[10]` 在指令流里**没有**携带长度信息，长度要从 `Procdesc.get_locals` 的 `Typ.Tarray {length}` 拿。
* 函数指针调用**不是** `Call` + `Const (Cfun …)`，详见 §14.4。

### 4.3 Procdesc / Cfg / CallGraph

**【源码确认】**
* `infer/src/IR/Procdesc.mli`：`get_formals`、`get_pvar_formals : t -> (Pvar.t * Typ.t) list`、
  `get_locals : t -> ProcAttributes.var_data list`、`get_nodes`、`get_exit_node`、`get_ret_var`。
* `infer/src/IR/Cfg.mli` 第 14 行：

  ```ocaml
  (** A control-flow graph is a collection of all the CFGs for the procedure names in a file *)
  type t = Procdesc.t Procname.Hash.t
  ```

  即 **一个源文件的 CFG 就是"过程名 → 过程描述"的哈希表**，没有全局跨文件 CFG 对象。
* 跨文件调用关系不靠预先建好的静态图驱动，而靠 §5 的 On-Demand 机制在运行时按需展开。

---

## 5. Function Summary：数据结构（真实类型，非概念复述）

### 5.1 Summary.t

**【源码确认】** `infer/src/backend/Summary.mli`：

```ocaml
type t =
  { payloads: Payloads.t
  ; mutable sessions: int              (** 经过符号执行的节点数 *)
  ; stats: Stats.t
  ; proc_name: Procname.t
  ; err_log: Errlog.t                  (** 本过程分析出的问题 *)
  ; mutable dependencies: Dependencies.t
        (** Dynamically discovered analysis-time dependencies used to compute this summary *)
  ; mutable is_complete_result: bool }
[@@deriving yojson_of]
```

### 5.2 Payloads.t —— **不存在统一 Schema，每种分析各自定义**

**【源码确认】** `infer/src/backend/Payloads.mli`（注释原文："This type has to be in sync with PayloadId.t"）：

```ocaml
type t =
  { annot_map: AnnotationReachabilityDomain.t SafeLazy.t option
  ; buffer_overrun_analysis: BufferOverrunAnalysisSummary.t SafeLazy.t option
  ; buffer_overrun_checker: BufferOverrunCheckerSummary.t SafeLazy.t option
  ; config_impact_analysis: ConfigImpactAnalysis.Summary.t SafeLazy.t option
  ; cost: CostDomain.summary SafeLazy.t option
  ; disjunctive_demo: DisjunctiveDemo.domain SafeLazy.t option
  ; static_constructor_stall_checker: StaticConstructorStallChecker.Summary.t SafeLazy.t option
  ; lab_resource_leaks: ResourceLeakDomain.summary SafeLazy.t option
  ; litho_required_props: LithoDomain.summary SafeLazy.t option
  ; pulse: PulseSummary.t SafeLazy.t option
  ; purity: PurityDomain.summary SafeLazy.t option
  ; racerd: RacerDDomain.summary SafeLazy.t option
  ; scope_leakage: ScopeLeakage.Summary.t SafeLazy.t option
  ; siof: SiofDomain.Summary.t SafeLazy.t option
  ; lineage: Lineage.Summary.t SafeLazy.t option
  ; lineage_shape: LineageShape.Summary.t SafeLazy.t option
  ; starvation: StarvationDomain.summary SafeLazy.t option }
[@@deriving fields, yojson_of]
```

`infer/src/base/PayloadId.ml` 定义逐字段对应的 17 个枚举变体，并且：

```ocaml
let database_fields = List.map ~f:fst Variants.descriptions
```

**`specs` 表的列名就是由这个枚举自动生成的**（`base/Database.ml:58` 使用 `PayloadId.database_fields` 建表）。

> **对贵团队的直接启示 #1**：不要指望设计一个"万能摘要 Schema"同时服务参数范围、指针偏移、别名、
> 路径约束、副作用等所有信息。Infer 自己的做法是：**每一类具体分析定制最适合它的摘要类型**，
> 物理上放在同一张表的不同列里，逻辑上互不干扰。BufferOverrun 甚至拆成了两个 payload
> （`BufferOverrunAnalysis` = 抽象内存状态，`BufferOverrunChecker` = 安全条件集合）。

### 5.3 BufferOverrun 的抽象值（内存越界最相关的那个）

**【源码确认】** `infer/src/bufferoverrun/bufferOverrunDomain.ml`，`module Val`：

```ocaml
type t =
  { itv: Itv.t                   (* 数值区间（可带符号 Bound） *)
  ; itv_thresholds: ItvThresholds.t
  ; itv_updated_by: ItvUpdatedBy.t
  ; modeled_range: ModeledRange.t
  ; powloc: AbsLoc.PowLoc.t      (* 指针可能指向的抽象位置集合 —— 别名用集合而非路径分裂表达 *)
  ; arrayblk: ArrayBlk.t         (* 作为数组/指针时的 (allocsite → offset/size/stride) *)
  ; func_ptrs: FuncPtr.Set.t
  ; traces: TraceSet.t }
```

其 `pp` 的格式串是 `"(%a%a%a%a, %a, %a%a%a)"`，前三个可见分量依次是 **(itv, powloc, arrayblk)** ——
这一点对读懂后文所有实测输出是必要的。

`ArrayBlk.t = Allocsite.t → ArrInfo.t`，`ArrInfo` 暴露 `get_offset : t -> Itv.t`、`get_size : t -> Itv.t`、
`byte_size`，构造器 `make_c : Allocsite.t -> offset:Itv.t -> size:Itv.t -> stride:Itv.t -> t`。
**这正是贵团队想要的 `extent(buf)` / `offset` / `access size` 的真实载体。**

`Itv.t` 内部是一对 `Bound.t`。`bounds.mli` 暴露 `mk_MultB : Z.t * t * t -> t`，用于表示 `c + x*y` 这类
**非线性符号 Bound**——回应贵团队"`variable * variable` 处理不了"的痛点：Infer 不回避非线性表达式，
而是把它保留成符号项继续参与推导，化简不了也不会导致分析失败。

---

## 6. Function Summary：完整源码调用链（谁生成、谁读取、怎么找到）

**【源码确认，逐函数追踪，行号对应本次 checkout】**

### 6.1 Checker 侧：`analyze_dependency` 从哪来

Checker 写的是：

```ocaml
match analyze_dependency callee_proc_name with
| Ok callee_summary -> (* 用摘要 *)
| Error _ -> (* 没有摘要：native / 未定义 / 递归环 *)
```

`analyze_dependency` 是框架注入的闭包，定义在 `infer/src/backend/CallbackOfChecker.ml:15-26`：

```ocaml
let mk_interprocedural_t analysis_req ~f_analyze_dep ~get_payload
    {Callbacks.proc_desc; summary= {Summary.proc_name; err_log} as caller_summary}
    ?(tenv = Exe_env.get_proc_tenv proc_name) () =
  let analyze_dependency ?specialization proc_name =
    Ondemand.analyze_proc_name analysis_req ?specialization ~caller_summary proc_name
    |> Result.bind ~f:(fun {Summary.payloads} ->
        f_analyze_dep (get_payload payloads) |> AnalysisResult.of_option )
  in
  { InterproceduralAnalysis.proc_desc ; tenv ; err_log ; analyze_dependency
  ; add_errlog= Summary.OnDisk.add_errlog }
```

**即：Checker 完全不需要知道 SQLite / 缓存 / 调度的存在**，框架把它们封装在这一个回调里。

### 6.2 On-Demand 调度

```
Ondemand.analyze_proc_name                                  [ondemand.ml:592]
  → analyze_callee ~specialization ?caller_summary          [ondemand.ml:574/566]
  → analyze_callee_can_raise_recursion                      [ondemand.ml:444]
      ├─ detect_mutual_recursion_cycle ~caller_summary ~callee   [ondemand.ml:121]
      │     `InMutualRecursionCycle → 或者 Error MutualRecursionCycle，
      │      或者抛 RecursiveCycleException 重启（受 Config.ondemand_recursion_restart_limit 限制，默认 100）
      ├─ register_callee ~cycle_detected ?caller_summary callee   [ondemand.ml:365]
      │     → Dependencies.record_pname_dep ~caller SummaryLoad callee   ★ 增量分析的依赖边就在这里记录
      ├─ is_summary_already_computed analysis_req callee_pname     [ondemand.ml:402]
      │     → Summary.OnDisk.get analysis_req callee_pname          [Summary.ml:211]
      │         → 先查内存两层 LRU 缓存（proc_name → analysis_req → summary）
      │         → 未命中 → spec_of_procname：
      │              SELECT report_summary, summary_metadata FROM specs WHERE proc_uid = :k
      │              （真实 SQL，Summary.ml:181-183）
      │              payloads 由 Payloads.SQLite.lazy_load ~proc_uid 惰性反序列化
      │              —— 不需要的 payload 列不会被读出来
      └─ 若 `ComputeDefaultSummary：
            Procdesc.load callee_pname                     （从 capture.db 取 callee 的 CFG）
            → run_proc_analysis                            [ondemand.ml:219]
                 → Preanal.do_preanalysis
                 → Callbacks.iterate_procedure_callbacks   [callbacks.ml:56]
                      对每个已注册 checker 调用其 proc_callback（内部跑 AbstractInterpreter）
                 → Summary.OnDisk.store analysis_req summary  [Summary.ml:231]
                      · summary.dependencies <- Dependencies.(Complete (freeze proc_name dependencies))
                      · DBWriter.store_spec … ~merge_pulse_payload:(Payloads.SQLite.serialize payloads)
                      · add_to_cache proc_name analysis_req summary
  → 返回 Ok callee_summary
```

### 6.3 几个关键设计点

* **过程间默认没有不动点迭代**：On-Demand 只分析一次就把摘要固定写盘（除非 specialization 请求或增量失效）。
  过程**内**一定有不动点（§11）。
* **调用链深度上限**：`Config.ondemand_callchain_limit` 是 `CLOpt.mk_int_opt`（**无默认值 = 不限制**，
  `base/Config.ml:2225`）；一旦设置，超深时 `run_proc_analysis` 直接 `postprocess initial_callee_summary`
  返回空摘要并 `Stats.incr_ondemand_callchain_limit_hit ()`（`ondemand.ml:281-285`）。
* **递归**：`ActiveProcedures` 维护"当前调用栈"，`detect_mutual_recursion_cycle` 检测环；
  `Config.ondemand_recursion_restart_limit` 默认 **100**（`base/Config.ml:2231`）。

---

## 7. Summary 存储：数据库结构与序列化格式

**【实测数据】** 对 §15.1 基础实验产出的 `infer-out/results.db` 直接读 schema：

```sql
CREATE TABLE procedures
        ( proc_uid TEXT PRIMARY KEY NOT NULL
        , proc_attributes BLOB NOT NULL
        , cfg BLOB
        , callees BLOB NOT NULL );

CREATE TABLE specs
        ( proc_uid TEXT PRIMARY KEY NOT NULL
        , proc_name BLOB NOT NULL
        , report_summary BLOB NOT NULL
        , summary_metadata BLOB
        , AnnotMap BLOB, BufferOverrunAnalysis BLOB, BufferOverrunChecker BLOB,
          ConfigImpactAnalysis BLOB, Cost BLOB, DisjunctiveDemo BLOB,
          StaticConstructorStallChecker BLOB, LabResourceLeaks BLOB, LithoRequiredProps BLOB,
          Pulse BLOB, Purity BLOB, RacerD BLOB, ScopeLeakage BLOB, SIOF BLOB,
          Lineage BLOB, LineageShape BLOB, Starvation BLOB );

CREATE TABLE issue_logs
        ( checker STRING NOT NULL, source_file TEXT NOT NULL, issue_log BLOB NOT NULL,
          PRIMARY KEY (checker, source_file) );
```

共 **21 列 = 4 个固定列 + 17 个 payload 列**，与 `PayloadId.t` 的 17 个变体逐字符对应。

**【实测数据】** 同一次运行中每个过程哪些 payload 非空：

```
read_value   non-empty payloads: ['BufferOverrunAnalysis', 'BufferOverrunChecker',
                                  'StaticConstructorStallChecker', 'Pulse', 'RacerD', 'Starvation']
wrapper      （同上）
main         （同上）
```

即：**摘要按列独立存储、按启用的分析按需产出**（未启用的 `Cost`、`SIOF` 等列为 NULL）。

**序列化格式【实测数据】**：

```
BufferOverrunChecker: 203 bytes, first 8 bytes = 8495a6be 000000b7
Pulse:                609 bytes, first 8 bytes = 8495a6be 0000024d
```

`0x8495A6BE` 是 **OCaml `Marshal` 模块的 `Intext_magic_number_small`**，随后 4 字节是数据长度
（0xb7=183 = 203-20，0x24d=589 = 609-20，正好是 20 字节头）。

> **结论**：摘要在磁盘上就是 OCaml 运行时原生的二进制 Marshal，**不是 JSON / Protobuf 等跨语言格式**。
> 这意味着贵团队现有的 C++ 工具**无法直接解析 Infer 的摘要**；官方唯一可读导出通道是
> `infer debug --procedures --procedures-summary`（§15 会用到）和 `--dump-json-summaries`。

**跨进程持久化/复用**：`results.db` 是磁盘 SQLite，第二次执行**可以复用**——但有一个关键前提，见 §19.3：
默认 `infer analyze` 会先 `DBWriter.delete_all_specs ()` 清空 specs 表，只有 `--incremental-analysis`
或 `--continue-analysis` 才保留（`backend/InferAnalyze.ml:277-280`）。

**内存缓存**：`Summary.ml:160-176` 的 `OnDisk.Cache` 是"进程名 → 分析请求 → 摘要"的**两层缓存**
（`Procname.Cache`，带 LRU 淘汰 `set_lru_limit`）。

---

## 8. 理论基础：理论 → 代码 → 例子

### 8.1 Separation Logic（分离逻辑）

CSA 的 `ProgramState` 本质是"环境表 + 存储表 + 路径约束"：任何时刻只有**一份全局 Store**，
指针别名靠 `MemRegion` / `SymbolicRegion` 的等价关系另外维护。

分离逻辑的状态是一个**堆断言**。原子断言 `p |-> v` 读作"当前堆里**恰好**存在一块由 `p` 指向的单元，其值为 `v`"。
分离合取 `p |-> v1 * q |-> v2` 不只是"两者都成立"，而是**额外断言 `p` 与 `q` 指向两块不相交的内存**。

```c
struct Node { int val; struct Node *next; };
int *p = malloc(sizeof(int));   *p = 1;     /* 堆断言： p |-> 1                */
int *q = malloc(sizeof(int));   *q = 2;     /* 堆断言： p |-> 1 * q |-> 2      */
```

`*` 天然编码了"`p != q` 且两块内存不重叠"，不需要像 CSA 那样另开约束求解去证明 `p != q`，
也不需要 `RegionStoreManager` 去维护两个 `MemRegion` 不重叠。这就是**局部推理（local reasoning）**：
Hoare 三元组 `{P} C {Q}` 中的 `P` 只描述 `C` 实际碰到的那部分堆，其余"帧（frame）"自动保留（Frame Rule）。

对数组而言，Infer 的 BufferOverrun 用 `ArrayBlk.t`（`Allocsite → {offset; size; stride}`）作为
"分离逻辑堆断言在区间抽象域下的具体实现"：每个分配点就是堆里一块被 `*` 分离出来的独立区域。
Pulse 则用"抽象地址 → 边表"的图结构直接表达（§13）。

### 8.2 Bi-Abduction（双向演绎）—— 版本澄清 + 思想归宿

**【源码确认，version-critical】** 经典 Bi-Abduction 的独立实现（旧 `infer/src/biabduction/`，
基于 `Prop`/`Sil` 的显式分离逻辑证明搜索）**在本 commit 中已不存在**。
网上大量 2015–2018 年代的 Infer 材料说"Infer 就是 Bi-Abduction"，对**当前版本不准确**。

但思想被原样吸收进 Pulse。`infer/src/pulse/PulseAbductiveDomain.mli` 模块注释**原文**：

> 1. Propagate operations on the current state to the pre-condition when necessary. This is the
> "abductive" part of the domain and follows the principles of biabduction as laid out in
> *Compositional Shape Analysis by Means of Bi-Abduction*, JACM, 2011,
> https://doi.org/10.1145/2049697.2049700. Simply put, a read from a memory location that is
> reachable from the precondition for the first time is taken to mean that this memory location
> had better been allocated since the beginning of the function for the operation to be safe,
> hence we "abduce" that this was the case and add it to the pre-condition (unless we already know
> that address to be invalid of course).

**用 `void set(int *p) { *p = 1; }` 逐步演绎**（标注为【推断/建议，基于源码机制的教学复述】）：

1. 单独分析 `set`：进入时对 `p` 一无所知，堆为空 `emp`。执行 `*p = 1`（`Store`）要往 `p` 指向的地址写值，
   但堆里没有关于 `p` 的断言。**溯因**：推出缺失堆断言 `p |-> _` 并加入前置条件，同时把后置更新为 `p |-> 1`。
   摘要变成 `{pre: p |-> _} set(p) {post: p |-> 1}`。
2. 分析 `caller`：`q = malloc(...)` 后状态 `q |-> _`；调用 `set(q)` 时把摘要中形参 `p` 代换为实参 `q`，
   检查当前状态能否蕴含前置条件 `q |-> _`——完全匹配，无缺失堆、无多余帧；应用后置得到 `q |-> 1`。

**【实测数据】** 上面的"溯因"在 Pulse 摘要里是可见的。对 `int read_value(int *buf, int i) { return buf[i]; }`
运行 `infer debug --procedures --procedures-summary --select all`，Pulse 摘要真实输出（节选）：

```
Pulse: 1 pre/post(s)
#0: ContinueProgram
    Current post: buf[i]->return &return: { Initialized, WrittenTo (7, ) }

    Inferred pre: buf[i]->-
    &buf: { MustBeInitialized(, t=0), MustBeValid(, None, t=0) }
    &i:   { MustBeInitialized(, t=0), MustBeValid(, None, t=0) }
    buf:  { MustBeInitialized(, t=3), MustBeValid(, None, t=3) }
    buf[i]: { MustBeInitialized(, t=3), MustBeValid(, None, t=3) }
Raw state: … { roots={ &i|read_value=v2, &buf|read_value=v1, &return|read_value=v7 };
               mem  ={ v1 -> { * -> v3 },      (* *buf        *)
                       v2 -> { * -> v4 },      (* *i          *)
                       v3 -> { [v4] -> v5 },   (* buf[i]  ★ ArrayAccess 边 *)
                       v5 -> { * -> v6 },
                       v7 -> { * -> v6 } };    (* return = buf[i] *)
               attrs={ v7 -> { Initialized, WrittenTo (7, ) } };}
           PRE=[{ … attrs={ v1 -> { MustBeInitialized, MustBeValid }, … } }]
```

`Inferred pre` 里的 `MustBeValid` / `MustBeInitialized` **就是被溯因出来的前置条件**：
"要让 `buf[i]` 这次读安全，`buf` 及 `buf[i]` 这两个地址从函数入口起就必须是有效且已初始化的"。

**与 CSA Inline 符号执行的本质区别**：CSA Inline `set` 时，把 `set` 的函数体语句**原样接到 Caller 的
ExplodedGraph 上继续走**——`set` 内部每条分支都会在 Caller 的路径上重新展开一遍，与 Caller 已有的路径约束
叉乘（路径爆炸的根源）。Bi-Abduction/Pulse 的方式是：`set` 只被分析**一次**、产出与调用点无关的抽象摘要
`{pre; post}`；Caller 端只做"前置条件蕴含检查 + 形参/实参代换 + 应用后置"这样一次**代数操作**，
计算量与 `set` 内部路径数无关，只与摘要本身的析取分支数有关（§18）。

### 8.3 Abstract Interpretation —— 为什么循环不需要 unsupported

见 §11（有逐轮真实状态转移作为证据）。

### 8.4 Bottom-up 跨过程分析

`callee → summary`、`caller → 消费 summary`，与 CSA `caller → inline callee` 的区别，本质是
"预先压缩掉被调者的状态空间" vs "把被调者的状态空间原样搬进调用者的搜索空间"。量化对比见 §18。

---

## 9. 循环 / 不动点 / Widening —— 为什么 Infer 不需要 `unsupported_loop`

### 9.1 框架

**【源码确认】** `infer/src/absint/AbstractInterpreter.ml`：
* `compute_post ?do_narrowing ... ~initial proc_desc`，遍历顺序是 **WTO（弱拓扑序，Bourdoncle）**，
  循环体被括号成一个"分量"，分量的入口是 loop head。
* `VisitCount.succ`（第 24-32 行）在访问次数超过 `Config.max_widens` 时直接 `L.die InternalError`
  （"Please check your widening operator or increase the threshold"）——**终止性被当作抽象域实现的硬约束**。
* `narrowing` 阶段由 `Config.max_narrows` 限制（第 697 行）。

### 9.2 真实的 widening 逐轮状态转移【实测数据】

对下面这个"差一错误"的循环：

```c
int f(void) {
    int a[10];
    int s = 0;
    for (int i = 0; i <= 10; ++i)   /* i 会取到 10 */
        s += a[i];
    return s;
}
```

`infer run --bufferoverrun-only --debug` 后从 `infer-out/captured/.../nodes/f.*_node1.html` 与 `node5.html`
提取的**真实**输出（node5 是 loop head）：

```
node1 [WEAK TOPOLOGICAL ORDER] bufferoverrun
 (1: 0) (12: 0) (12: 1) (6: 0) (6: 1) (6: 2) ((5: 0) (8: 0) (9: 0) (9: 1) (11: 0) … (7: 3)) (10: 0) …
 Loop heads: (5: 0)                 ← 循环分量与 loop head 被显式识别

node5 [exec] JOIN
  FROM Node 6:  i -> (0, _|_, { })                    (* 首次进入循环 *)
  FROM Node 7:  i -> (1, _|_, { })                    (* 绕回一圈 *)
  RESULT:       i -> ([0, 1], _|_, { })

node5 WIDEN(num_iters= 1)
  PREV:   i -> (0, _|_, { })
  NEXT:   i -> ([0, 1], _|_, { })
  RESULT: i -> ([0, 9], _|_, { })       ← ★ 不是直接跳到 +oo，而是跳到"阈值" 9

node5 [exec] JOIN → i -> ([0, 10], _|_, { })
node5 WIDEN(num_iters= 2)
  PREV:   i -> ([0, 9], …)
  NEXT:   i -> ([0, 10], …)
  RESULT: i -> ([0, 10], …)             ← 下一个阈值 10
… Fixpoint reached.

同一节点上数组 a 始终是：
  a -> (_|_, _|_, { f--746625832-1-1(a) -> offset : 0, size : 10 })
       ↑itv ↑powloc  ↑arrayblk：Allocsite → offset 0, size 10
```

最终 `i ∈ [0,10]` 与 `size = 10` 比较，区间部分重叠 → 报 `BUFFER_OVERRUN_L2`，
**实测报告文字**：`Offset: [0, 10] Size: 10`。

### 9.3 那个 `[0, 9]` 从哪来（源码根因）

**【源码确认】** `bufferOverrunDomain.ml:392-398`（`Val.prune_*` 路径）：

```ocaml
let itv_thresholds =
  Option.value_map (Itv.get_const y.itv) ~default:x.itv_thresholds ~f:(fun z ->
      x.itv_thresholds
      |> ItvThresholds.add Z.(z - one)
      |> ItvThresholds.add z
      |> ItvThresholds.add Z.(z + one) )
```

即：**每当一个值与常量 `z` 做比较（`Prune`），Infer 就把 `z-1, z, z+1` 加进该值的 widening 阈值集合**。
`for (i = 0; i <= 10; ++i)` 里的 `i <= 10` 使阈值集合含 `{9, 10, 11}`；
`Val.widen`（`bufferOverrunDomain.ml:18-40`）用 `Itv.widen_thresholds ~thresholds` 外推到**不小于当前值的最小阈值**，
于是 `[0,1] → [0,9] → [0,10]`，而不是一步到 `[0,+∞]`。

> **对贵团队的直接启示 #2（最可直接照搬的一条）**：
> "遇到循环就 unsupported" 与 "widening 到 Top 导致全部失去精度" 之间，还有一个**成本极低、收益极高**的中间选项：
> **把循环条件里出现的常量收集为 widening 阈值**。这不到 10 行逻辑，却是 Infer 能在循环里保住
> "`i` 恰好到 10 而不是到 +∞" 这种关键精度的全部秘密。

### 9.4 诚实的边界：抽象解释保证**终止**，不保证**精确**

* 若循环体里出现非线性递推或复杂别名改写循环边界，widen 结果可能直接到 `[-oo, +oo]`（Top），退化为"什么都不知道"。
* 但**失败模式不同**：Infer 的失败是"生成了一个过于保守、可能查不出真 bug 的摘要"，
  贵团队当前方案的失败是"根本生成不出摘要，只能退化为 inline 兜底"。这是两条路线最根本的分野。
  §16 的实验会给出这条分野的真实数据。

---

## 10. Heap / Alias / Memory Model

**BufferOverrun（非析取的区间域）**：`Mem` 分成 `StackLocs`（栈变量集合）+ `MemPure`（抽象位置 → 抽象值）
+ `Alias` + `LatestPrune`。**"一个指针可能指向多个位置"直接用集合 `PowLoc.t` 表达，而不是像 CSA 那样
为每个可能别名 fork 一条 ExplodedGraph 路径**；赋值/解引用对集合里每个位置分别应用后 `join` 回一个抽象值。
这是"用集合抽象代替路径分裂"的标准做法，是非关系型抽象域天然具备的能力。

**【实测数据】** 用集合抽象处理别名是真的有效：§16 的 `c07_alias`（`int *p = a; int *q = p; return q[17];`）
被正确检出 `BUFFER_OVERRUN_L1 Offset: 17 Size: 10`。

**Pulse（析取域）**：堆是"抽象地址 `AbstractValue.t` → 边表"的图（§8.2 实测 JSON 的 `mem` 字段：
`v1 -> { * -> v3 }`、`v3 -> { [v4] -> v5 }`）。两个变量是否别名，直接看 `roots` 里是否映射到同一个抽象地址。
更精确，但会驱动析取状态数增长（§18）。

---

## 11. Pulse 分析机制与它的硬上限

**【文档/Changelog】** Pulse 是当前默认的内存/值分析引擎，覆盖空指针解引用、内存/资源泄漏、
未初始化读、污点传播、C++ 不必要拷贝等。核心是 §8.2 的 `PulseAbductiveDomain`（溯因）+
`PathCondition/Formula`（等价类 + 线性算术 + 区间的轻量约束层，实测 JSON 里可见
`var_eqs` / `linear_eqs` / `intervals` / `atoms`，说明 Pulse 内部自带算术推理，不依赖外部 SMT）。

**【源码确认】** Pulse 的抽象域是**析取的**——一个程序点的状态是一组状态的析取列表。
Infer 不假装这不会爆炸，而是设了两道显式闸门（`infer/src/pulse/Pulse.ml:1698-1701`）：

```ocaml
let join_policy = TransferFunctions.UnderApproximateAfter Config.pulse_max_disjuncts
let widen_policy = TransferFunctions.UnderApproximateAfterNumIterations Config.pulse_widen_threshold
```

**【源码确认】默认值**（`infer/src/base/Config.ml`）：

| 配置项 | 行号 | 默认值 | 含义 |
|---|---:|---:|---|
| `--pulse-max-disjuncts` | 2498 | **20** | 一个程序点最多保留 20 条析取状态，超出直接丢弃 |
| `--pulse-widen-threshold` | 3021 | **3** | 循环迭代超过 3 次仍未收敛就停止探索新路径 |
| `--ondemand-recursion-restart-limit` | 2231 | **100** | 互递归环重启分析的最大次数 |
| `--ondemand-callchain-limit` | 2225 | **无默认（不限制）** | 可选的调用链深度上限 |

超限时 `AbstractInterpreter.ml:360-378` 的 `join_all_disj_astates` 直接丢弃多余状态并
`DisjunctiveMetadata.add_dropped_disjuncts`。**这是欠近似（under-approximation），代价是漏报。**

### 11.1 把闸门跑出来【实测数据】

构造一个有 2¹² = 4096 条路径的函数（12 个互不相关的 `if`）：

```c
int f(int *p, int *c) {
    int i = 0;
    if (c[0]) i += 1; if (c[1]) i += 2; … if (c[11]) i += 2048;
    return p[i];                       /* p 只有 10 个元素 */
}
int entry(void) { int a[10]; int c[12]; return f(a, c); }
```

| `--pulse-max-disjuncts` | 耗时 | `pulse_disjuncts_dropped` | `pulse_summaries_count` |
|---:|---:|---:|---|
| 5 | 0.29 s | **48** | `{ 5: 2 }` |
| 20（默认） | 0.31 s | **152** | `{ 20: 2 }` |
| 50 | 0.39 s | **314** | `{ 50: 2 }` |
| 200 | 0.59 s | **856** | `{ 200: 2 }` |

同一个文件交给 **BufferOverrun**（非析取的区间域）：

```
disj.c:17: error: Buffer Overrun L2(BUFFER_OVERRUN_L2)
  Offset: [0, 4095] Size: 10 by call to `f`.
```

**一次 join 就把 4096 条路径压成区间 `[0,4095]`，常数代价，且检出。**

> **对贵团队的直接启示 #3**：这组对比说明了一个对选型极重要的事实——
> **"能不能扛住路径爆炸"主要由抽象域是否析取决定，而不是由"用不用摘要"决定。**
> Pulse 用了摘要，但因为域是析取的，仍然要靠"丢状态"兜底；BufferOverrun 用了摘要且域是非析取的区间，
> 所以同一个函数常数代价搞定。贵团队若坚持 CSA 的路径敏感（析取）模型做摘要，
> **爆炸只会从"调用点组合"转移到"单函数内析取数"，不会消失**。

---

## 12. Buffer Overrun：内存越界分析的完整机制

### 12.1 抽象域（§5.3 已给出 `Val.t`），补充"证明义务"这一层

**【源码确认】** `infer/src/bufferoverrun/bufferOverrunProofObligations.ml:185-186`：

```ocaml
module ArrayAccessCondition = struct
  type t = {offset: ItvPure.t; idx: ItvPure.t; size: ItvPure.t; last_included: bool; void_ptr: bool}
```

`ConditionSet` 的接口（`.mli:24-84`）：

```ocaml
val add_array_access :
     Location.t -> offset:ItvPure.t -> idx:ItvPure.t -> size:ItvPure.t -> last_included:bool
  -> idx_traces:… -> arr_traces:… -> latest_prune:BufferOverrunDomain.LatestPrune.t
  -> checked_t -> checked_t
val add_alloc_size : … -> length:ItvPure.t -> …          (* malloc 大小 *)
val add_binary_operation : … -> Binop.t -> lhs:… -> rhs:… (* 整数溢出 *)
val subst : summary_t -> (mode:… -> eval_sym_trace) -> Procname.t -> Location.t -> … -> checked_t
val for_summary : checked_t -> summary_t
val report_errors : report:(Condition.t -> ConditionTrace.t -> IssueType.t -> unit) -> checked_t -> unit
```

**这就是贵团队想要的"前置条件 / requires"在 Infer 中的真实形态**：
一个 `(offset, idx, size)` 三元组的区间约束，配一个"可达性条件"，
`subst` 负责在调用点把符号换成实参，`for_summary` 负责决定哪些条件要继续往上传。

### 12.2 判级算法（决定报不报、报什么等级）

**【源码确认】** `bufferOverrunProofObligations.ml:333-370`，`ArrayAccessCondition.check`：

```ocaml
let real_idx = ItvPure.plus c.offset c.idx in
let size = … in
let not_overrun  = ItvPure.lt_sem real_idx size in
let not_underrun = ItvPure.le_sem ItvPure.zero real_idx in
if Boolean.is_true not_overrun && Boolean.is_true not_underrun then
  {report_issue_type= NotIssue; propagate= false}                      (* 证明安全 *)
else if Boolean.is_false not_overrun || Boolean.is_false not_underrun then
  {report_issue_type= Issue IssueType.buffer_overrun_l1; propagate= false}   (* 必然越界 *)
else if Bound.is_not_infty (ItvPure.ub real_idx) && Bound.le (ItvPure.ub size) (ItvPure.ub real_idx) then
  {report_issue_type= Issue IssueType.buffer_overrun_l2; propagate= false}   (* 很可能越界 *)
else if Bound.is_symbolic (ItvPure.lb real_idx) && Bound.le (ItvPure.lb size) (ItvPure.lb real_idx) then
  {report_issue_type= Issue IssueType.buffer_overrun_s2; propagate= true}
else
  let is_symbolic = ItvPure.is_symbolic c.offset || ItvPure.is_symbolic c.idx || ItvPure.is_symbolic size in
  let report_issue_type =
    if Config.bo_debug <= 3 && is_symbolic then SymbolicIssue         (* ★ 不报，但继续往上传 *)
    else if filter1 ~real_idx c then Issue IssueType.buffer_overrun_l5
    else if filter2 ~real_idx c then Issue IssueType.buffer_overrun_l4
    else Issue IssueType.buffer_overrun_l3
  in
  {report_issue_type; propagate= is_symbolic}
```

`ConditionSet.for_summary`（第 916 行 + 785-797 行）只保留 `propagate = true` 的条件——
**`SymbolicIssue` + `propagate = true` 就是"这条 requires 还没法判定，留给调用者"的机制**。

**【源码确认】默认是否上报**（`infer/src/base/IssueType.ml:387-418`）：

| Issue Type | 默认启用 |
|---|---|
| `BUFFER_OVERRUN_L1` / `L2` / `L3` / `S2` | ✅ 启用 |
| `BUFFER_OVERRUN_L4` / `L5` / `U5` | ❌ `~enabled:false` |

### 12.3 库函数建模而非"理解实现"

**【源码确认】** `bufferOverrunModels.ml` 对 `malloc` 等有专门模型：识别到 `malloc(n)` 就直接造一个新的
`Allocsite`，`size` 取 `n` 的区间、`offset` 取 `Itv.zero`，而不是让抽象解释引擎去"读懂" malloc 的实现。

> **对贵团队的直接启示 #4**：为高频库函数与内部基础工具函数**手写模型表**，
> 是绕开大量 `unsupported` 场景性价比最高的手段，Infer 自己就是这么干的。

### 12.4 ★ 一个对选型有决定性影响的事实：BufferOverrun 已被官方废弃

**【源码确认 + Changelog】**

* `Changelog.md`（v1.3.0）："The following checkers are now deprecated and may be removed in a future
  release: SIOF, **BufferOverrun**, Impurity, Purity, LoopHoisting."
* `infer/src/base/Checker.ml:117-130`：

  ```ocaml
  | BufferOverrunChecker ->
      { id= "bufferoverrun"
      ; kind= UserFacingDeprecated { title= "Buffer Overrun Analysis (InferBO)"
                                   ; … ; deprecation_message= "Use Pulse instead." }
      ; … ; enabled_by_default= false ; activates= [BufferOverrunAnalysis] }
  ```

* **但是**：`infer/src/base/IssueType.ml` 中，所有 `BUFFER_OVERRUN_L1..U5`、`INTEGER_OVERFLOW_L1..U5`
  的 owner **全部是 `BufferOverrunChecker`**；Pulse 拥有约 57 个 issue type，
  **其中没有任何一个是数组越界/缓冲区越界类**（本次用 grep 逐条核对）。
* **【实测数据】** 对本报告全部 13 个越界用例单独运行 `--pulse-only`，**Pulse 一条数组下标越界都没报**。

> **这是本次调研对技术选型影响最大的单条发现**：
> Infer 官方说"BufferOverrun 已废弃，用 Pulse 代替"，但**截至 v1.3.0，Pulse 并没有等价的数组越界能力**。
> 也就是说：**贵团队最关心的这一类规则，恰好落在 Infer 一条"正在被淘汰、且替代者尚未补上能力"的分析上。**
> 任何"把内存越界主链路迁移到 Infer"的方案都必须正视这个风险（§26、§28）。

---

## 13. 实验一：真实的 Function Summary（含三层代换全过程）

### 13.1 基础实验

```bash
export PATH=<infer>/bin:$PATH
cat > example.c <<'EOF'
int read_value(int *buf, int i) { return buf[i]; }
int wrapper(int *buf, int i)    { return read_value(buf, i); }
int main() { int buf[10]; return wrapper(buf, 20); }
EOF
infer run --bufferoverrun --pulse -- gcc -c example.c
```

**【实测数据】完整输出**：

```
Capturing in make/cc mode...
Found 1 source file to analyze in …/infer-out
example.c starting
example.c DONE

example.c:7: error: Buffer Overrun L1(BUFFER_OVERRUN_L1)
  Offset: 20 Size: 10 by call to `wrapper`.
  5. int main() {
  6.     int buf[10];
  7.     return wrapper(buf, 20);
                ^
  8. }

Found 1 issue
  Buffer Overrun L1(BUFFER_OVERRUN_L1): 1
real 0m0.297s
```

`by call to \`wrapper\`` 这句话本身就是"Caller 消费 Callee 摘要"的直接证据。

### 13.2 ★ 把摘要真正打印出来（本报告核心证据）

**看摘要的正确命令**（比 `--dump-json-summaries` 可读得多，且覆盖 BufferOverrun）：

```bash
infer debug --procedures --procedures-name --procedures-summary --procedures-summary-skip-empty --select all
```

**【实测数据】** `read_value` 的真实摘要（原样粘贴，未做任何改写）：

```
int read_value(int* buf, int i)
BufferOverrunAnalysis: StackLocs: { } MemPure:
{ return -> (buf, _|_, { }), buf -> M(buf, _|_, { }) } Alias:
{ ret=_=buf, cpp_iter={ }, cpp_iter_begin_or_end={ } }

BufferOverrunChecker: Safety conditions:
                      { buf.offset + i < buf.length at example.c:1:42 reachable when { } }

summary_loads=
```

`wrapper` 的摘要：`Safety conditions: { buf.offset + i < buf.length at example.c:1:42 … }`，`summary_loads= read_value`。
`main` 的摘要：`Safety conditions: {  }`（空），`ERRORS: Buffer Overrun L1`，`summary_loads= wrapper`。

> **`buf.offset + i < buf.length` 就是贵团队想要的 `requires 0 <= idx < extent(p)` 在 Infer 中的真实写法。**

### 13.3 ★ 三层调用链上的形参→实参代换（把"Summary Composition"看清楚）

```c
int leaf(int *base, int idx) { return base[idx]; }
int mid(int *arr, int k)     { return leaf(arr, k + 1); }
int top(int *p, int n)       { return mid(p, n * 2); }
int caller(void) { int a[10]; return top(a, 7); }   /* n=7 → k=14 → idx=15 > 10 */
```

**【实测数据】** 四个过程的真实 `BufferOverrunChecker` 摘要（`infer run --bufferoverrun-only` 后 dump）：

| 过程 | Safety conditions（原样输出） | `summary_loads` |
|---|---|---|
| `leaf` | `base.offset + idx < base.length at sub.c:1:39` | （空） |
| `mid` | `arr.offset + k + 1 < arr.length at sub.c:1:39`，<br>`(k + 1):signed32 at sub.c:3:35` | `leaf` |
| `top` | `p.offset + 2.n + 1 < p.length at sub.c:1:39`，<br>`(2.n + 1):signed32 at sub.c:3:35`，<br>`(n x 2):signed32 at sub.c:5:33` | `mid` |
| `caller` | `{ }` ← 全部判定完毕 | `top` |

报告：`sub.c:9: error: BUFFER_OVERRUN_L1  Offset: 15 Size: 10 by call to \`top\`.`

逐项读懂这张表，等于读懂了 Infer 的整个摘要组合机制：

1. `leaf` 的条件用**自己的形参名**表达：`base.offset + idx < base.length`。
2. `mid` 调 `leaf(arr, k+1)`：形参 `base ↦ arr`、`idx ↦ k+1`，条件变成 `arr.offset + k + 1 < arr.length`；
   **源码位置仍然是 `sub.c:1:39`（`leaf` 里那次真正的数组访问）**——这正是最终报告能精确指向根因的原因。
   同时新增一条整数溢出义务 `(k+1):signed32`。
3. `top` 调 `mid(p, n*2)`：`arr ↦ p`、`k ↦ 2·n`，条件变成 `p.offset + 2.n + 1 < p.length`。
   注意 `2.n`（线性项）与 `n x 2`（乘法义务）都被保留成**符号项**，没有因为"非线性"而放弃。
4. `caller` 传入 `a`（`size=10`）和 `n=7`：代换后 `0 + 15 < 10` 为假 → 判定为 `L1` → 上报 → 条件集合清空。

**这就是完整的"Summary → subst → 判定/再传播"闭环，全部来自真实输出，无任何人工构造。**

---

## 14. 实验二：13 类复杂场景矩阵

方法：每个场景一段独立最小 C 程序，都含**真实**越界；分别用默认配置与"最高灵敏度"配置各跑一遍。

* 默认：`infer run --bufferoverrun --pulse -o out -- gcc -c X.c`
* 最高灵敏度：额外加
  `--enable-issue-type BUFFER_OVERRUN_L4 --enable-issue-type BUFFER_OVERRUN_L5 --enable-issue-type BUFFER_OVERRUN_U5 --bo-debug 4`
  （`--bo-debug 4` 关键：它绕过 §12.2 里 `Config.bo_debug <= 3 && is_symbolic → SymbolicIssue` 那条"不报只传"的分支）

**【实测数据】**（"CSA 当前方案"一列是**转述贵团队在任务描述中的自述，非本次实测**）

| # | 场景 | CSA 摘要原型（转述） | Infer 默认 | 最高灵敏度 | 说明 / 根因 |
|---:|---|---|---|---|---|
| 1 | `if` 决定下标 | 单函数可处理 | ✅ **L2** `Offset [0,12] Size 10` | L2 | 区间把两分支 join 成 `[0,12]`，故是"可能越界" |
| 2 | `for` 差一错误 | ❌ unsupported | ✅ **L2** `[0,10] Size 10` | L2 | §9.2 的 widening 阈值机制 |
| 3 | `while` 后越界读 | ❌ unsupported | ❌ **漏报** | ✅ **L4** `Offset max(0, n(s)) Size 10` | 条件含符号 `n`，默认走 `SymbolicIssue` 只传不报；`f` 无调用者→永不判定 |
| 4 | `switch` 多分支 | 需逐分支符号执行 | ✅ **L2** `[0,14] Size 10` | L2 | 各 case 值 join 成区间 |
| 5 | 指针算术 `*(a+6+7)` | ❌ 薄弱 | ✅ **L1** `Offset 13 (<= 6 + 7) Size 10` | L1 | `ArrayBlk.offset` 随指针算术平移 |
| 6 | 结构体字段数组 `b->data[11]`（`b` 是**形参**） | ❌ 复杂 | ✅ **L1** `Offset 11 Size 8` | L1 | 形参按声明类型生成符号数组块 |
| 6b | 结构体字段数组（基址是**局部/静态/全局** struct） | ❌ 复杂 | ❌ **漏报** | ❌ 仍漏报 | **【v2.2 更正】结构性缺口，见下方更正说明与 `07-capability-boundary/`** |
| 7 | 指针别名 `q = p = a; q[17]` | ❌ 薄弱 | ✅ **L1** `Offset 17 Size 10` | L1 | `PowLoc` 集合抽象，无需路径分裂（§10） |
| 8 | **全局数组** `g_arr[100]` | 支持有限 | ❌ **漏报** | ❌ **仍漏报** | **结构性缺口，根因见 §14.3** |
| 9 | 3 层嵌套调用 + 结构体 | ❌ 易整体 opaque | ✅ **L1** `Offset 33 Size 10 by call to vec_get_checked` | L1 + 1 条 L4 噪声 | 三层摘要链式组合 |
| 10 | 20 层直线调用 | 只能到约 2 层 | ✅ 见 §15 | — | — |
| 11 | 递归 `rec(a,15)` | ❌ 通常 opaque | ✅ **L1** `Offset 15 Size 10 by call to rec` | L1 + 2 条 L4 噪声 | 递归本身不阻止摘要；`ActiveProcedures` 环检测 |
| 12 | 函数指针 `fp = &get; fp(a,22)` | ❌ 不支持 | ❌ **漏报** | ⚠️ 只报了 `get` 自身的符号条件（L4），**没有报真实越界** | **根因见 §14.4** |
| 13a | 循环 + `goto` 改写下标 | ❌ opaque | ❌ **漏报** | ✅ **L4 ×2**，其中 `Offset [2+max(1,n(s)), n(s)+2] Size 10` 精确指向 goto 路径 | 与 #3 同因：符号条件默认只传不报 |
| 13b | 仅 `goto`（无循环，对照组） | — | ✅ **L1** `Offset 12 Size 10` | L1 | **对照实验证明 goto 本身完全不影响分析** |

**耗时**：每个用例 0.21 – 0.80 s（含进程启动，小规模下启动开销占主导）。

> **【v2.2 更正：场景 #6 原结论过于乐观】**
>
> 本表初版的场景 #6 只测了**结构体指针形参**（`int f(struct Buf *b) { return b->data[11]; }`）
> 这一种写法，它确实检出 L1，于是被概括成"结构体字段数组：支持"。
> **这个概括是错的。** 后续的专项探针（13 个变体，见 `07-capability-boundary/corpus/probe/`）显示：
>
> * 基址是**形参**或 **`malloc` 分配** → ✅ 检出
> * 基址是**函数内局部 struct / `static` 局部 struct / 全局 struct（即使有初始化器）/
>   struct 数组的元素 / 嵌套 struct** → ❌ **全部漏报，且任何参数都救不回来**
> * 把 `.` 换成 `->` 也没用——区别不在访问语法，在**基址是不是形参或堆分配**
>
> 【源码确认】根因在 `bufferOverrunUtils.ml:56-70` 的 `decl_local_loc`：
> 局部变量若是 `Typ.Tstruct`，除非命中 `TypModels` 里硬编码的几个模型，
> 该函数**原样返回内存、不递归进字段**，于是字段数组永远拿不到带 size 的 `ArrayBlk`。
> 【实测数据】此时摘要里 `Safety conditions: {  }`——**一条条件都没生成**，
> 所以这不是阈值或报告等级问题。
>
> 这一项与 §14.3 的全局数组缺口同源，且**在 C 业务代码里出现频率更高**。
> 完整的实测矩阵、根因走查、修复工作量估计（约 10–30 行 OCaml）见
> **`07-capability-boundary/01-capability-boundary-fp-fn.md` §3.1**。

### 14.1 直接回答"为什么我们遇到 Loop 就 unsupported，Infer 不用"

Case 2/3/4/13a 全部是"循环/复杂控制流"，Infer **一次都没有拒绝分析**：
它总是产出一个类型合法、可被 Caller 消费的摘要，只是精度可能降级到"符号条件"或"过宽区间"。
贵团队方案的失败模式是 **opaque（摘要不存在，只能 inline 兜底）**，
Infer 的失败模式是 **精度降级（摘要存在，只是可能查不出真 bug）**。
**后者可以靠调参（`--bo-debug 4` + 启用 L4/L5）换回来，前者不能。**

### 14.2 灵敏度调高的真实代价

`--bo-debug 4 + L4/L5/U5` 把 #3、#13a 从漏报变成检出，但同时在 #9、#11 上引入了
"对被调函数自身符号参数的无法证明访问"这类报告，例如：

```
BUFFER_OVERRUN_L4 | raw_get | Offset: `i(s)` + `d.offset(s)` (<= `d.offset(s)` + `i(s)`) Size: `d.length(u)`.
```

这类报告在语义上是"这个函数对任意入参都不安全"，对**导出 API** 有价值，对**内部工具函数**几乎全是噪声。
这恰好对应贵团队"低漏报优先、谨慎控制误报"的取舍点，**是一个可以直接拿来调参的旋钮**。

### 14.3 ★ 全局数组漏报的源码级根因（本版新增，早先版本列为"未确认"）

**对照实验【实测数据】**：

```c
int g_noinit[10];                          /* 试探性定义，无初始化器 */
int g_init[10] = {1,2,3,4,5,6,7,8,9,10};   /* 有初始化器 */
static int g_st_init[10] = {0};
int f_noinit(void)  { return g_noinit[100]; }
int f_init(void)    { return g_init[100];   }
int f_st_init(void) { return g_st_init[100];}
```

```
g2.c:5: error: Buffer Overrun L1  Offset: 100 Size: 10.   ← f_init    ✅
g2.c:6: error: Buffer Overrun L1  Offset: 100 Size: 10.   ← f_st_init ✅
（f_noinit 无任何报告）                                     ← ❌ 漏报
Found 2 issues
```

同一次 capture 中 `infer debug --procedures` 可见**只有带初始化器的全局才生成了合成过程**：

```
__infer_globals_initializer_g_init          source_file: g2.c  defined: true
__infer_globals_initializer_g_st_init{…}    source_file: g2.c  defined: true
```

**源码根因【源码确认】**：`bufferOverrunDomain.ml:2169-2190`，`Mem.init` 里的 `find_global_array`：

```ocaml
let find_global_array loc =
  let open IOption.Let_syntax in
  let* pname = Loc.get_global_array_initializer loc in   (* ← 必须存在初始化器过程 *)
  let* m = get_summary pname in
  … find_opt loc m
```

而 `absLoc.ml:330-336`：

```ocaml
let rec get_global_array_initializer =
  let initializer_of_pvar pvar =
    if Pvar.is_constant_array pvar then Pvar.get_initializer_pname pvar else None
  in …
```

**结论**：BufferOverrun 获知全局数组 `size` 的**唯一通道**是"该全局的初始化器过程的摘要"。
C 语言里 `int g[10];` 是试探性定义、没有初始化器，因此 `find_global_array` 返回 `None`，
该全局的 `Val` 的 `arrayblk` 为 ⊥，**连一条安全条件都不会生成**（实测 `Safety conditions: {  }`）。
这不是阈值问题、不是精度问题，而是**内存建模的结构性缺口**。

**【实测数据】佐证**：`f_noinit` 的 `BufferOverrunAnalysis` 摘要里，`g_arr` 的三元组是
`M(g_arr, _|_, { })`——第三位 `arrayblk` 是 ⊥（对比局部数组是 `{ f--…(a) -> offset : 0, size : 10 }`）。

> **对贵团队的直接启示 #5**：全局变量必须作为**一等公民**单独建模，不能"顺带支持"。
> Infer 在这一点上比贵团队现有 CSA（单函数内对全局的路径敏感分析通常没有这个问题）**更差**。

### 14.4 ★ 函数指针漏报的根因（真实 SIL 证据）

**【实测数据】** `c12_fnptr.c` 中 `f()` 的真实 SIL（从 `--debug` 的 node HTML 提取）：

```
node5: VARIABLE_DECLARED(fp:_fn_(*));   *&fp:_fn_(*)=_fun_get
node3: n$0=*&fp:_fn_(*)
       n$1=_fun___call_c_function_ptr(n$0:_fn_, &a:int*, 22:int)   ★
node3 抽象状态: fp -> (1, { Unknown }, { Unknown -> T })
node4 抽象状态: n$1 -> ([-oo, +oo], _|_, { })
```

**根因**：C 的函数指针调用被前端降级成对**内建函数 `__call_c_function_ptr` 的调用**，
而不是 `Call (…, Const (Cfun get), …)`。BufferOverrun 没有为这个内建函数建模，
把 `fp` 的值抽象为 `Unknown`，整个调用退化为"未知调用"，返回 `[-oo,+oo]`，
**不会去读 `get` 的摘要**，因此 `get` 里那条 `a.offset + i < a.length` 的条件根本没有被实例化。

注意 `Val.t` 里确实有 `func_ptrs: FuncPtr.Set.t` 字段，但从 `bufferOverrunAnalysis.ml:465-475` 可见，
它服务的是 `Closure`（C++/ObjC block/lambda）路径；**纯 C 函数指针走的是另一条路，未被覆盖**。

---

## 15. 实验三：调用深度实验（团队核心诉求）

生成 `f1 → f2 → … → fN`，最底层 `fN(p,i) { return p[i]; }`，最顶层 `entry` 传 `int arr[10]` 与 `i = 20`。

```bash
infer run --bufferoverrun-only -o oN -- gcc -c chainN.c
```

**【实测数据】**（2 vCPU / 7.8 GB；时间含进程启动开销；`peakRSS` 为子进程峰值 RSS）

| 调用深度 | 耗时 (s) | `specs` 行数 | 报告数 | peak RSS (MB) |
|---:|---:|---:|---:|---:|
| 2 | 0.26 | 3 | 1 | 178 |
| 5 | 0.25 | 6 | 1 | 178 |
| 10 | 0.26 | 11 | 1 | 178 |
| **20** | **0.30** | **21** | **1** | **179** |
| 50 | 0.33 | 51 | 1 | 179 |
| 100 | 0.39 | 101 | 1 | 181 |
| **200** | **0.76** | **201** | **1** | **189** |

深度 200 的报告：

```
BUFFER_OVERRUN_L1 | Offset: 20 Size: 10 by call to `f1`. | proc: entry | trace nodes: 405
```

`ondemand_procs_analyzed: 201` —— **201 个过程各被分析恰好一次**，摘要条数与过程数严格线性。

> **这直接、正面地回答了贵团队最核心的诉求**：在直线调用链场景下，
> "有效分析深度从 2 层提到十几层"不仅可行，而且 **200 层仍是亚秒级、内存几乎不增长**。

**是否存在最大调用深度**：默认**没有**。`Config.ondemand_callchain_limit` 是可选项（无默认值）；
一旦设置，超深的 On-Demand 分析直接返回空摘要并计数 `ondemand_callchain_limit_hit`（`ondemand.ml:281-285`）。

### 15.1 诚实的边界：这个实验只测了"每层一条路径"

因此本报告额外做了"每层多分支"的压力实验：每层函数有 2 或 4 个分支再调用下一层。

**【实测数据】**

| 文件 | 深度 | 每层分支 | BufferOverrun 耗时 | Pulse 耗时 | BO 报告数 | Pulse 报告数 |
|---|---:|---:|---:|---:|---:|---:|
| d3_b2 | 3 | 2 | 0.23 s | 0.26 s | 1 | 0 |
| d20_b2 | 20 | 2 | 0.25 s | 0.50 s | 1 | 0 |
| d3_b4 | 3 | 4 | 0.22 s | 0.26 s | 1 | 0 |
| **d20_b4** | **20** | **4** | **0.28 s** | **0.52 s** | **1** | **0** |

理论上 `4^20 ≈ 10¹²` 条路径，**实测 BufferOverrun 0.28 s、Pulse 0.52 s**，两者都没有爆炸——
因为每层摘要在离开该层时已经被压缩（BO：join 成区间；Pulse：裁剪到 ≤ 20 条析取）。
Pulse 一条越界都没报，再次印证 §12.4。

---

## 16. 路径爆炸对比：CSA Inline vs Infer Summary

用贵团队给的例子：`A`（3 条路径）调 `B`（5 条），`B` 调 `C`（4 条）。

**CSA Inline 符号执行**：`A` 完整展开后路径上界 `3×5×4 = 60`；再加一层 `D`（4 条）变成 `240`。
**每多一层乘以该层路径数，随层数指数增长**——这正是贵团队"限制 IPA / 限制 Inline 深度"的直接动因。

**Infer 摘要方式**：`C` 独立分析一次（代价只算一次，不被 `A`/`B` 的路径数放大），产出摘要；
`B` 分析时把 `C` 的摘要当**代数对象**使用；`B` 自身摘要在离开 `B` 时被压缩；`A` 同理。
**层与层之间是"加法"而不是"乘法"。**

**但 Infer 并非没有状态爆炸**，明确指出会爆的四处：

1. **单个过程内部的析取状态数**（Pulse）：超过 `pulse_max_disjuncts=20` 直接丢弃 → 欠近似 → 漏报。
   §11.1 已实测：4096 条路径下丢弃 152 条状态。
2. **循环迭代次数**（Pulse）：`pulse_widen_threshold=3`，超过即停止探索新路径。
3. **别名 × 析取的组合**：Pulse 的精确堆图让指针分支比整数分支更容易触达上限。
4. **摘要实例化本身的局部代价**：Caller 的每条路径 × Callee 摘要的每个析取分支，在**调用点局部**相乘。
   它不会传染到 Caller 之外，但一条深链上每层都接近上限时，最外层仍可能显著变慢。

**非析取域（BufferOverrun 的区间域）不受 1/2/3 影响**，代价是牺牲路径敏感性
（越界判断在 join 之后的区间上做，不区分具体是哪条分支触发）。

---

## 17. 增量分析机制

### 17.1 源码

**【源码确认】** `infer/src/backend/AnalysisDependencyGraph.mli`：

```ocaml
val invalidate : changed_files:SourceFile.Set.t option -> unit
(** Build a dependency graph from the summaries in the specs table, flag any transitive
    dependencies of the [changed_files], then invalidate any flagged transitive dependencies. *)
val store_previous_schedule : unit -> unit
val load_previous_schedule : unit -> CallGraph.t option
```

链路：

1. **依赖记录**：`IR/Dependencies.mli` 定义 `type kind = SummaryLoad | RecursionEdge | Other`；
   `ondemand.ml:365` 的 `register_callee` 每次成功取到 callee 摘要就
   `Dependencies.record_pname_dep ~caller SummaryLoad callee`。
   **运行分析的同时就顺带记录了一张"谁读了谁的摘要"的动态依赖边表**，不需要额外的静态调用图步骤。
2. **依赖固化**：`Summary.OnDisk.store` 调用 `Dependencies.(Complete (freeze proc_name dependencies))`，
   连同 `used_tenv_sources` 一起落盘到 `specs.summary_metadata`。
3. **失效传播**：`AnalysisDependencyGraph.build ~changed_files` 从 `specs` 表**重建反向依赖图**
   （`summary_loads` + `other_proc_names` 反向连边），从三类种子出发做**传递闭包**并 `flag_reachable`：
   (1) 被删除的过程；(2) **变更文件的类型环境（tenv）的依赖者**；(3) `attrs.changed = true` 的过程。
   随后 `Summary.OnDisk.delete_all ~procedures:invalidated_pnames` + `IssueLog.invalidate`。
4. **变更判定不是文件哈希，而是逐过程的 CFG 结构比对**：`IR/SourceFiles.ml:78-92` 在
   `Config.mark_unchanged_procs` 时对每个过程调 `Procdesc.mark_if_unchanged ~old_pdesc ~new_pdesc`，
   `IR/Procdesc.ml:1040-1070` 逐节点、逐指令比较：

   ```ocaml
   let is_structurally_equal =
     Bool.equal old_attrs.is_defined new_attrs.is_defined
     && Typ.equal old_attrs.ret_type new_attrs.ret_type
     && formals_eq old_attrs.formals new_attrs.formals
     && nodes_eq (get_nodes old_pdesc) (get_nodes new_pdesc)
   in
   let changed = (Config.continue_capture && (get_attributes old_pdesc).changed)
                 || not is_structurally_equal
   ```

   **改注释、改空白、改同文件里的别的函数，都不会让一个函数失效。**

### 17.2 实测

**【实测数据】** 三文件工程：`leaf.c`（`leaf_get`、`leaf_untouched`）→ `mid.c`（`mid_a`、`mid_b`、`mid_indep`）
→ `top.c`（`top_a`、`top_b`、`top_indep`），共 8 个过程。

**(A) 全量基线**

```bash
infer capture -- gcc -c leaf.c mid.c top.c
infer analyze --bufferoverrun-only
# → ondemand_procs_analyzed: 8 , specs 行数 8
```

**(B) 源码完全没变，重新 capture + 增量 analyze**

```bash
infer capture --incremental-analysis -- gcc -c leaf.c mid.c top.c
infer analyze --bufferoverrun-only --incremental-analysis --changed-files-index all.idx
```

```
Incremental analysis: Invalidated 0 of 8 procedure summaries, and file-level analyses for 0 distinct files
                      [traversal 460μs] [db 11.8μs]
Found 3 (out of 3) source files to analyze
Incremental analysis: Computed 0 procedure summaries.
ondemand_procs_analyzed: 0
```

> **`ondemand_procs_analyzed: 0`：这是"摘要跨进程持久化并被真正复用"最硬的证据。**

**(C) 只改 `leaf.c`（`leaf_get` 改成 `p[i+100]`），重新 capture 变更文件，增量分析全部三个文件**

```bash
infer capture --incremental-analysis -- gcc -c leaf.c
infer analyze --bufferoverrun-only --incremental-analysis --changed-files-index all.idx
```

```
Incremental analysis: Invalidated 7 of 8 procedure summaries, and file-level analyses for 3 distinct files
                      [traversal 408μs] [db 137μs]
Found 3 (out of 3) source files to analyze
Incremental analysis: Computed 7 procedure summaries.
top.c:4: error: Buffer Overrun L1 …
top.c:5: error: Buffer Overrun L1 …
Found 2 issues
ondemand_procs_analyzed: 7
```

> **复现补充（打包后又跑了一次，如实记录）**：同一份改动，把上面的命令序列换一个 capture 历史重跑，
> 得到的是 `Invalidated 6 of 8 / Computed 6`（`leaf_untouched` 这次被判为 unchanged 而未失效），
> 顶层的两条 L1 照常报出。更极端地，若在改动前**多插一次全量 `infer capture --incremental-analysis`**，
> 会看到 `Invalidated 0 of 8`——即改动后 `changed` 标志未被更新。
> 结论不变，但**"失效条数"不是一个稳定可断言的指标，依赖 capture 历史；CI 里应当断言的是
> "顶层报告是否重新出现"，而不是失效了几个**。

**7 / 8 被失效并重算，1 个复用。** 纯调用图可达集只有 5 个
（`leaf_get`、`mid_a`、`mid_b`、`top_a`、`top_b`），多出来的两个来自
**`used_tenv_sources` 这条依赖**：任何用到 `leaf.c` 类型环境的过程也会被失效。
**这是一处比纯调用图更保守的过度失效**，源码里可直接确认（§17.1 第 3 步）。

### 17.3 三个必须注意的工程细节（决定它能不能用在版本级 CI 上）

1. **默认 `infer analyze` 会先清空 specs 表**：`InferAnalyze.ml:277-280`

   ```ocaml
   if not Config.continue_analysis then
     if Config.reanalyze then ( … Summary.OnDisk.delete_all … )
     else if not Config.incremental_analysis then DBWriter.delete_all_specs ()
   ```

   **只有 `--incremental-analysis` 或 `--continue-analysis` 才复用上次的摘要。**
2. **`--incremental-analysis` 必须配 `--changed-files-index`**，否则直接
   `L.die ExternalError "incremental invalidation requires specifying changed files via --changed-files-index"`。
3. **`--changed-files-index` 同时决定了"分析哪些文件"**（`InferAnalyze.ml:71-97`）。
   如果 index 里只写变更文件，被失效的上游 Caller 摘要会被**删掉但不会在本轮重算**
   （实测：只写 `leaf.c` 时输出 `Found 1 (out of 3) source file to analyze`、`Computed 1 procedure summaries`，
   且顶层越界**没有**被报出来）。
   **正确的 CI 用法是：capture 只捕获变更文件，但 analyze 的 index 要覆盖需要出结论的全部文件。**
4. **`--continue` 方式 capture 会过度失效**：`Procdesc.mark_if_unchanged` 中
   `(Config.continue_capture && old.changed) || not is_structurally_equal` 会把上一轮的 `changed` 位继承下来，
   实测出现 `Invalidated 8 of 8`。

---

## 18. 扩展机制：在 Infer 里写自己的分析

### 18.1 Infer 真正推荐的方式（不强行类比 CSA）

**Infer 没有 CSA 那种"在已有大符号执行引擎上挂 `checkPreCall`/`checkBind` 回调"的薄插件机制。**
一个新分析在 Infer 里是**一个完整独立的模块**：自定义抽象域 + 自定义 Transfer Function +
用框架的 functor 组装出过程内分析器 + 注册进回调体系。官方教学路径是 `infer/src/labs/`
（`README.md` 六步练习 + `00_dummy_checker` … `05_access_paths_interprocedural` 参考答案）。

**【源码确认】** `infer/src/labs/04_interprocedural/ResourceLeaks.ml` 的跨过程部分原文：

```ocaml
| Call (_return, Const (Cfun callee_proc_name), _actuals, _loc, _) -> (
    …
    match analyze_dependency callee_proc_name with
    (* interprocedural analysis produced a summary: use it *)
    | Ok callee_summary ->
        ResourceLeakDomain.apply_summary ~summary:callee_summary astate
    (* No summary for [callee_proc_name]; it's native code or missing for some reason *)
    | Error _ ->
        astate )
```

**关键点：`analyze_dependency` 由框架注入，实现者完全不需要知道 §6 的 Ondemand / SQLite / LRU 细节。**

四个接入点：

| # | 要写什么 | 位置 |
|---|---|---|
| 1 | **Domain**：满足 `AbstractDomain.S`（`leq`/`join`/`widen`/`pp`），可用 `AbstractDomain.{Pair,TopLifted,BottomLifted,Map,FiniteSet}` 组合子搭 | `XxxDomain.ml/.mli` |
| 2 | **TransferFunctions**（functor，参数化于 `CFG`）：`exec_instr astate analysis_data node idx instr`，对 `Sil.instr` 的 5 个构造子做模式匹配 | `Xxx.ml` |
| 3 | **checker 入口**：`AbstractInterpreter.MakeRPO(TransferFunctions(CFG)).compute_post` + `Reporting.log_issue` | `Xxx.ml` |
| 4 | **注册**：`Checker.ml/.mli` 加枚举 + `PayloadId.ml/.mli` 加 payload + `Payloads.ml/.mli` 加字段 + `registerCheckers.ml` 加一行 + `IssueType.ml/.mli` 加 issue type | `base/`、`backend/` |

**是否可以复用 Pulse？——不建议。** Pulse 是为"通用内存安全"设计的 176 文件的析取域 + 溯因引擎，
复用意味着要掌握 `PulseAbductiveDomain` / `PathCondition` / `PulseModels` 一整套私有 API。
照着 `labs/` 从零写一个**专用**区间域分析，成本低得多，也更贴合"内存越界"这个聚焦目标——
这正是官方教程把"新写一个 Checker"而不是"改 Pulse"作为标准做法的原因。

---

## 19. ★ MiniOobChecker：完整实现（已真实编译、已真实运行）

> **本章的所有源码都在本次 checkout 上编译通过，并在同一环境跑出了下文的真实日志。**
> `infer --version` → `Infer version v1.3.0-3d38229b`（自建二进制，版本串带上了源码 commit 前缀）。

### 19.1 设计

要证明的核心命题：**`read` 被独立分析并生成摘要，`foo` 分析到 `read(...)` 时读取摘要，而不是 inline `read` 的函数体。**

摘要内容是一组"安全条件"（obligation），形式为 `0 <= idx < extent(base)`，其中
`idx` 可以是常量或**"形参 #k + 常量"**，`base` 可以是**已知长度的局部数组**或**形参 #k**。
这是 §12.1 `ArrayAccessCondition` + `ConditionSet.{subst,for_summary}` 的教学级最小实现。

### 19.2 文件清单（4 个新文件 + 9 个文件的注册改动共 37 行）

```
新增：infer/src/labs/MiniOobDomain.mli
     infer/src/labs/MiniOobDomain.ml
     infer/src/labs/MiniOobChecker.mli
     infer/src/labs/MiniOobChecker.ml
改动：infer/src/base/Checker.mli            +1
     infer/src/base/Checker.ml             +16
     infer/src/base/PayloadId.mli          +1
     infer/src/base/PayloadId.ml           +3
     infer/src/base/IssueType.mli          +2
     infer/src/base/IssueType.ml           +5
     infer/src/backend/Payloads.mli        +1
     infer/src/backend/Payloads.ml         +6
     infer/src/backend/registerCheckers.ml +2
                                    合计   37 行
```

**注意：不需要新建 `dune` 文件，也不需要改任何 `dune`。**
因为 `infer/src/labs/` 已经是库 `Labs`，而 `infer/src/backend/dune` 已经 `-open Labs` 并把 `Labs` 列入 `libraries`。
这是把 demo 放进 `labs/` 的原因，也是本方案比"新建一个库目录"少一大半改动的原因。
`specs` 表的新列 `MiniOob` 由 `PayloadId.database_fields` **自动生成**（实测列数从 21 变成 22）。


### 19.3 完整源码（逐字为编译通过的版本）

**`infer/src/labs/MiniOobDomain.mli`**

```ocaml
(*
 * MiniOobChecker: a minimal, summary-based out-of-bounds checker.
 *
 * This is a teaching-sized re-implementation of the two ideas that make Infer's
 * BufferOverrun work across procedure boundaries:
 *   1. an abstract value that can name a *formal parameter* symbolically, so that
 *      facts survive outside the procedure they were discovered in;
 *   2. a set of *safety conditions* ("obligations") carried in the summary, which
 *      the caller instantiates by substituting its own actuals for the callee's
 *      formals -- see BufferOverrunProofObligations.ConditionSet.{subst,for_summary}.
 *)

open! IStd

(** A symbolic integer: either a literal, or "the value of formal #k, plus a constant". That second
    form is what lets an index discovered inside a callee be re-expressed at a call site. *)
module Idx : sig
  type t = Const of int | Formal of int * int | Top [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit

  val plus_const : t -> int -> t
end

(** What an array access is based on: a local array whose element count we know, or the pointer that
    arrived as formal #k (whose extent is unknown here and must be resolved by a caller). *)
module Base : sig
  type t = Local of {name: string; size: int} | Formal of int [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit
end

(** One safety condition: "[idx] must be within the bounds of [base]", recorded at [loc]. *)
module Obligation : sig
  type t = {base: Base.t; idx: Idx.t; loc: Location.t} [@@deriving compare, equal]

  val pp : Format.formatter -> t -> unit
end

(** The abstract value of a program variable or temporary. *)
module Val : sig
  type t = {idx: Idx.t; base: Base.t option} [@@deriving compare, equal]

  val top : t

  val pp : Format.formatter -> t -> unit
end

include AbstractDomain.S

val initial : formals:(Var.t * int) list -> arrays:(Var.t * string * int) list -> t
(** Entry state: each formal #k is bound to the symbolic value [Formal (k, 0)] and, if it is a
    pointer, to the symbolic base [Base.Formal k]; each local array is bound to a [Base.Local] with
    its element count. *)

val find : Var.t -> t -> Val.t

val set : Var.t -> Val.t -> t -> t

val add_obligation : Obligation.t -> t -> t

val obligations : t -> Obligation.t list

type summary = {conditions: Obligation.t list} [@@deriving compare, equal]

val pp_summary : Format.formatter -> summary -> unit

val summary_of_post : t -> summary
(** Keep only the conditions that are still open, i.e. those phrased in terms of formals. Conditions
    about the procedure's own locals were already decided here and never escape. *)
```

**`infer/src/labs/MiniOobDomain.ml`**

```ocaml
(*
 * MiniOobChecker domain -- see MiniOobDomain.mli for the design notes.
 *)

open! IStd
module F = Format

module Idx = struct
  type t = Const of int | Formal of int * int | Top [@@deriving compare, equal]

  let pp fmt = function
    | Const n ->
        F.fprintf fmt "%d" n
    | Formal (k, 0) ->
        F.fprintf fmt "arg%d" k
    | Formal (k, c) when c > 0 ->
        F.fprintf fmt "arg%d+%d" k c
    | Formal (k, c) ->
        F.fprintf fmt "arg%d-%d" k (-c)
    | Top ->
        F.pp_print_string fmt "T"


  let plus_const t c =
    match t with
    | Const n ->
        Const (n + c)
    | Formal (k, c0) ->
        Formal (k, c0 + c)
    | Top ->
        Top
end

module Base = struct
  type t = Local of {name: string; size: int} | Formal of int [@@deriving compare, equal]

  let pp fmt = function
    | Local {name; size} ->
        F.fprintf fmt "%s[0..%d)" name size
    | Formal k ->
        F.fprintf fmt "arg%d" k
end

module Obligation = struct
  type t = {base: Base.t; idx: Idx.t; loc: Location.t} [@@deriving compare, equal]

  let pp fmt {base; idx; loc} =
    F.fprintf fmt "0 <= %a < extent(%a) at %a" Idx.pp idx Base.pp base Location.pp loc
end

module Val = struct
  type t = {idx: Idx.t; base: Base.t option} [@@deriving compare, equal]

  let top = {idx= Idx.Top; base= None}

  let join x y = if equal x y then x else top

  let pp fmt {idx; base} =
    match base with
    | None ->
        Idx.pp fmt idx
    | Some b ->
        F.fprintf fmt "%a@@%a" Idx.pp idx Base.pp b
end

module VarMap = PrettyPrintable.MakePPMap (Var)

type t = {vars: Val.t VarMap.t; obligations: Obligation.t list}

let dedup = List.dedup_and_sort ~compare:Obligation.compare

(* A state is smaller when it knows *more* about variables (a missing binding is Top) and has
   accumulated no extra obligations. *)
let leq ~lhs ~rhs =
  VarMap.for_all
    (fun v rv ->
      match VarMap.find_opt v lhs.vars with Some lv -> Val.equal lv rv | None -> false )
    rhs.vars
  && List.for_all rhs.obligations ~f:(fun o ->
      List.mem lhs.obligations o ~equal:Obligation.equal )


let join a b =
  { vars=
      VarMap.merge
        (fun _ x y ->
          match (x, y) with Some x, Some y -> Some (Val.join x y) | _ -> None (* = Top *) )
        a.vars b.vars
  ; obligations= dedup (a.obligations @ b.obligations) }


(* The domain has finite height on every program: [Val.t] collapses to Top after one disagreement
   and the obligation list only grows with syntactically distinct array accesses, of which a
   procedure has finitely many. So widening is just join -- no threshold needed. *)
let widen ~prev ~next ~num_iters:_ = join prev next

let pp fmt {vars; obligations} =
  F.fprintf fmt "@[<v>vars: %a@,conds: [%a]@]" (VarMap.pp ~pp_value:Val.pp) vars
    (F.pp_print_list ~pp_sep:(fun f () -> F.fprintf f ", ") Obligation.pp)
    obligations


let initial ~formals ~arrays =
  let vars =
    List.fold formals ~init:VarMap.empty ~f:(fun acc (var, k) ->
        VarMap.add var {Val.idx= Idx.Formal (k, 0); base= Some (Base.Formal k)} acc )
  in
  let vars =
    List.fold arrays ~init:vars ~f:(fun acc (var, name, size) ->
        VarMap.add var {Val.idx= Idx.Top; base= Some (Base.Local {name; size})} acc )
  in
  {vars; obligations= []}


let find v {vars} = Option.value (VarMap.find_opt v vars) ~default:Val.top

let set v value astate = {astate with vars= VarMap.add v value astate.vars}

let add_obligation o astate = {astate with obligations= dedup (o :: astate.obligations)}

let obligations {obligations} = obligations

type summary = {conditions: Obligation.t list} [@@deriving compare, equal]

let pp_summary fmt {conditions} =
  if List.is_empty conditions then F.pp_print_string fmt "(no open safety conditions)"
  else
    F.fprintf fmt "@[<v>%a@]"
      (F.pp_print_list ~pp_sep:(fun f () -> F.fprintf f ", ") Obligation.pp)
      conditions


let summary_of_post (astate : t) =
  let open_conditions =
    List.filter astate.obligations ~f:(fun {Obligation.base} ->
        match base with Base.Formal _ -> true | Base.Local _ -> false )
  in
  {conditions= dedup open_conditions}
```

**`infer/src/labs/MiniOobChecker.mli`**

```ocaml
(*
 * MiniOobChecker: entry point, registered in RegisterCheckers.
 *)

open! IStd

val checker : MiniOobDomain.summary InterproceduralAnalysis.t -> MiniOobDomain.summary option
```

**`infer/src/labs/MiniOobChecker.ml`**

```ocaml
(*
 * MiniOobChecker: a minimal out-of-bounds checker that works ACROSS procedures by
 * reading callee summaries, never by inlining callee bodies.
 *
 * The structure mirrors infer/src/labs/04_interprocedural (the official lab) and the
 * real BufferOverrun pipeline:
 *
 *   TransferFunctions.exec_instr
 *     |-- Load  {e = Lindex (base, idx)}  -> record a safety condition, decide it if we can
 *     |-- Call  (_, Const (Cfun callee), actuals, _, _)
 *            -> analyze_dependency callee          (* Ondemand: cache -> SQLite -> analyse once *)
 *            -> substitute actuals for the callee's formals in each condition of its summary
 *            -> decide it here, or re-propagate it in terms of OUR formals
 *   checker
 *     -> AbstractInterpreter.MakeRPO(...).compute_post
 *     -> MiniOobDomain.summary_of_post  (the payload stored in the specs table)
 *
 * Note what is absent: nothing in this file ever loads the callee's Procdesc or walks its
 * CFG. The only thing that crosses the procedure boundary is the summary value.
 *)

open! IStd
module F = Format
module L = Logging
module Dom = MiniOobDomain

let log fmt = F.kasprintf (fun s -> L.progress "[MiniOOB] %s@." s) fmt

(** Bounds check on a fully-resolved condition. [None] = cannot decide. *)
let decide ~(base : Dom.Base.t) ~(idx : Dom.Idx.t) =
  match (base, idx) with
  | Dom.Base.Local {size}, Dom.Idx.Const v ->
      Some (v >= 0 && v < size)
  | _ ->
      None


module TransferFunctions (CFG : ProcCfg.S) = struct
  module CFG = CFG
  module Domain = Dom

  type analysis_data = Dom.summary InterproceduralAnalysis.t

  let int_of_const c = match (c : Const.t) with Cint i -> IntLit.to_int i | _ -> None

  (** Abstract evaluation of a SIL expression. Anything we do not model becomes Top, which costs
      precision but never soundness of the *reporting* decision (we only report when the condition
      resolves to a concrete violation). *)
  let rec eval astate (e : Exp.t) : Dom.Val.t =
    match e with
    | Const c -> (
      match int_of_const c with
      | Some n ->
          {Dom.Val.idx= Dom.Idx.Const n; base= None}
      | None ->
          Dom.Val.top )
    | Var id ->
        Dom.find (Var.of_id id) astate
    | Lvar pvar ->
        Dom.find (Var.of_pvar pvar) astate
    | Cast (_, e) ->
        eval astate e
    | BinOp ((PlusA _ | PlusPI), e1, Const c) | BinOp ((PlusA _ | PlusPI), Const c, e1) -> (
      match int_of_const c with
      | Some n ->
          let v = eval astate e1 in
          {v with Dom.Val.idx= Dom.Idx.plus_const v.Dom.Val.idx n}
      | None ->
          Dom.Val.top )
    | BinOp ((MinusA _ | MinusPI), e1, Const c) -> (
      match int_of_const c with
      | Some n ->
          let v = eval astate e1 in
          {v with Dom.Val.idx= Dom.Idx.plus_const v.Dom.Val.idx (-n)}
      | None ->
          Dom.Val.top )
    | _ ->
        Dom.Val.top


  (** Record one array access, and report immediately if it is decidable here. *)
  let check_access {InterproceduralAnalysis.proc_desc; err_log} astate ~base_exp ~idx_exp ~loc =
    let base = (eval astate base_exp).Dom.Val.base in
    let idx = (eval astate idx_exp).Dom.Val.idx in
    match base with
    | None ->
        astate
    | Some base -> (
        let o = {Dom.Obligation.base; idx; loc} in
        match decide ~base ~idx with
        | Some true ->
            log "  safe access %a" Dom.Obligation.pp o ;
            astate
        | Some false ->
            log "  OUT OF BOUNDS %a" Dom.Obligation.pp o ;
            Reporting.log_issue proc_desc err_log ~loc MiniOob IssueType.mini_oob
              (F.asprintf "Out-of-bounds access: %a" Dom.Obligation.pp o) ;
            astate
        | None ->
            log "  open condition %a" Dom.Obligation.pp o ;
            Dom.add_obligation o astate )


  (** Instantiate one condition of a callee summary at this call site: replace the callee's formals
      by our actuals. This is the miniature of
      BufferOverrunProofObligations.ConditionSet.subst. *)
  let instantiate ~actual_vals ({Dom.Obligation.base; idx; loc} as callee_obl) =
    let nth k = List.nth actual_vals k in
    let base' =
      match base with
      | Dom.Base.Local _ ->
          None (* a condition about the callee's own local: already decided there *)
      | Dom.Base.Formal k ->
          Option.bind (nth k) ~f:(fun (v : Dom.Val.t) -> v.Dom.Val.base)
    in
    let idx' =
      match idx with
      | Dom.Idx.Const _ | Dom.Idx.Top ->
          idx
      | Dom.Idx.Formal (k, c) -> (
        match nth k with
        | Some (v : Dom.Val.t) ->
            Dom.Idx.plus_const v.Dom.Val.idx c
        | None ->
            Dom.Idx.Top )
    in
    Option.map base' ~f:(fun base' -> {callee_obl with Dom.Obligation.base= base'; idx= idx'})


  let exec_call ({InterproceduralAnalysis.proc_desc; err_log; analyze_dependency} as analysis_data)
      astate ~callee ~actuals ~loc =
    match analyze_dependency callee with
    | Error _ ->
        log "no summary for %a (undefined/native) -- nothing to do" Procname.pp callee ;
        astate
    | Ok {Dom.conditions= callee_obligations} ->
        log "Load Summary %a (%d open condition(s))" Procname.pp callee
          (List.length callee_obligations) ;
        let actual_vals = List.map actuals ~f:(fun (e, _typ) -> eval astate e) in
        log "Apply Summary %a" Procname.pp callee ;
        List.fold callee_obligations ~init:astate ~f:(fun astate callee_obl ->
            match instantiate ~actual_vals callee_obl with
            | None ->
                astate
            | Some ({Dom.Obligation.base; idx} as o) -> (
                (* keep the callee's source location: that is where the access really is *)
                match decide ~base ~idx with
                | Some true ->
                    log "  discharged (safe): %a" Dom.Obligation.pp o ;
                    astate
                | Some false ->
                    log "  discharged (VIOLATED): %a" Dom.Obligation.pp o ;
                    Reporting.log_issue proc_desc err_log ~loc MiniOob IssueType.mini_oob
                      (F.asprintf "Out-of-bounds access inside `%a`: %a" Procname.pp callee
                         Dom.Obligation.pp o ) ;
                    astate
                | None ->
                    log "  still open, propagating to our own summary: %a" Dom.Obligation.pp o ;
                    Dom.add_obligation o astate ) )
        |> fun astate -> ignore analysis_data ; astate


  let exec_instr astate analysis_data _node _idx (instr : Sil.instr) =
    match instr with
    (* n$0 = *&x  -- read a program variable *)
    | Load {id; e= Lvar pvar} ->
        Dom.set (Var.of_id id) (Dom.find (Var.of_pvar pvar) astate) astate
    (* n$2 = *n$0[n$1]  /  n$1 = *&a[n$0]  -- the array read we care about *)
    | Load {id; e= Lindex (base_exp, idx_exp); loc} ->
        let astate = check_access analysis_data astate ~base_exp ~idx_exp ~loc in
        Dom.set (Var.of_id id) Dom.Val.top astate
    | Load {id; e} ->
        Dom.set (Var.of_id id) (eval astate e) astate
    (* *&a[n$0] = rhs -- an array write is checked exactly like a read *)
    | Store {e1= Lindex (base_exp, idx_exp); loc} ->
        check_access analysis_data astate ~base_exp ~idx_exp ~loc
    | Store {e1= Lvar pvar; e2} ->
        Dom.set (Var.of_pvar pvar) (eval astate e2) astate
    | Call ((ret_id, _), Const (Cfun callee), actuals, loc, _) ->
        let astate = exec_call analysis_data astate ~callee ~actuals ~loc in
        Dom.set (Var.of_id ret_id) Dom.Val.top astate
    | Call ((ret_id, _), _, _, _, _) ->
        (* indirect call (function pointer): we cannot resolve the callee, so we learn nothing *)
        Dom.set (Var.of_id ret_id) Dom.Val.top astate
    | Store _ | Prune _ | Metadata _ ->
        astate


  let pp_session_name _node fmt = F.pp_print_string fmt "mini oob checker"
end

module CFG = ProcCfg.Normal
module Analyzer = AbstractInterpreter.MakeRPO (TransferFunctions (CFG))

(** Collect the entry state: formals get symbolic values, local arrays get their extent. *)
let initial_state proc_desc =
  let formals =
    Procdesc.get_pvar_formals proc_desc
    |> List.mapi ~f:(fun k (pvar, _typ) -> (Var.of_pvar pvar, k))
  in
  let proc_name = Procdesc.get_proc_name proc_desc in
  let arrays =
    Procdesc.get_locals proc_desc
    |> List.filter_map ~f:(fun {ProcAttributes.name; typ} ->
        match typ.Typ.desc with
        | Typ.Tarray {length= Some len} ->
            IntLit.to_int len
            |> Option.map ~f:(fun size ->
                (Var.of_pvar (Pvar.mk name proc_name), Mangled.to_string name, size) )
        | _ ->
            None )
  in
  Dom.initial ~formals ~arrays


let checker ({InterproceduralAnalysis.proc_desc} as analysis_data) =
  let proc_name = Procdesc.get_proc_name proc_desc in
  log "Analyze %a" Procname.pp proc_name ;
  let initial = initial_state proc_desc in
  match Analyzer.compute_post analysis_data ~initial proc_desc with
  | None ->
      log "no post for %a" Procname.pp proc_name ;
      None
  | Some post ->
      let summary = Dom.summary_of_post post in
      log "Generate Summary %a: %a" Procname.pp proc_name Dom.pp_summary summary ;
      Some summary
```

### 19.4 注册改动的完整 diff（真实 `git diff`）

```diff
diff --git a/infer/src/backend/Payloads.mli b/infer/src/backend/Payloads.mli
index 11a44e03..60e2aeb9 100644
--- a/infer/src/backend/Payloads.mli
+++ b/infer/src/backend/Payloads.mli
@@ -28,6 +28,7 @@ include sig
     ; static_constructor_stall_checker: StaticConstructorStallChecker.Summary.t SafeLazy.t option
     ; lab_resource_leaks: ResourceLeakDomain.summary SafeLazy.t option
     ; litho_required_props: LithoDomain.summary SafeLazy.t option
+    ; mini_oob: MiniOobDomain.summary SafeLazy.t option
     ; pulse: PulseSummary.t SafeLazy.t option
     ; purity: PurityDomain.summary SafeLazy.t option
     ; racerd: RacerDDomain.summary SafeLazy.t option
diff --git a/infer/src/backend/registerCheckers.ml b/infer/src/backend/registerCheckers.ml
index d4277032..3496d371 100644
--- a/infer/src/backend/registerCheckers.ml
+++ b/infer/src/backend/registerCheckers.ml
@@ -154,6 +154,8 @@ let all_checkers =
                interprocedural later on *)
             interprocedural Payloads.Fields.lab_resource_leaks ResourceLeaks.checker
           , Java ) ] }
+  ; { checker= MiniOob
+    ; callbacks= [(interprocedural Payloads.Fields.mini_oob MiniOobChecker.checker, Clang)] }
   ; { checker= RacerD
     ; callbacks=
         (let racerd_proc = interprocedural Payloads.Fields.racerd RacerDProcAnalysis.analyze in
diff --git a/infer/src/base/Checker.mli b/infer/src/base/Checker.mli
index 8c74418e..f3a5b0c2 100644
--- a/infer/src/base/Checker.mli
+++ b/infer/src/base/Checker.mli
@@ -25,6 +25,7 @@ type t =
   | LithoRequiredProps
   | Liveness
   | LoopHoisting
+  | MiniOob
   | ParameterNotNullChecked
   | Pulse
   | PurityAnalysis
diff --git a/infer/src/base/IssueType.ml b/infer/src/base/IssueType.ml
index 1d2e975f..361ced96 100644
--- a/infer/src/base/IssueType.ml
+++ b/infer/src/base/IssueType.ml
@@ -659,6 +659,11 @@ let lab_resource_leak =
     ~user_documentation:"Toy issue."
 
 
+let mini_oob =
+  register ~category:NoCategory ~id:"MINI_OOB" Error MiniOob
+    ~user_documentation:"Array or pointer access that the MiniOob demo checker proved out of bounds."
+
+
 let lock_consistency_violation =
   register Warning ~id:"LOCK_CONSISTENCY_VIOLATION" ~category:Concurrency RacerD
     ~user_documentation:[%blob "./documentation/issues/LOCK_CONSISTENCY_VIOLATION.md"]
diff --git a/infer/src/base/IssueType.mli b/infer/src/base/IssueType.mli
index 5bddd4f5..4ee6afe5 100644
--- a/infer/src/base/IssueType.mli
+++ b/infer/src/base/IssueType.mli
@@ -209,6 +209,8 @@ val ipc_on_ui_thread : t
 
 val lab_resource_leak : t
 
+val mini_oob : t
+
 val lockless_violation : t
 
 val lock_consistency_violation : t
diff --git a/infer/src/base/PayloadId.ml b/infer/src/base/PayloadId.ml
index d0c3bd90..49490970 100644
--- a/infer/src/base/PayloadId.ml
+++ b/infer/src/base/PayloadId.ml
@@ -17,6 +17,7 @@ type t =
   | StaticConstructorStallChecker
   | LabResourceLeaks
   | LithoRequiredProps
+  | MiniOob
   | Pulse
   | Purity
   | RacerD
@@ -49,6 +50,8 @@ let to_checker payload_id : Checker.t =
       ResourceLeakLabExercise
   | LithoRequiredProps ->
       LithoRequiredProps
+  | MiniOob ->
+      MiniOob
   | Pulse ->
       Pulse
   | Purity ->
diff --git a/infer/src/base/PayloadId.mli b/infer/src/base/PayloadId.mli
index eea8e086..04e0cfc3 100644
--- a/infer/src/base/PayloadId.mli
+++ b/infer/src/base/PayloadId.mli
@@ -18,6 +18,7 @@ type t =
   | StaticConstructorStallChecker
   | LabResourceLeaks
   | LithoRequiredProps
+  | MiniOob
   | Pulse
   | Purity
   | RacerD
diff --git a/infer/src/backend/Payloads.ml b/infer/src/backend/Payloads.ml
index 47a0b1e5..2e78e78b 100644
--- a/infer/src/backend/Payloads.ml
+++ b/infer/src/backend/Payloads.ml
@@ -19,6 +19,7 @@ type t =
   ; static_constructor_stall_checker: StaticConstructorStallChecker.Summary.t SafeLazy.t option
   ; lab_resource_leaks: ResourceLeakDomain.summary SafeLazy.t option
   ; litho_required_props: LithoDomain.summary SafeLazy.t option
+  ; mini_oob: MiniOobDomain.summary SafeLazy.t option
   ; pulse: PulseSummary.t SafeLazy.t option
   ; purity: PurityDomain.summary SafeLazy.t option
   ; racerd: RacerDDomain.summary SafeLazy.t option
@@ -66,6 +67,7 @@ let all_fields =
     ~static_constructor_stall_checker:(fun f ->
       mk f StaticConstructorStallChecker StaticConstructorStallChecker.Summary.pp )
     ~litho_required_props:(fun f -> mk f LithoRequiredProps LithoDomain.pp_summary)
+    ~mini_oob:(fun f -> mk f MiniOob MiniOobDomain.pp_summary)
     ~pulse:(fun f -> mk_full f Pulse PulseSummary.pp)
     ~purity:(fun f -> mk f Purity PurityDomain.pp_summary)
     ~racerd:(fun f -> mk f RacerD RacerDDomain.pp_summary)
@@ -109,6 +111,7 @@ let empty =
   ; static_constructor_stall_checker= None
   ; lab_resource_leaks= None
   ; litho_required_props= None
+  ; mini_oob= None
   ; pulse= None
   ; purity= None
   ; racerd= None
@@ -130,6 +133,7 @@ let freeze t =
        ; static_constructor_stall_checker
        ; lab_resource_leaks
        ; litho_required_props
+       ; mini_oob
        ; pulse
        ; purity
        ; racerd
@@ -151,6 +155,7 @@ let freeze t =
   freeze static_constructor_stall_checker ;
   freeze lab_resource_leaks ;
   freeze litho_required_props ;
+  freeze mini_oob ;
   freeze pulse ;
   freeze purity ;
   freeze racerd ;
@@ -279,6 +284,7 @@ module SQLite = struct
     ; static_constructor_stall_checker= load ~proc_uid StaticConstructorStallChecker
     ; lab_resource_leaks= load ~proc_uid LabResourceLeaks
     ; litho_required_props= load ~proc_uid LithoRequiredProps
+    ; mini_oob= load ~proc_uid MiniOob
     ; pulse= load ~proc_uid Pulse
     ; purity= load ~proc_uid Purity
     ; racerd= load ~proc_uid RacerD
diff --git a/infer/src/base/Checker.ml b/infer/src/base/Checker.ml
index 52b9f1c8..e0886779 100644
--- a/infer/src/base/Checker.ml
+++ b/infer/src/base/Checker.ml
@@ -26,6 +26,7 @@ type t =
   | LithoRequiredProps
   | Liveness
   | LoopHoisting
+  | MiniOob
   | ParameterNotNullChecked
   | Pulse
   | PurityAnalysis
@@ -256,6 +257,21 @@ let config_unsafe checker =
       ; cli_flags= Some {deprecated= []; show_in_help= true}
       ; enabled_by_default= false
       ; activates= [BufferOverrunAnalysis; PurityAnalysis] }
+  | MiniOob ->
+      { id= "mini-oob"
+      ; kind=
+          UserFacing
+            { title= "Mini Out-Of-Bounds (summary demo)"
+            ; markdown_body=
+                "A deliberately small, summary-based array out-of-bounds checker written to \
+                 demonstrate Infer's interprocedural machinery: callee safety conditions are \
+                 stored in the summary and instantiated at each call site, never inlined." }
+      ; support= mk_support_func ~clang:Support ()
+      ; short_documentation=
+          "Minimal summary-based detector for out-of-bounds array accesses (demo checker)."
+      ; cli_flags= Some {deprecated= []; show_in_help= true}
+      ; enabled_by_default= false
+      ; activates= [] }
   | ParameterNotNullChecked ->
       { id= "parameter-not-null-checked"
       ; kind=
```

### 19.5 ★ 真实运行输出（本报告最重要的实证）

**用例（正是任务描述里指定的那个）**

```c
/* demo.c */
int read(int *p, int n, int i) { return p[i]; }

int foo(void) {
    int a[10];
    return read(a, 10, 20);      /* 20 >= 10 -> out of bounds, 只有在 foo 里才看得出来 */
}
```

```bash
infer run --mini-oob -o out -- gcc -c demo.c
```

**【实测数据】完整输出，未删减：**

```
Capturing in make/cc mode...
Found 1 source file to analyze in .../out
[MiniOOB] Analyze read
[MiniOOB]   open condition 0 <= arg2 < extent(arg0) at line 1, column 41
[MiniOOB] Generate Summary read: 0 <= arg2 < extent(arg0) at line 1, column 41
demo.c starting
demo.c DONE
[MiniOOB] Analyze foo
[MiniOOB] Load Summary read (1 open condition(s))
[MiniOOB] Apply Summary read
[MiniOOB]   discharged (VIOLATED): 0 <= 20 < extent(a[0..10)) at line 1, column 41
[MiniOOB] Generate Summary foo: (no open safety conditions)
demo.c starting
demo.c DONE

demo.c:5: error: Mini Oob(MINI_OOB)
  Out-of-bounds access inside `read`: 0 <= 20 < extent(a[0..10)) at line 1, column 41.
  3. int foo(void) {
  4.     int a[10];
  5.     return read(a, 10, 20);
                ^
  6. }

Found 1 issue
  Mini Oob(MINI_OOB): 1
```

日志序列与任务描述要求的 `Analyze read → Generate Summary read → Analyze foo → Load Summary read → Apply Summary read`
**完全一致**，且 `Analyze read` **严格早于** `Analyze foo`、**只出现一次**。

### 19.6 证明它确实用的是摘要而不是 Inline

三条独立证据：

**(1) 源码层**：`MiniOobChecker.ml` 中处理 `Call` 的分支里，**没有任何一行**加载 callee 的 `Procdesc`
或遍历其 CFG；唯一的跨过程操作是 `analyze_dependency callee`，其背后就是 §6 追踪过的
`Ondemand.analyze_proc_name → Summary.OnDisk.get`（缓存/SQLite 命中）或**一次只针对 callee 自身**的
`run_proc_analysis`。

**(2) 摘要真的落盘了**【实测数据】：

```
MiniOob column present: True | total columns: 22      ← 原 21 列 + 自动新增的 MiniOob 列
  read   MiniOob payload: 43 bytes  magic=8495a6be    ← OCaml Marshal
  foo    MiniOob payload: 22 bytes  magic=8495a6be
```

官方工具可读导出：

```
int read(int* p, int n, int i)
MiniOob: 0 <= arg2 < extent(arg0) at line 1, column 41
summary_loads=

int foo()
MiniOob: (no open safety conditions)
summary_loads= read          ← 依赖边被记录，增量分析可用
```

**(3) 多层传播实验**（这是"Summary Composition"最直观的一张图）【实测数据】：

```c
int leaf(int *p, int i) { return p[i]; }
int mid(int *q, int k)  { return leaf(q, k + 5); }
int top(int *r, int m)  { return mid(r, m + 2); }
int entry(void) { int a[10]; return top(a, 20); }   /* 20 → +2 → +5 = 27 >= 10 */
```

```
[MiniOOB] Analyze leaf
[MiniOOB]   open condition 0 <= arg1 < extent(arg0) at line 1, column 43
[MiniOOB] Generate Summary leaf: 0 <= arg1 < extent(arg0) at line 1, column 43
[MiniOOB] Analyze mid
[MiniOOB] Load Summary leaf (1 open condition(s))
[MiniOOB] Apply Summary leaf
[MiniOOB]   still open, propagating to our own summary: 0 <= arg1+5 < extent(arg0) at line 1, column 43
[MiniOOB] Generate Summary mid: 0 <= arg1+5 < extent(arg0) at line 1, column 43
[MiniOOB] Analyze top
[MiniOOB] Load Summary mid (1 open condition(s))
[MiniOOB] Apply Summary mid
[MiniOOB]   still open, propagating to our own summary: 0 <= arg1+7 < extent(arg0) at line 1, column 43
[MiniOOB] Generate Summary top: 0 <= arg1+7 < extent(arg0) at line 1, column 43
[MiniOOB] Analyze entry
[MiniOOB] Load Summary top (1 open condition(s))
[MiniOOB] Apply Summary top
[MiniOOB]   discharged (VIOLATED): 0 <= 27 < extent(a[0..10)) at line 1, column 43
[MiniOOB] Generate Summary entry: (no open safety conditions)

chain3.c:4: error: Mini Oob(MINI_OOB)
  Out-of-bounds access inside `top`: 0 <= 27 < extent(a[0..10)) at line 1, column 43.
ondemand_procs_analyzed: 4
```

条件 `0 <= arg1 < extent(arg0)` 沿调用链逐层代换为 `arg1+5`、`arg1+7`，
最后在 `entry` 才因为拿到具体的 `a[10]` 与 `20` 而被判定。**4 个过程各分析一次。**

### 19.7 20 层深链 + 阴性对照（证明没有"为了报而报"）

```c
int f20(int *p, int i) { return p[i]; }
int f19(int *p, int i) { return f20(p, i + 1); }   /* … 一直到 f1 */
…
int entry_bad(void)  { int a[10]; return f1(a, 5); }   /* 5 + 19 = 24 -> 越界 */
int entry_safe(void) { int a[64]; return f1(a, 5); }   /* 24 < 64 -> 安全 */
```

**【实测数据】**

```
time: 0.31s
[MiniOOB] Generate Summary f20: 0 <= arg1     < extent(arg0) at line 1, column 33
[MiniOOB] Generate Summary f2:  0 <= arg1+18  < extent(arg0) at line 1, column 33
[MiniOOB] Generate Summary f1:  0 <= arg1+19  < extent(arg0) at line 1, column 33
[MiniOOB]   discharged (VIOLATED): 0 <= 24 < extent(a[0..10)) at line 1, column 33
[MiniOOB]   discharged (safe):     0 <= 24 < extent(a[0..64)) at line 1, column 33

deep.c:21: error: Mini Oob(MINI_OOB)
  Out-of-bounds access inside `f1`: 0 <= 24 < extent(a[0..10)) at line 1, column 33.
Found 1 issue                       ← entry_safe 正确地没有报（无误报）
ondemand_procs_analyzed: 22
specs rows: 22
```

**20 层调用链，下标偏移被符号化地累加了 19 次，0.31 秒，1 个真阳性 0 个假阳性。**

### 19.8 已知局限（诚实声明）

MiniOobChecker 是**教学级**实现，刻意保持在 442 行（净代码约 300 行）。它**不**处理：
循环（`widen` 就是 `join`，循环变量一旦变化即退化为 `Top`，不会误报但会漏报）、
`malloc`（只认 `Typ.Tarray {length}` 形式的局部数组）、多级指针、结构体字段、全局变量、
非线性下标、函数指针、区间（`Idx` 只有"常量 / 形参+常量 / Top"三态，没有 `[lo,hi]`）。
把它补成生产级，需要的正是 §12 里 Infer 真实用的那套：`Itv` + `Bound`（含符号/非线性项）+
`ArrayBlk` + 阈值 widening。**这个"从几百行到生产级"的距离，恰恰是 §28 要量化的自研成本。**

---

## 20. 编译与运行：从零到跑通 Demo 的完整步骤

### 20.1 官方推荐：先用预编译二进制

**【文档】** `INSTALL.md` 原文：

> We provide a binary release of Infer. We encourage you to use this release as compiling infer's
> dependencies is time-consuming.

本报告 §13–§17 的所有实验都是用预编译二进制做的，**建议贵团队评估阶段同样优先走这条路**：

```bash
curl -L -o infer.tar.xz \
  https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
tar xf infer.tar.xz
export PATH=$PWD/infer-linux-x86_64-v1.3.0/bin:$PATH
infer --version
infer run --bufferoverrun-only -- gcc -c example.c
```

（本次实测：下载 276 MB 用时约 20 s，解压后 `clang/install` 占 935 MB，整包约 1.3 GB。）

### 20.2 系统依赖（Linux，`INSTALL.md` 原文）

* `opam >= 2.0.0`、`pkg-config`
* `Java >= 21`（仅 Java 分析需要）
* `gcc >= 5.x` 或 `clang >= 3.4`（C/ObjC 分析需要）
* `autoconf >= 2.63`、`automake >= 1.11.1`（从 git 构建时需要）
* `CMake`、`Ninja`（可选）
* `gmp`、`mpfr`
* `charon = 0.1.177`（**仅 Rust 分析需要**）

Debian/Ubuntu 一行装齐（取自官方 `docker/master/Dockerfile`）：

```bash
apt-get install --yes --no-install-recommends \
  autoconf automake bubblewrap bzip2 cmake curl g++ gcc git libc6-dev libgmp-dev libmpfr-dev \
  libsqlite3-dev sqlite3 make opam openjdk-11-jdk-headless patch patchelf pkg-config \
  python3 python3-distutils unzip xz-utils zlib1g-dev
```

### 20.3 标准源码构建（网络正常的机器上）

```bash
git clone https://github.com/facebook/infer.git
cd infer
git checkout 3d38229b04b649024fee065621f6a4c838f6d029   # 本报告的调研基线（v3.3 补）
./build-infer.sh clang        # 只构建 C/C++/ObjC 分析器
# 或 ./build-infer.sh --local-opam-switch clang   （把 opam switch 建在仓库内的 _opam/，不污染全局）
sudo make install             # 或 export PATH=$PWD/infer/bin:$PATH
infer --version
```

`build-infer.sh` 会依次：`opam init` → 建 switch（**OCaml 5.4.1+flambda**，脚本内
`INFER_OPAM_DEFAULT_SWITCH="5.4.1+flambda"`）→ pin 仓库内自带的
`dependencies/{charon,ppx_show,pyml,camlzip}` → `opam install --deps-only opam/infer.opam.locked`
→ `facebook-clang-plugins/clang/setup.sh`（**从源码编译一整套定制 LLVM/Clang，这一步最耗时**）→ `make`。

### 20.4 本次实际做的事：在一个受限沙箱里把它跑通（可复现的加速路径）

**本次没有从源码编译 LLVM/Clang**，而是复用预编译包里已经编译好的 clang，把编译时间从"数小时"压到"分钟级"。
这条路径对贵团队做 PoC 同样有价值：

```bash
# 1) 复用官方预编译包里的 clang，跳过最耗时的 LLVM 自举
cd infer/facebook-clang-plugins/clang
ln -sfn <解压出来的>/infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install install
SHASUM="sha256sum" ./setup.sh -r          # -r = --only-record-install，写 installed.version
SHASUM="sha256sum" ./setup.sh -c && echo "clang 被认为已安装"

# 2) 构建（关掉用不到的前端，尤其是 Rust——它依赖 charon）
cd <infer 根目录>
eval $(opam env)
./autogen.sh
./configure --disable-rust-analyzers --disable-java-analyzers --disable-erlang-analyzers \
            --disable-hack-analyzers --disable-python-analyzers --disable-swift-analyzers
make -j2 BUILD_MODE=default infer
./infer/bin/infer --version           # → Infer version v1.3.0-3d38229b
```

**注意**：`--disable-rust-analyzers` **不足以**让 dune 跳过 Rust 代码——
`infer/src/integration/dune.in` 无条件依赖 `charon` 库。本次为此额外做了三处改动
（已在 §20.7 如实记录为"环境限制导致的偏离"）：把 `infer/src/rust/RustMir2Textual.ml{,i}` 换成桩、
从 `integration/dune.in`、`src/dune.in`、`rust/dune` 里移除 `charon`、把 `integration/Rust.ml` 里
对 `Charon.UllbcOfJson.crate_of_json` 的调用换成 `L.die`。
**若贵团队的机器能正常访问 gitlab.inria.fr，直接 `opam install charon` 即可，不需要这些改动。**

### 20.5 跑 MiniOobChecker

```bash
cd <任意目录>
cat > demo.c <<'EOF'
int read(int *p, int n, int i) { return p[i]; }
int foo(void) { int a[10]; return read(a, 10, 20); }
EOF
<infer 根目录>/infer/bin/infer run --mini-oob -o out -- gcc -c demo.c
# 预期输出见 §19.5

# 看落盘的摘要：
<infer>/infer/bin/infer debug -o out --procedures --procedures-name \
     --procedures-summary --procedures-summary-skip-empty --select all
# 看 SQLite 里的原始 blob：
python3 - <<'PY'
import sqlite3; c=sqlite3.connect('out/results.db')
for uid,b in c.execute('select proc_uid, MiniOob from specs'):
    print(uid, len(b) if b else 0, b[:4].hex() if b else '')
PY
```

### 20.6 Docker（仓库内官方 Dockerfile 原文，未做任何修改）

**【源码确认】** `docker/master/Dockerfile`：

```dockerfile
FROM debian:bullseye-slim AS compilator
LABEL maintainer "Infer team"
RUN apt-get update && \
    mkdir -p /usr/share/man/man1 && \
    apt-get install --yes --no-install-recommends \
      autoconf automake bubblewrap bzip2 cmake curl g++ gcc git libc6-dev libgmp-dev \
      libmpfr-dev libsqlite3-dev sqlite3 make opam openjdk-11-jdk-headless patch patchelf \
      pkg-config python3 python3-distutils unzip xz-utils zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*
RUN opam init --reinit --bare --disable-sandboxing --yes --auto-setup
RUN cd / && git clone --depth 1 https://github.com/facebook/infer/
RUN cd /infer && ./build-infer.sh --only-setup-opam
RUN cd /infer && eval $(opam env) && ./autogen.sh && ./configure && \
    ./facebook-clang-plugins/clang/setup.sh
RUN cd /infer && make install-with-libs BUILD_MODE=opt PATCHELF=patchelf \
    DESTDIR="/infer-release" libdir_relative_to_bindir="../lib"

FROM debian:bullseye-slim AS executor
RUN apt-get update && apt-get install --yes --no-install-recommends sqlite3
COPY --from=compilator /infer-release/usr/local /infer
ENV PATH /infer/bin:${PATH}
RUN if test -d /infer-host; then cp -av /infer/. /infer-host; fi
```

```bash
docker build -t infer-from-source -f docker/master/Dockerfile .
docker run --rm infer-from-source infer --version
docker run --rm -v "$PWD/testcode:/testcode" infer-from-source \
  bash -c "cd /testcode && infer run --bufferoverrun-only -- gcc -c example.c"
```

要在 Docker 里验证 MiniOobChecker，在 `compilator` 阶段的 `git clone` 之后插入：

```dockerfile
COPY infer/src/labs/MiniOob*.ml  /infer/infer/src/labs/
COPY infer/src/labs/MiniOob*.mli /infer/infer/src/labs/
COPY minioob-registration.patch  /infer/
RUN cd /infer && git apply minioob-registration.patch     # §19.4 的 diff
```

**资源建议**【实测数据 + 推断】：磁盘 ≥ 20 GB（本次 opam switch + 依赖 4.6 GB、预编译 clang 0.9 GB、
`_build` 数 GB）；内存 ≥ 8 GB（本次 7.8 GB 全程够用）；CPU **越多越好**——本次 2 vCPU 下，
OCaml 编译器自举约 15 min、152 个 opam 依赖约 45 min、Infer 自身 `make -j2` 约 3–4 min。
**从源码完整构建（含 LLVM/Clang）官方文档明确警告"may take a really long time"，请预留半天。**


### 20.7 本次沙箱的出网策略与由此产生的构建偏离（一手记录）

这一节不是 Infer 的问题，而是**本次调研环境**的限制记录。写出来有两个用处：
(1) 它是"从源码构建 Infer 的真实门槛"最直接的一手数据；
(2) 贵团队如果在内网/受限环境里做 PoC，会遇到同一类问题，这里给出了可复用的解法。

**本沙箱允许的出网只有两种**（其余一律 403）：

| 可达 | 不可达（HTTP 403 / CONNECT 403） |
|---|---|
| `git clone` / `git fetch` over https（github.com） | `opam.ocaml.org`（opam 默认仓库与 cache） |
| `https://github.com/<o>/<r>/releases/download/…` | `raw.githubusercontent.com`、`codeload.github.com`、`github.com/<o>/<r>/archive/…` |
| | `erratique.ch`（fmt / fpath / astring / cmdliner…） |
| | `gitlab.inria.fr`、`gitlab.com`（menhir、visitors、unionFind、store、charon） |
| | `ftp.gnu.org`、`gmplib.org`、`archive.ubuntu.com`（apt）、`pypi.org` |

**为把构建跑通所做的变通**（全部是环境适配，不改变 Infer 语义）：

1. **opam 本体**：从 GitHub Releases 下载 `opam-2.4.1-x86_64-linux`。
2. **opam 仓库**：`git clone ocaml/opam-repository`，然后用脚本把 **6924 个** package 的
   `url { src: "https://github.com/o/r/archive/…tar.gz" }` 重写成 `"git+https://github.com/o/r#<tag>"`
   （并去掉该 url 块的 checksum——git 检出无法匹配 tarball 哈希）。
3. **`raw.githubusercontent.com` 上的 extra-source**（如 `ocaml-config` 的 `gen_ocaml_config.ml.in`、
   `bin_prot` 的 patch）：用 `git fetch --depth 1 origin <sha>` 取出那个确切 blob，
   按其**声明的 sha256** 塞进 `$OPAMROOT/download-cache/sha256/<xx>/<hash>`，opam 就不再走网络，
   **且校验和依然生效**（实测 git blob 与 opam 声明的 sha256 逐字节一致）。这是最干净的一招。
4. **erratique.ch 的包**：重写为 `git+https://github.com/dbuenzli/<pkg>#v<version>`（**100 个**包）。
5. **menhir**：官方源在 gitlab.inria.fr；改用 GitHub 镜像 `LexiFi/menhir` 的 `20231231` 标签
   （满足 `infer.opam` 的 `menhir >= 20220210`）。
6. **GMP**：apt 不可用、gmplib.org/ftp.gnu.org 被拦。从 `github.com/gmp-mirror/gmp-6.3` 克隆并 `./configure`
   生成 `gmp.h`，配合系统已有的 `libgmp.so.10`（GMP 6.3.0，ABI 匹配）建立 `libgmp.so` 软链接。
7. **`rsync` 缺失**：opam 用它拷贝 `file://` 与本地 pin 源。写了一个 60 行的 Python 替身
   （支持 `--delete` 与尾部斜杠语义、跳过悬空软链）。
8. **charon 不可达**（其依赖 `visitors`/`unionFind`/`store` 都在 gitlab）：
   从锁文件里去掉 charon 链，并把 Rust 前端从 dune 构建里摘掉（§20.4 已列出三处改动）。
9. **clang**：复用官方预编译包里的 `clang/install`（935 MB），用 `setup.sh -r` 记录版本哈希，
   跳过从源码编译 LLVM/Clang。

**真实耗时与资源**【实测数据】（2 vCPU / 7.8 GB）：

| 阶段 | 耗时 | 产物大小 |
|---|---:|---:|
| opam init + 仓库克隆与重写 | ~5 min | opam-repository ~1 GB |
| OCaml 5.4.1+flambda 自举 | ~14 min | — |
| 152 个 opam 依赖安装 | ~40 min（含多轮排障） | `$OPAMROOT` 3.8 GB |
| `autogen + configure + make -j2 infer` | ~13 min | `infer/_build` 1.3 GB，`infer` 二进制 125 MB |
| **合计（不含 LLVM/Clang 自举）** | **≈ 72 min** | **≈ 6 GB** |

**结论**【推断/建议】：在一台**网络正常**的 Linux 机器上，`./build-infer.sh clang` 应当能直接跑通，
主要成本是**从源码编译 LLVM/Clang**（官方 `INSTALL.md` 原文警告 "may take a really long time"）。
如果贵团队内网有 opam 镜像，建议先确认它是否可用——**这往往比"要不要用 Infer"更先决**。
若没有镜像，本节的 9 条变通（尤其第 3 条"用 git blob 填 opam download-cache"）可以直接复用。


---

## 21. OCaml 工程门槛评估

**学习 OCaml 本身的成本**：本次调研中，**从"完全不写 OCaml"到"写出一个编译通过、跑得对的跨过程 Checker"，
实际写的代码是 4 个文件共 442 行（含大量注释，净代码约 300 行）+ 9 个文件 37 行注册**。真正卡住的两次编译错误是：

1. 记录字段名冲突（`t` 和 `summary` 都有 `obligations` 字段，OCaml 按**后定义者优先**推断类型）；
2. `.mli` 与 `.ml` 的签名不匹配。

这两类都是 OCaml 特有、C++ 工程师不熟悉的坑，但**错误信息非常精确**（直接指到 `.mli` 第几行与 `.ml` 第几行），
定位不难。**读懂一个模块在做什么**比预想容易——Infer 的 `.mli` 文档注释质量很高，
本报告大量结论就是直接引用 `.mli` 注释得到的。

**真正陡峭的不是语言，是框架**：`AbstractInterpreter.MakeRPO` 这类 functor 组合、
`InterproceduralAnalysis.t` 各字段的隐含契约、`Payloads`/`PayloadId`/`Checker` 三处枚举必须保持同步、
`SafeLazy` 的惰性反序列化语义——这些只有 `labs/README.md` 一份教程，没有系统文档。

**Debug 成本**：`--debug --write-html` 产出的逐节点 HTML（本报告 §9.2、§14.4 的证据就是从那里挖出来的）
比"看日志"友好得多，`infer debug --procedures-summary` 可直接打印摘要。
但 OCaml 的运行时栈、模式匹配穷尽性警告对 C++ 工程师是新体验，初期效率会明显低于在 CSA 上。

**维护成本与上游变化**：Infer 迭代激进——本版本一次性**移除**了 Biabduction 和 Quandary，
**废弃**了包括 BufferOverrun 在内的 5 个 checker。基于它做深度定制必须持续跟踪上游、评估合并冲突。

### 21.1 给 C++ 工程师的"读 Infer 源码所需最小 OCaml 知识清单"

1. `let` / `let rec` / `match … with` / `fun` / `->` / 管道 `|>`（Infer 代码高频，读作"前一步结果传给后一步"）。
2. 代数数据类型 `type t = A | B of int | C of {x:int}` 与穷尽性模式匹配 —— 读懂 `Sil.instr`、`Checker.t` 的关键。
3. `'a option = None | Some of 'a`、`('a,'b) result = Ok of 'a | Error of 'b` ——
   `analyze_dependency` 的返回值全靠这两个类型表达"可能没有/可能失败"。
4. **模块与签名**：`.mli` 就是模块签名。**只读 `.mli` 往往就能搞清一个模块的职责边界——这是最高效的读法。**
5. **Functor**（`module F (X : SIG) = struct … end`）：`AbstractInterpreter.MakeRPO`、`TransferFunctions(CFG)`
   这类"用模块生成模块"的写法在框架核心大量出现，不懂 functor 基本读不了调度框架。
6. `[@@deriving compare/equal/hash/yojson_of/fields/enumerate]`：PPX 自动生成样板代码。
   看到时理解为"这里自动生成了下面会被调用但源码里搜不到定义的函数"即可。
   **特别注意 `[@@deriving fields]`**：`Payloads.Fields.mini_oob` 就是它生成的。
7. **不需要**掌握：GADT、多态变体高级用法、Effect handlers。本次阅读的核心分析模块基本没用到。

**估算**【推断/建议】：一名熟悉抽象解释/程序分析理论的工程师，专门投入 **1–2 周**学 OCaml 基础 + 读 `labs/`，
可以做出 MiniOobChecker 级别的原型；做到生产级（覆盖 TOP23 规则的复杂度、性能、误报率调优）
需以**月**为单位，且要先解决构建关（§20.7）。

---

## 22. CSA vs Infer vs 贵团队 CSA Summary 原型：技术维度对比

| 维度 | CSA（原生） | Infer v1.3.0 | 贵团队 CSA Summary 原型 |
|---|---|---|---|
| 分析方向 | 自顶向下，Caller 驱动 Inline Callee | **自底向上按需**（On-Demand），Callee 先出摘要 | 目标自底向上，但引擎本身自顶向下 |
| 执行模型 | 路径敏感符号执行（ExplodedGraph） | 过程内：BO = 非析取区间抽象解释；Pulse = 析取符号执行 | 路径敏感符号执行（复用 CSA 引擎） |
| IR | Clang AST + CFG（数百种 Stmt） | **Sil：5 条指令** | Clang AST + CFG |
| 路径敏感性 | 高（单函数内） | BO 低（join 后判定）；Pulse 高但有 20 条析取硬上限 | 高（单函数内），跨函数取决于摘要 |
| 上下文敏感性 | Inline 时天然，但受深度限制 | 摘要级参数化（形参代换）+ 可选 specialization | 依赖摘要设计，未成体系 |
| 内存/堆模型 | `MemRegion` / `RegionStore` | BO：`ArrayBlk`（Allocsite→offset/size/stride）；Pulse：地址图 | 复用 CSA `MemRegion`（**路径绑定、不可序列化**） |
| 别名 | `SymbolicRegion` + 约束求解 | BO：`PowLoc` 集合近似；Pulse：地址图精确 | 依赖 CSA 原生能力，摘要层未建模 |
| 循环 | Inline 展开/启发式，深度受限 | **Widen/Narrow 不动点 + 比较常量阈值**，保证终止 | ❌ 直接 `unsupported` |
| 递归 | 深度限制 + 截断 | `ActiveProcedures` 环检测 + 确定性重启（≤100 次），**实测可检出**（§14 #11） | 未见明确机制 |
| 跨过程 | 受 Inline 深度限制 | **核心能力，实测 200 层线性**（§15） | 目标能力，原型未达成 |
| 摘要 | 无原生概念 | **每分析独立 Payload**，SQLite 持久化 + 两层 LRU 缓存 | 自研，未形成稳定 schema |
| 不动点 / Widening | 无（Inline 代替） | 有，且有显式安全阀（`max_widens` / `pulse_widen_threshold`） | 无 |
| 增量分析 | 无原生支持 | **基于摘要依赖边 + 逐过程 CFG 结构比对**，实测可用（§17） | 无 |
| 扩展方式 | Checker 回调，学习曲线平缓，C++ 生态 | 从零写 Domain+TransferFunctions+Checker，**实测 442 行 + 37 行注册** | 复用 CSA Checker 机制（团队最熟） |
| C/C++ 前端 | 原生 Clang（贵团队用 **LLVM 14 fork**） | **定制 Clang 21.1.6**，与主线/贵团队 fork 均不同 | 原生（贵团队自有 LLVM 14 fork） |
| 越界能力现状 | 贵团队 TOP23 规则已积累多年调优 | **BufferOverrun 已 deprecated；Pulse 无数组越界 issue type**（§12.4） | 在建 |
| 可调试性 | 团队已有成熟流程 | `--debug --write-html`、`infer debug --procedures-summary` 很好用，但 OCaml 调试是新技能 | 团队已有成熟流程 |
| 工程复杂度 | 中 | 高（176 文件的 Pulse、自建构建系统、OCaml 生态） | 中，但摘要部分复杂度持续攀升 |

---

## 23. 当前 CSA Summary 方案 vs Infer：差距分类与逐项对应

| 类别 | 贵团队方案的问题 | Infer 的解法（含本报告证据出处） | 差距性质 |
|---|---|---|---|
| **A. Summary 表示** | 试图用一个 schema 覆盖参数/Range/内存访问/别名/路径约束/副作用 | Payload-per-Analysis：17 个独立类型，同表不同列（§5.2、§7） | **可直接迁移**，改造成本低 |
| **B. 抽象域不完整** | 基本等同 CSA 原生 `SVal`/`Range` | `Itv`（含符号 Bound + `MultB` 非线性项）+ `ArrayBlk`（Allocsite→offset/size/stride）（§5.3） | **需要新建**：独立于 `SVal` 的、可序列化的区间/数组块域 |
| **C. 循环抽象缺失** | 遇循环直接 `unsupported` | Widen/Narrow 两阶段不动点 + **比较常量阈值**（§9.2/§9.3） | **需要新建**，但阈值机制本身不到 10 行 |
| **D. 堆模型** | 复用 CSA `MemRegion`（与当前路径绑定） | 摘要内自成一体、与 Caller 状态解耦、靠代换绑定（§5.3、§13.3） | **需要新建**：摘要内的堆效应必须能脱离 `ProgramState` 独立存在并序列化 |
| **E. 别名模型** | 依赖 CSA 原生别名，摘要层未考虑传递 | BO 用 `PowLoc` 集合近似绕开路径分裂（§10，实测 case 7 检出） | **可迁移**：集合近似成本远低于精确别名 |
| **F. 跨过程不动点** | 无 | 过程内有不动点，**过程间默认一次性 On-Demand，不做全局多轮迭代**（§6.3） | **可迁移**：不需要设计全局摘要不动点，这是被验证够用的方案 |
| **G. Summary Composition** | "应用到 Caller"的规则不明确 | `ConditionSet.subst`：形参→实参代换 + `for_summary` 只保留 `propagate=true` 的条件（§12.1/§12.2/§13.3） | **需要新建**，但算法已由 §13.3 与 §19.6 展示得非常具体 |
| **H. 递归 / SCC** | 未见机制 | 调用栈式环检测 + 有限次确定性重启（≤100），实测 case 11 可检出 | **可迁移**：先做"检测到环即返回 Top 摘要"最简版即可 |
| **I. 增量摘要依赖** | 未见机制 | 摘要读取处埋点记依赖边 + `changed_files` 传递失效 + **逐过程 CFG 结构比对**（§17） | **可直接迁移，投入产出比最高**：只需在"摘要读取"处埋点，不动分析算法 |
| **J. 全局变量** | 支持有限 | **Infer 在这一点上比贵团队更差**（§14.3 实测 + 源码根因） | **必须自建**，不能照抄 |
| **K. 函数指针** | 不支持 | **Infer 同样不支持**（§14.4 真实 SIL 证据） | **必须自建**，不能照抄 |

---

## 24. ★ 直面那个尖锐问题：「CSA 生成摘要 + CSA 消费摘要」这条路线合理吗？

贵团队自己的判断是：*"我们很可能是在把 Path-sensitive Symbolic Execution Engine 硬改造成
Bottom-up Abstract Interpretation Framework。"*

**【推断/建议，但证据链完整】这个判断基本成立，并且本报告可以把它说得更精确。**

### 24.1 成立在哪里：状态的生命周期假设不兼容

CSA 的 `ProgramState` 是**路径绑定**的：它的意义永远是"沿着 ExplodedGraph 上这一条具体路径走到这里时的状态"。
`MemRegion` 是分析进程内的对象身份，`SVal` 里的符号绑定在当次 `ExplodedGraph` 的上下文里，
`ConstraintManager` 里的约束依附于当前路径。

Bottom-up 摘要要求的状态是**路径无关**的：必须能脱离任何具体调用点独立存在、能序列化到磁盘、
能在任意 Caller 上下文里被重新实例化。

这两者不是"实现难度"的差别，是**状态生命周期模型**的差别。
所以贵团队遇到的不是"实现得不够好"，而是：**每当你要把一个 CSA 状态变成摘要，你都必须先把它从路径里"拆下来"，
而这一步本质上就是在做抽象——loop abstraction、join、fixpoint、widening、heap abstraction、alias abstraction、
summary composition、recursive SCC 这 8 项，全都是"把路径绑定状态变成路径无关摘要"这件事的必要组成部分。
换任何底层引擎都绕不开。**

Infer 的选择恰好印证了这一点：它**没有**在某个路径敏感符号执行引擎上加摘要层，
而是**从零设计了一套独立的抽象解释框架**（`absint/`，92 个文件）来承载这些概念。

### 24.2 但"不合理"要分成两句话说

**第一句（不合理的部分）**：让 **CSA 引擎自己**完成摘要的抽象解释——
即在 CSA 的 `ExplodedGraph` 上做循环头 widen/narrow、做跨路径 join、做摘要不动点——
这条路会持续、必然地撞上上述 8 个问题，而且每一个都要自己从零实现一遍。**这部分判断成立。**

**第二句（合理的部分）**：用 **CSA 作为"事实采集器"**是完全合理的，而且是贵团队相对 Infer 的**优势**。
CSA 最擅长的事是"在一条具体路径上，精确地知道某次内存访问的 base 是谁、offset 是什么符号表达式、
当前路径上有哪些约束"。这正是生成高质量摘要所需的**原材料**。
Infer 的 BufferOverrun 因为域是非析取的，**拿不到**"这条越界只在 `flag != 0` 时发生"这种路径级信息
（§14 case 1 只能报 `[0,12]` 的 L2，而不能说"当 flag 为真时必然越界"）。

**所以结论不是"推倒重来"，而是"把职责切开"**——见 §25。

### 24.3 一个必须正视的额外风险

即使贵团队把这套做出来，还有一个**不是技术、而是产品**的问题：
Infer 自己已经把 BufferOverrun 标成 deprecated（§12.4），Pulse 又没接上数组越界的能力。
这说明**连 Meta 都没有在"工业级 C/C++ 数组越界"这条线上维持长期投入**。
贵团队如果决定自建，要有"这条路上没有可以长期跟随的开源参照物"的心理准备；
如果决定用 Infer，要有"你依赖的这条分析随时可能被上游删掉"的心理准备。**两条路都有这个风险，必须写在决策依据里。**

---

## 25. CSA Function Summary 2.0 架构建议

**设计原则：CSA 只做它结构上擅长的事（单函数、路径敏感的事实采集），
把"跨路径归并 / 跨函数组合 / 不动点"交给一个与 CSA 引擎解耦的新组件。**

```
                     ┌──────────────────────────────────────────────┐
   现有 CSA 引擎      │ ExplodedGraph 符号执行（单函数，路径敏感）       │
   （几乎不动）        │  Checker 回调：checkLocation / checkBind / …  │
                     └───────────────┬──────────────────────────────┘
                                     │ 产出【路径级事实】 PathFact：
                                     │  { access: read/write
                                     │  , base:   AccessPath (形参#k / 全局 / allocsite)
                                     │  , offset: SymExpr    (可含 a*b 非线性项)
                                     │  , elemSize, pathConstraints, loc }
                                     ▼
   ★ 新建：FactCollector（薄）→  FixpointEngine（独立小引擎，不依赖 ProgramState）
                                     │  · AbstractRange：区间 + 符号 Bound（含 MultB 非线性）
                                     │  · 循环头 widen/narrow，**阈值 = 路径约束里出现的常量**
                                     │  · 跨路径 join
                                     ▼
   ★ 新建：SummaryIR（可序列化，与 CSA 运行时对象无耦合）
        ├─ PreCondition  : 一组 (base, offsetRange, elemSize) 安全条件 + 可达性条件
        ├─ PostCondition : 返回值 / out 参数 / 全局 的抽象值
        ├─ MemoryEffect  : 读/写了哪些 AccessPath、写成了什么
        ├─ AliasEffect   : 形参之间/形参与返回值之间可能合流（集合近似即可）
        └─ GlobalEffect  : 全局变量作为一等公民单列
                                     ▼
   ★ 新建：SummaryManager（On-Demand 调度 + 递归环检测 + 两层缓存）
           SummaryDB     （SQLite：每分析一列 + 依赖边表）
           SummaryApplier（形参→实参代换；判定/再传播；判不了就往上传）
                                     ▼
                        现有 Checker 层（几乎不动）消费判定结果
```

> **【v2.3】本节已细化为可开工的设计文档。**
> 组件边界、数据结构（C++ 伪代码）、关键算法、与现有 CSA 的接口、工作量估算、
> 以及每条设计决策的**证伪条件**，见调研包 **`08-summary2-design/`** 三篇：
> `01-architecture.md` / `02-effort-and-milestones.md` / `03-assumptions-and-test-plan.md`。
>
> 其中 **`01-architecture.md` §1 修正了下面 §25.2 的一处结论**：
> 摘要生成不应以 CSA 的 ExplodedGraph 为**主要**事实源——否则 CSA 放弃探索的地方
> （循环预算、路径预算、inline 深度到顶）就没有事实可采，
> **现有的 `unsupported` 失败模式会被原封不动继承**。
> 正确结构是"独立 AI 为主（全函数性是硬约束）、CSA 路径信息为辅（精度增强）"。
> 这条修正本身也挂了证伪条件（见 `03-assumptions-and-test-plan.md` 的 T0-4）。
> **v3.6**：立项输入明确现有漏报的根因是跨多层时的**路径爆炸**——正是"CSA 放弃探索、无事实可采"的情形，这条修正据此成立，T0-4 不再需要。

### 25.1 哪些复用、哪些新建

**复用（不动）**：`SVal` / `Symbol` / `Range` / `ConstraintManager` 作为过程内符号执行的基础设施；
`Checker` / `CallEvent` 回调机制作为"规则怎么写"的入口——**团队现有 Checker 开发经验和工具链完全保留**。

**新建**（按投入产出比排序，也是落地顺序）：

| 优先级 | 组件 | 对标 Infer | 为什么先做 |
|---|---|---|---|
| P0 | **SummaryDB + 依赖边记录** | `specs` 表 + `Dependencies.record_pname_dep` + `AnalysisDependencyGraph.invalidate`（§17） | **几乎不触碰分析算法**，只在"读摘要"处埋点；直接支撑版本级 CI |
| P0 | **AbstractRange**（区间 + 符号 Bound + 非线性项保留） | `Itv` / `Bound.mk_MultB`（§5.3） | 是后面一切的地基；"能表示"比"能求解"重要得多 |
| P1 | **SummaryIR + SummaryApplier** | `ConditionSet` + `subst` + `for_summary`（§12.1/§13.3） | 有 §13.3 与 §19.6 两份逐层代换的真实样例可直接对照实现 |
| P1 | **SummaryManager**（On-Demand + 环检测 + 两层缓存） | `ondemand.ml` + `Summary.OnDisk.Cache`（§6） | 不需要全局拓扑排序，按需递归就够 |
| P2 | **FixpointEngine**（只服务摘要生成的小型抽象解释器） | `AbstractInterpreter` + 阈值 widening（§9） | 工作量最大，但可以先只覆盖"下标与循环变量线性相关"这一类 |
| P2 | **GlobalEffect**（全局一等公民） | **Infer 的反面教材**（§14.3） | 贵团队业务里全局数组很常见，不能重蹈 Infer 的缺口 |
| P3 | **库函数 / 内部工具函数模型表** | `bufferOverrunModels.ml`（§12.3） | 性价比高：比让引擎"读懂"实现划算得多 |

### 25.2 直接回答"Loop 怎么办？是不是应该 CSA 只负责收集事实？"

**建议：是。** 具体做法：

1. **CSA 端**：对循环**只做有限次展开**（比如 2 次，正是贵团队现在的做法），
   目的**不是**求出循环不变式，而是采集"循环体内第 1/2 次迭代时，下标表达式与循环变量的关系"这一**事实**
   （形如 `idx = i`、`idx = i*4 + 3`、`i` 的初值与步长、循环条件里出现的常量）。
2. **FixpointEngine 端**：拿这些事实做**递推式外推**——
   已知 `i₀ = 0`、`i_{n+1} = i_n + 1`、循环条件 `i <= 10`，就能直接得到 `i ∈ [0, 10]`，
   **根本不需要在 CSA 的 ExplodedGraph 上迭代到不动点**。
   对无法识别成简单递推的循环，退化为 `[-∞, +∞]`（Top），**但仍然产出一个合法摘要**，绝不 `unsupported`。
3. **阈值**：把循环条件与分支条件里出现的常量收进阈值集合（§9.3），这是保住精度的关键 10 行。

这样 CSA 引擎的职责边界保持清晰（路径敏感事实采集），新建的 FixpointEngine 承担 CSA 结构上不擅长的
"跨路径 / 跨迭代归并"，两者解耦、可独立演进和测试。

### 25.3 一条关于"精度上限"的重要提醒

**不要把 Summary 2.0 设计成"精度不低于现有 CSA 规则"。** 这是做不到的：
摘要的本质就是把一个函数的行为**有损压缩**成可组合的对象。
正确的目标是：**在现有规则之外，新增一条"跨函数"的检出通道，两者并存、各报各的**，
用 `L1/L2` 式的分级告警区分"确定越界"和"可能越界"（§12.2 的四级分类值得直接照搬），
让规则消费方按等级决定拦不拦。**如果硬要求"摘要路径必须不劣于 inline 路径"，这个项目大概率会失败。**

---

## 26. 直接使用 Infer 的可行性（问题 1 的正面回答）

| 提问 | 结论 | 依据 |
|---|---|---|
| 是否可以直接使用 Infer 现成的越界告警？ | **不可行** | §12.4：唯一有数组越界能力的 BufferOverrun 已 deprecated，Pulse 无替代；§14：全局变量、函数指针结构性漏报；告警分级与误报取舍是 Meta 按自己代码库调的 |
| 是否可以基于 Infer 写自己的 Checker？ | **技术上完全可行，已被本报告证明** | §19：442 行 + 37 行注册，真实编译、真实跑通、真实报出 20 层深链越界 |
| 是否可以扩展 Infer 的 Analysis？ | **可行，且是官方唯一推荐方式** | §18 |
| 是否可以复用 Infer 已有的 Interprocedural Framework？ | **可行**——`analyze_dependency` 一个回调就够，完全不用碰 Ondemand/SQLite | §18、§19.6 |
| 是否可以增加自己的 Abstract Domain / Transfer Function？ | **可行**，这就是标准做法 | §18、§19 |
| 是否可以复用 Pulse 框架？ | **不建议** | §18：176 文件的私有 API，学习成本远高于从零写专用区间域 |
| 能否"只借框架、规则仍用 C++/CSA 写"？ | **不可行** | Domain/TransferFunctions/Checker 紧耦合在同一 OCaml 进程与同一套 IR 上，Infer **没有**提供跨语言可插拔接口；跨语言 FFI 已超出官方支持范围 |
| 能否把 Infer 摘要导出给现有 C++ 工具消费？ | **不可行（直接消费）** | §7：磁盘格式是 OCaml Marshal；唯一可读通道是 `infer debug --procedures-summary` 的文本 / `--dump-json-summaries` 的 JSON（且 JSON 导出并非对所有 payload 都实现） |

**还有两个常被忽略的硬约束**：

* **前端版本冲突**：Infer 自带 **Clang 21.1.6**（Meta 定制分支）。贵团队用的是 **Huawei 内部 LLVM 14 fork**。
  这意味着：Infer 对贵团队代码的解析结果、宏展开、内建函数行为，与贵团队现有 CSA 链路**不是同一套**，
  两边的 TP/FP 不可直接对齐；且贵团队针对 LLVM 14 做的一切前端适配（内部扩展语法、编译选项）都要在 Infer 侧重做一遍。
* **构建可达性**：Infer 从源码构建强依赖 opam 生态（本次实测 152 个包）+ gitlab.inria.fr（menhir、charon 的依赖）。
  **这往往是比"要不要用 Infer"更先决的基础设施问题**（§20.7）。

---

## 27. 技术路线方案比较

| 维度 | **A. 继续当前 CSA Summary** | **B. 直接基于 Infer 二次开发** | **C. 借鉴 Infer 重设计 CSA Summary 2.0（推荐）** | **D. Infer 跨过程 + CSA 规则（混合）** |
|---|---|---|---|---|
| 开发成本 | 中→高（结构性缺口会让成本持续走高） | 高（规则全部用 OCaml 重写 + 建立 OCaml 工程能力） | 中高（新建 SummaryIR/FixpointEngine，复用 CSA 基础设施与 Checker 层） | 高（无官方接口，需自建集成协议 + 摘要格式转换） |
| 学习成本 | 低 | 高（OCaml + Infer 私有框架；本报告 §21 给了最小知识清单） | 中（借鉴设计，不需读懂 Infer 全部实现） | 高（两边都要懂） |
| 工程改造范围 | 局部（摘要模块） | 全部（几十个 CSA Checker 重写） | 局部→中（摘要模块 + 新增 FixpointEngine，Checker 层基本不动） | 大（双引擎并行维护 + 转换层） |
| 性能 | 未知 | 好（§15 实测 200 层亚秒级） | 预期较好（复用被验证过的 On-Demand + 持久化 + 非析取域设计） | 中（双引擎切换与格式转换开销） |
| 准确率 | 受 loop/alias 结构性问题限制 | 初期大概率不如现有 CSA 规则精细；且受 Infer 自身缺口限制（全局/函数指针） | 有机会同时保留 CSA 路径敏感精度与跨过程能力 | 上限高，但一致性风险最大 |
| 兼容现有 Checker | 完全兼容 | **不兼容，需重写** | **完全兼容**（Checker 层几乎不变） | 兼容，但需接入层 |
| 前端一致性 | 一致（自有 LLVM 14） | **不一致**（Infer 用 Clang 21.1.6） | 一致 | 不一致 |
| 落地周期 | 短期可迭代，长期天花板低 | 长（月→年级） | 中（按季度分阶段） | 最长 |
| 长期维护 | 差（问题会反复出现） | 中（依赖 Meta 上游；BufferOverrun 已 deprecated 是明确风险） | 好（架构清晰、职责分明、自主可控） | 差 |
| 技术风险 | 中（问题已知，解法未知） | **高**（技术栈迁移 + 上游废弃风险 + 前端不一致） | 中（新建模块有工程风险，但设计已被 Infer 验证） | 高 |
| 收益上限 | 低 | 中 | **高** | 理论最高，工程风险抵消大部分收益 |

---

## 28. 推荐技术路线

> **v3.7：本节写于立项之前，已被 `08-summary2-design/00-implementation-plan.md` 取代**（架构主次见其 D1，优先级见其 §4，目标深度为 2–17 层）。

### 短期（1–2 个月）：**C 起步 + 用官方二进制做零成本交叉验证**

1. **不改任何生产链路**，按 §29 的 PoC 计划，在 20–50 个真实业务函数上同时跑
   贵团队现有 CSA 原型 与 官方 Infer 预编译二进制（`--bufferoverrun-only`，并准备一份
   `--bo-debug 4 + L4/L5` 的高灵敏度对照），量化 Summary Coverage / 检出 / FP / FN 差异。
   把 Infer 当作"免费的第二意见 + 能力上限标尺"。
2. **并行启动 Summary 2.0 的 P0 两项**（§25.1）：
   * `SummaryDB` + **依赖边记录 + `changed_files` 传递失效**（§17）——几乎不碰分析算法，风险最低、见效最快，
     而且直接支撑"版本级静态分析"这个独立诉求；
   * `AbstractRange`（区间 + 符号 Bound + 非线性项保留）——后续一切的地基。
3. **立刻可做的一个小改进**（不需要等架构）：把**分支/循环条件里出现的常量收集为 widening 阈值**（§9.3），
   以及把越界告警做成 **L1/L2 分级**（§12.2）。这两项加起来不到一天，收益立竿见影。

### 中期（1–2 个季度）：**落地 SummaryIR / SummaryApplier / SummaryManager**

把"CSA 采集路径级事实 → 独立 FixpointEngine 归并 → SummaryIR 组合"的架构（§25）落到
**TOP23 内存越界规则族**这一类具体规则上，用 PoC 积累的真实函数集验证。
目标：这一类规则的有效分析深度做到 **10 层以上**，误报率不劣于当前原型，
**且允许摘要通道的精度低于 inline 通道**（§25.3）。

### 长期（半年以上）

视中期效果决定是否推广到其他规则族。持续跟踪 Infer 上游（**特别是 BufferOverrun 是否被真正删除、
Pulse 是否补上数组越界能力**），作为参照系；但**不建议任何阶段把生产越界分析主链路迁移到 Infer**。

**不建议方案 D**：本报告在源码与文档层面均未找到 Infer 提供跨语言集成接口的证据，
自建集成层的成本与收益不成正比。

---

## 29. 1–2 周 PoC 计划

> **v3.7：立项前的 PoC 建议，已过期**。实施计划见 `08-summary2-design/00-implementation-plan.md` §5。

| Day | 任务 | 产出 | 备注 |
|---|---|---|---|
| 1 | 环境：下载官方预编译二进制（**不要**先从源码构建，参考 §20.1 与 §20.7）；挑 20–50 个真实业务函数（**必须含**循环、数组访问、指针、结构体、别名、多层调用、全局数组） | 可用的 `infer` + 样本清单 | 20 分钟内可完成 |
| 2 | 复现本报告 §13.1/§13.2 基础实验 + §14 的 13 类场景矩阵，作为团队环境基线 | 复现记录，确认与本报告数据一致 | 全部命令本报告已给全 |
| 3 | 摘要 dump 练习：`infer debug --procedures-summary`、`sqlite3`/python 读 `specs` 表 | 团队工程师掌握"怎么看摘要" | §13.2、§7 |
| 4 | 把样本函数丢给 Infer，统计：能否生成 Safety conditions、条件形态、是否含符号项、是否被上层判定 | **Summary Coverage 的第一份真实数据** | 关键：统计"生成了条件但一直没被判定"的比例 |
| 5 | 深度实验：在团队代码库里挑天然的深层调用链复现 §15 | 真实业务下的深度/性能数据 | |
| 6–7 | 按 §20.4 在一台**网络正常**的机器上从源码构建 Infer，并按 §19 打上 MiniOobChecker，跑通 §19.5 的 demo | 一名工程师获得"能改 Infer"的实操经验 | 这一步同时是 OCaml 门槛的真实测量 |
| 8 | 把 MiniOobChecker 的思路在 **CSA 侧**搭一个最小对标原型（SummaryIR + 形参代换 + 分级判定，不做循环） | CSA 侧的对标物 | 目的：量化"同样的机制在 CSA 上要多写多少代码" |
| 9 | 挑典型差异案例（Infer 检出/CSA 漏报，或反之）做逐案根因分析 | 差异清单 + 根因结论 | |
| 10 | 数据整理 | 可用于专家评审的定量表格 | 见下方指标清单 |

**必须统计的指标**（建议做成一张表，每个函数一行）：
Summary 生成成功 / Unsupported 原因分类 / 生成的条件数 / 条件中符号项占比 /
分析耗时 / 峰值内存 / OOB 检出（TP）/ FP / FN / 该函数所在调用链深度 / 是否涉及全局/函数指针/循环。

**样本选择警告**：不要只挑"容易的"函数。**必须刻意包含全局数组和函数指针场景**，
因为这两类是 Infer 的结构性缺口（§14.3/§14.4），也是最能暴露"迁移到 Infer 能不能解决贵团队问题"的用例。

---

## 30. Infer 源码阅读路线（可执行的 Reading Map）

| 阶段 | 目录 / 核心文件 | 重点符号 | 阅读目的 | 建议用时 |
|---|---|---|---|---|
| 1. IR | `IR/{Sil,Exp,Procdesc,Cfg,Typ}.mli` | `Sil.instr`（5 个构造子）、`Procdesc.get_pvar_formals/get_locals`、`Cfg.t` | 知道分析面对的是什么 | 半天 |
| 2. 摘要全链路 | `backend/{ondemand,Summary,Payloads,CallbackOfChecker,callbacks,registerCheckers}.ml` | `analyze_callee`、`is_summary_already_computed`、`Summary.OnDisk.{get,store}`、`mk_interprocedural_t` | **本报告 §6 已逐行注释，建议对照边读边验证** | 1 天 |
| 3. 抽象解释框架 | `absint/{AbstractInterpreter,TransferFunctions,AbstractDomain,InterproceduralAnalysis,Reporting}.ml(i)` | `compute_post`、`MakeRPO`、`join_up_to_with_leq`、`VisitCount.succ` | 理解 WTO / widen / narrow / 析取上限 | 1 天 |
| 4. **动手** | `labs/README.md` + `labs/00_dummy_checker` → `04_interprocedural` | 六步练习 | 从"能读"到"能写"最短路径 | 2–3 天 |
| 5. BufferOverrun | `bufferoverrun/{itv,bounds,arrayBlk,bufferOverrunDomain,bufferOverrunProofObligations,bufferOverrunModels,bufferOverrunChecker}.ml` | `Val.t`、`ArrayBlk.make_c`、`ArrayAccessCondition.check`、`ConditionSet.{subst,for_summary}`、`Bound.mk_MultB` | **与贵团队目标最相关的一条分析** | 2–3 天 |
| 6. 增量 | `IR/Dependencies.mli`、`backend/AnalysisDependencyGraph.ml`、`IR/Procdesc.mark_if_unchanged` | `record_pname_dep`、`invalidate`、`is_structurally_equal` | **投入产出比最高的可迁移机制** | 半天 |
| 7. Pulse（选读） | `pulse/{PulseAbductiveDomain,PulseSummary,Pulse}.ml(i)` | 模块注释、`join_policy`/`widen_policy` | 理解 Bi-Abduction 的当代实现 | 1 天；**不建议深入全部 176 个文件** |

---

## 31. 风险与未确认问题（如实列出）

* **【未确认】** BufferOverrun 对**非试探性定义**的全局（如 `extern int g[10];` 在另一个 TU 定义）是否也漏报——
  本次只测了同 TU 内的试探性定义与带初始化器两种情形。
* **【未确认】** `--bo-debug 4 + L4/L5/U5` 这套高灵敏度配置在**真实业务代码规模**上的误报率——
  本次只在最小用例上观察到"对被调函数符号参数的不可证明访问"这一类噪声，没有量化。
* **【未确认】** Pulse 的 `--pulse-max-disjuncts` 调大到多少会在真实代码上出现不可接受的耗时——
  本次在人造用例上 200 条析取仍是 0.59 s。
* **【未确认】** Infer 在**编译数据库（`compile_commands.json`）+ 大型真实工程**下的端到端耗时与内存——
  本次全部实验都是单文件/少量文件规模。
* **【已确认但需注意是沙箱特有】** §20.7 记录的所有网络阻断都是本沙箱出网策略造成的，
  **不能外推为"Infer 从源码构建普遍困难"**。在能正常访问 opam.ocaml.org 与 gitlab.inria.fr 的机器上，
  `./build-infer.sh clang` 应当能直接跑通（只是耗时长）。
* **【推断】** §25 的 CSA Summary 2.0 架构是基于本次证据的工程判断，**不是 Infer 官方的任何指导文档**，
  需要贵团队做详细设计评审后再落地。

### 31.1 本版纠正/细化的若干常见误解

| 常见说法 | 本版结论 |
|---|---|
| "Infer 就是 Bi-Abduction" | 对当前版本**不准确**：`biabduction/` 已整体移除，思想保留在 `PulseAbductiveDomain`（§8.2） |
| "Pulse 已经取代 BufferOverrun" | **能力上没有**：Pulse 的 57 个 issue type 里没有任何数组越界类；实测 `--pulse-only` 一条越界都不报（§12.4） |
| "Infer 摘要是统一 schema" | **不是**：17 个 analysis-specific payload，同表不同列（§5.2、§7） |
| "Infer 能无限深度分析" | 默认**不限深度**（`ondemand_callchain_limit` 无默认值），实测 200 层线性；但这是**直线链**场景，且 Pulse 侧有 20 条析取硬上限（§11、§15） |
| "摘要解决了所有路径爆炸" | **没有**：爆炸从"调用点组合"转移到"单函数内析取数"，Infer 用**主动丢状态**兜底，代价是漏报（§11.1 实测丢弃 152 条） |
| "Infer 第二次跑会自动复用摘要" | **默认不会**：`infer analyze` 会先 `delete_all_specs()`，只有 `--incremental-analysis`/`--continue-analysis` 才复用（§17.3） |
| "循环里 Infer 会 widen 到 ±∞" | 不一定：比较常量会成为 widening 阈值，实测 `[0,1] → [0,9] → [0,10]`（§9.2/§9.3） |

---

## 32. 最终结论

1. **Infer 的摘要机制是真实、成熟、可验证的**：本报告从源码逐函数追踪了摘要的生成/存储/读取/组合全链路（§6），
   并把真实摘要打印了出来（§13.2）、把三层与二十层的形参→实参代换过程完整展示了出来（§13.3、§19.6/§19.7）。
   **"把有效分析深度从 2 层提到十几层"这个诉求，在直线调用链上已被实测证实为亚秒级、线性开销（§15）。**
2. **Infer 之所以不怕循环，靠的不是什么高深东西**：标准抽象解释（widen/narrow 不动点）
   + 一个极简的工程技巧（把比较常量收进 widening 阈值）。后者不到 10 行，**贵团队今天就可以抄**（§9.3）。
3. **但 Infer 不适合承载贵团队的越界规则**：唯一有数组越界能力的 BufferOverrun **已被官方废弃**，
   Pulse 没有接上这个能力（§12.4）；Infer 自身在全局数组（§14.3）和函数指针（§14.4）上有**结构性漏报**，
   其中全局数组这一项甚至**弱于贵团队现有 CSA**；前端是 Clang 21.1.6，与贵团队的 LLVM 14 fork 不一致。
4. **贵团队的自我判断成立，但要分成两句话**：让 CSA 引擎**自己**做摘要的抽象解释，
   确实是在把路径敏感符号执行引擎硬改造成自底向上抽象解释框架，会持续撞上 loop/join/fixpoint/widening/
   heap/alias/composition/SCC 这 8 个结构性必需品（§24.1）。
   但用 CSA 做**路径级事实采集**是合理的，而且是相对 Infer 的**优势**（§24.2）。
5. **推荐路线 C**：把 Infer 验证过的机制搬进一个与 CSA 引擎解耦的 Summary 2.0 架构——
   Payload-per-Analysis、On-Demand + 两层缓存 + SQLite 持久化、含符号/非线性 Bound 的区间域、
   阈值 widening、依赖图驱动的增量失效、分级告警、库函数模型表；
   让 CSA 继续做单函数路径敏感事实采集，把跨路径/跨迭代归并交给一个独立的小 FixpointEngine（§25）。
6. **下一步具体该做什么**（不是"值得学习"，是可以排期的动作）：
   * 本周：加"比较常量 → widening 阈值"（§9.3）；把越界告警改成 L1/L2 分级（§12.2）。
   * 本月：按 §29 跑完 10 天 PoC，拿到 20–50 个真实函数上的 Summary Coverage 定量数据。
   * 本季度：落地 P0 两项——SummaryDB + 依赖边失效（§17）、AbstractRange（§5.3）。
   * 下季度：SummaryIR + SummaryApplier + SummaryManager，先只覆盖 TOP23 越界规则族。

---

## 33. 《核心问题结论》

### Q1：Infer 的 Function Summary 本质上是什么？

**结论**：不是一个统一数据结构，而是**"每种具体分析各自定义一个可序列化的 Payload 类型，
统一挂在同一张 SQLite 表的不同列上"**。`Payloads.t` 的 17 个可选字段 ↔ `PayloadId.t` 的 17 个变体
↔ `specs` 表的 17 个 BLOB 列（实测共 21 列 = 4 固定 + 17 payload；加上我们自建的 MiniOob 变成 22 列）。
对内存越界最相关的具体形态是 **BufferOverrunChecker 的"安全条件集合"**，真实文本形如
`buf.offset + i < buf.length at example.c:1:42 reachable when { }`；
BufferOverrunAnalysis 的抽象内存是 `Val = (Itv, PowLoc, ArrayBlk)`；
Pulse 的摘要是一组析取的分离逻辑 pre/post。
**核心依据**：§5.2、§5.3、§7、§13.2、§13.3（全部有真实输出）。
**风险/限制**：不存在可直接照搬到贵团队所有规则族的"万能摘要格式"；
磁盘格式是 OCaml Marshal，C++ 工具无法直接消费。

### Q2：Infer 为什么能处理我们当前方案处理不了的 Loop / 复杂 CFG？

**结论**：因为它用的是标准抽象解释（区间抽象域 + widen/narrow 两阶段不动点 + **比较常量作为 widening 阈值**），
**失败模式是"精度可控降级"而不是"放弃生成摘要"**。实测循环头状态演化：
`i: 0 → join [0,1] → widen [0,9] → join [0,10] → widen [0,10] → Fixpoint reached`，
全程只访问 loop head 三四次，与循环真实迭代次数无关。
**核心依据**：§9.1（框架源码）、§9.2（真实 widening 轨迹）、§9.3（阈值来源 `bufferOverrunDomain.ml:392-398`）、
§14（13 类场景中所有循环用例的实测结果）。
**风险/限制**：抽象解释保证**终止**不保证**精确**；case 3（while + 符号条件）与 case 13a（循环+goto）
在默认配置下仍漏报，需要 `--bo-debug 4 + L4/L5` 才检出，代价是引入符号噪声（§14.2）。

### Q3：Infer 的 Summary 如何生成、存储、读取和组合？

**结论**：
* **生成**：Caller 分析到 `Call` 时调用框架注入的 `analyze_dependency`（内部即 `Ondemand.analyze_proc_name`），
  若无摘要则**就地递归**触发 Callee 的一次独立分析（`run_proc_analysis` → `Callbacks.iterate_procedure_callbacks`
  → 各 Checker 的 `AbstractInterpreter.compute_post`）。
* **存储**：`Summary.OnDisk.store` → `DBWriter.store_spec` → SQLite `specs` 表对应 payload 列，
  **OCaml Marshal 二进制**（实测魔数 `0x8495a6be`）。
* **读取**：`Summary.OnDisk.get` 先查**两层内存 LRU 缓存**（proc_name → analysis_req → summary），
  未命中执行 `SELECT report_summary, summary_metadata FROM specs WHERE proc_uid = :k`，
  payload 列由 `Payloads.SQLite.lazy_load` **惰性反序列化**（不需要的列不读）。
* **组合**：`ConditionSet.subst` 做形参→实参代换（实测：`base.offset+idx` → `arr.offset+k+1` → `p.offset+2.n+1`），
  `check` 判级（L1/L2/S2/SymbolicIssue/L3-L5），`for_summary` 只保留 `propagate = true` 的条件继续上传。
  返回值绑定到 `Call` 的 `ret_id`；全局变量以 `Pvar` 形式独立出现。
**核心依据**：§6（含行号的完整调用链）、§7（真实 schema + Marshal 魔数）、§12.1/§12.2（subst/check/for_summary 源码）、
§13.3（三层代换实测）、§19.6（自建 Checker 的四层代换实测）。
**风险/限制**：全局变量的摘要路径存在结构性缺口（§14.3 已定位源码根因）。

### Q4：Infer 是否真的可以避免函数调用层次增加造成的指数级路径爆炸？

**结论**：**在"层与层之间"是的，在"单个过程内部"不是。**
层间：每层摘要在离开该层时被压缩（BO 用 join 压成区间，Pulse 裁剪到 ≤20 条析取），
所以层间是**加法**不是乘法——实测 20 层 × 每层 4 分支（理论 4²⁰ ≈ 10¹² 条路径）：BO 0.28 s、Pulse 0.52 s。
层内：Pulse 的析取数有 `pulse_max_disjuncts = 20` 的硬上限，`pulse_widen_threshold = 3` 的循环上限，
**超限即主动丢状态做欠近似（漏报）**——实测一个 4096 路径的函数丢弃 152 条状态。
非析取的 BufferOverrun 不受此影响（同一函数一次 join 得 `[0,4095]`），代价是失去路径敏感性。
**核心依据**：§11（源码 + 默认值 + 丢弃计数实测）、§15.1（分支压力实验）、§16（理论对比）。
**风险/限制**：**"能不能扛住爆炸"主要由抽象域是否析取决定，而非"用不用摘要"**——
贵团队若坚持路径敏感（析取）摘要，爆炸只会转移不会消失。

### Q5：Infer 分析 10~20 层函数调用在工程上是否可行？

**结论**：**可行，且余量很大。** 实测 2 / 5 / 10 / 20 / 50 / 100 / 200 层直线调用链：
耗时 0.26 / 0.25 / 0.26 / 0.30 / 0.33 / 0.39 / 0.76 秒；`specs` 行数 = 过程数（严格线性）；
峰值 RSS 178 → 189 MB（几乎不增长）；`ondemand_procs_analyzed` = 过程数（每个恰好分析一次）。
**我们自建的 MiniOobChecker 同样做到了 20 层（0.31 s，22 个过程各一次，1 真阳 0 假阳）。**
默认**没有**最大调用深度限制（`ondemand_callchain_limit` 是可选项、无默认值）。
**核心依据**：§15、§19.7。
**风险/限制**：实验是直线链与"每层多分支"两种；真实业务里深链常伴随循环 + 别名 + 全局，
需按 §29 PoC 在真实函数上复测。

### Q6：我们能否像开发 CSA Checker 一样在 Infer 中开发自己的越界规则？

**结论**：**可以，而且本报告已经做完并跑通了。** 但开发范式完全不同——不是"挂回调"，
而是"实现 Domain + TransferFunctions + checker 三件套，再在 6 个文件里注册"。
**实测成本：4 个新文件共 442 行 OCaml（含注释）+ 9 个文件 37 行注册改动，不需要新建或修改任何 `dune` 文件**
（把代码放进已有的 `infer/src/labs/` 库即可，`specs` 表的新列由 `PayloadId` 自动生成）。
真实运行输出见 §19.5/§19.6/§19.7。
**核心依据**：§18（扩展机制源码 + 官方 lab 原文）、§19（完整源码 + 真实 diff + 真实日志）。
**风险/限制**：把这份教学版补成生产级（区间、malloc、结构体、全局、非线性、循环）
是数量级更大的工作；且要先解决源码构建关（§20、§20.7）。

### Q7：能否只复用 Infer 的 Abstract Interpretation / Summary Framework，自己只写 Domain 和 Transfer Function？

**结论**：**在 Infer 内部——可以，这正是官方唯一推荐的方式**，`analyze_dependency` 一个回调就把
Ondemand / SQLite / LRU 全部封装掉了（§18、§19 已证明）。
**但"把框架抽出来接给 C++/CSA 用"——不可以**：Domain/TransferFunctions/Checker 紧耦合在同一 OCaml 进程、
同一套 Sil IR 之上，Infer 没有设计跨语言可插拔接口，摘要磁盘格式又是 OCaml Marshal。
**核心依据**：§18、§26。
**风险/限制**：这条路径的产出只服务于"在 Infer 里重做一套分析"，无法反哺贵团队现有 CSA 体系。

### Q8：当前"CSA 生成 Summary + CSA 消费 Summary"的技术路线是否合理？

**结论**：**部分不合理，贵团队的自我判断基本成立，但需要拆成两句话。**
不合理的是：让 CSA 引擎**自己**在 `ExplodedGraph` 上做循环 widen/narrow、跨路径 join、摘要不动点——
CSA 的 `ProgramState` 是**路径绑定**的，而摘要要求**路径无关、可序列化、可任意实例化**，
这是两种不兼容的状态生命周期假设；沿这条路走**必然**持续遇到 loop abstraction / join / fixpoint / widening /
heap abstraction / alias abstraction / summary composition / recursive SCC，因为它们是
"把路径绑定状态变成路径无关摘要"这件事的**结构性必需品**，换任何引擎都绕不开
（Infer 的选择恰好印证：它没有在符号执行引擎上加摘要层，而是**从零建了 92 个文件的独立抽象解释框架**）。
合理的是：用 CSA 做**路径级事实采集**是对的，而且是相对 Infer 的优势——
Infer 的 BufferOverrun 因为域非析取，拿不到"这条越界只在 `flag != 0` 时发生"这种路径级信息。
**正确做法是把职责切开**（§25）：CSA 采集事实，独立的小 FixpointEngine 做归并与不动点。
**核心依据**：§24 全章、§25、§9、§11.1。
**风险/限制**：这是工程判断（【推断/建议】），不是可被源码证明的定理；
但证据链（Infer 自己另建框架、析取域必须丢状态、CSA 状态不可序列化）是完整的。

### Q9：如果继续 CSA 路线，最应该从 Infer 借鉴哪 5~10 个核心机制？

按**投入产出比**排序：

1. **依赖边记录 + `changed_files` 传递失效的增量模型**（§17）——只需在"读摘要"处埋点，
   不动分析算法，直接支撑版本级 CI。**投入产出比第一。**
2. **把比较/循环条件里的常量收集为 widening 阈值**（§9.3）——不到 10 行，是"循环里保住精度"的全部秘密。
3. **分级告警**（`L1` 必然越界 / `L2` 很可能 / `S2` 符号可疑 / `SymbolicIssue` 只传不报）（§12.2）——
   把"报不报"从二元决策变成可调策略，直接服务"低漏报 + 控误报"的工业诉求。
4. **Payload-per-Analysis 而非大一统 schema**（§5.2）——改造成本低，立即可做。
5. **安全条件（proof obligation）作为摘要主体 + `subst` + `for_summary`**（§12.1、§13.3）——
   摘要不必描述函数的全部行为，只需携带"还没判定的 requires"。这是整个设计里最优雅的一点。
6. **含符号 Bound（支持 `c + x*y` 非线性项）的区间抽象域**（§5.3）——"能表示"比"能求解"重要。
7. **On-Demand 调度 + 两层缓存（内存 LRU + 磁盘 SQLite）**（§6、§7）——不需要全局拓扑排序。
8. **逐过程 CFG 结构比对判断"是否变更"**（§17.1）——改注释/改同文件其他函数不触发重算。
9. **库函数 / 内部工具函数模型表**（§12.3）——绕开大量 unsupported 最划算的手段。
10. **独立于 CSA 引擎的轻量 FixpointEngine**（§25.2）——只服务"跨路径/跨迭代归并"这一件事。

**另外两条是"反面借鉴"**：全局变量必须作为一等公民单独建模（Infer 在此有结构性缺口，§14.3）；
C 函数指针调用需要专门建模（Infer 未覆盖，§14.4）。

### Q10：下一阶段最值得投入的技术路线是什么？

**结论**：**方案 C——借鉴 Infer 核心机制、重新设计 CSA Summary 2.0**，
同时用官方预编译 Infer 做零成本交叉验证。
**不建议**方案 B（直接迁移到 Infer）：唯一有数组越界能力的 BufferOverrun 已被官方废弃、
Pulse 未补上替代能力、Infer 自身在全局数组/函数指针上有结构性漏报、前端 Clang 版本与贵团队 LLVM 14 fork 不一致、
几十个现有 CSA Checker 需用 OCaml 重写。
**不建议**方案 A（维持现状）：结构性缺口会让成本持续走高而天花板不变。
**不建议**方案 D（混合）：无官方集成接口，自建集成层成本与收益不成正比。
**核心依据**：§26、§27、§28。
**风险/限制**：Summary 2.0 的具体落地细节需贵团队做详细技术设计评审；
本报告给出的是方向性的、有源码与实验证据支撑的建议，不是可直接开工的详细设计文档。
同时必须接受一个前提：**摘要通道的精度天然低于 inline 通道，目标应是"新增一条跨函数检出通道"
而不是"摘要不劣于 inline"**（§25.3）。


---
---

# 附录（v2 增补）

> 以下三个附录是本报告第二轮增补的内容，回应三个具体问题：
> 全量工程怎么分析、每一步能观测到什么、以及"关键技术点是不是都覆盖到了"。
> 详细展开在配套文档里，这里只给结论与索引。

---

## 附录 A：执行流程与可观测点速查

**完整版见调研包 `06-practice/02-observability-handbook.md`，并配有一键 dump 脚本 `observe.sh`。**

```
 阶段            命令                 落盘产物                  怎么看
 ─────────────────────────────────────────────────────────────────────────────
 ① capture      infer capture        capture.db               SQL / --store-textual
    Clang AST → SIL                  ├ source_files 表          infer debug --procedures-cfg
                                     └ procedures 表（CFG）
 ② analyze      infer analyze        results.db               ★ infer debug --procedures-summary
    不动点 + 按需跨过程                └ specs 表（17 payload 列）  SQL 查 payload 矩阵
                                     stats/stats.txt           计数器
                                     logs / captured/*.html    --debug 时逐节点状态
 ③ report       infer report         report.json / .txt        jq / python
                                     differential/*.json       infer reportdiff
```

**四个最该知道的观测入口**（都是本次调研中真正用上的）：

| 想看什么 | 命令 | 备注 |
|---|---|---|
| **摘要** | `infer debug --procedures --procedures-name --procedures-summary --procedures-summary-skip-empty --select all` | **不是** `--dump-json-summaries`（那个只导出 Pulse，BufferOverrun 的摘要不在里面）——这是最容易走弯路的一点 |
| **IR（SIL）文本** | `infer capture --store-textual …` + `infer debug --export-textual <dir>` | 产出 `.sil` 文件。§14.4 定位函数指针漏报根因用的就是这个 |
| **逐节点抽象状态** | `infer run --debug` → `infer-out/captured/*/nodes/*.html` | §9.2 的 widening 轨迹从这里挖出来 |
| **分析行为计数器** | `cat infer-out/stats/stats.txt` | `ondemand_procs_analyzed` / `pulse_disjuncts_dropped` / `pulse_interrupted_loops` 是上真实代码库后最该盯的三个 |

**数据库要点**（`sqlite3` 可直接查的部分）：

* `capture.db.source_files(source_file, freshly_captured, textual)` —— capture 成功率自检、增量判定依据。
* `capture.db.procedures(proc_uid, cfg, callees)` —— `length(cfg)` 排序即可找出超大函数。
* `results.db.specs` —— **21 列 = 4 固定 + 17 payload**；哪列非空 = 哪种分析产出了摘要。
* 所有 BLOB 都是 **OCaml Marshal**（魔数 `0x8495A6BE`），SQL 读不出内容，必须走 `infer debug`。

**观测点 → 可借鉴点的对应**（这是"先看得见再谈借鉴"的落点）：

| 观测到的 | 报告结论 | 对 CSA Summary 2.0 的启示 |
|---|---|---|
| `specs` 17 个 payload 列 | §5.2 | 摘要按分析分列存，不要大一统 schema |
| `Safety conditions: { … }` | §12.1 | **摘要主体 = "还没判定的 requires"**，不是函数全部行为 |
| 条件里的 `at <file>:<line>` 不随代换改变 | §13.3 | 代换时保留原始访问位置，报告才能归因到根因文件 |
| `summary_loads` / `type environments from sources` | §17 | 依赖边在"读摘要"处埋点即可；失效要算上类型依赖 |
| `pulse_disjuncts_dropped` | §11.1 | 析取域必须有丢弃策略，**且要把丢弃量做成可观测指标** |
| `report.json` 的 `hash` / `bug_trace` | 附录 A | 告警指纹 + 跨过程路径是 CI 落地的基础设施，要早做 |

---

## 附录 B：全量工程分析（compile_commands.json）

**完整版见调研包 `06-practice/01-whole-program-analysis.md`。**

### 结论

**capture 按文件，analyze 一次全量。跨文件信息在 analyze 阶段完全保留。**

```bash
infer capture --compilation-database compile_commands.json   # 每个文件用自己的 -I/-D，结果累积进同一个 capture.db
infer analyze --bufferoverrun-only                            # ★ 这一步是全量的，不认文件边界
infer report
```

真正会丢信息的是"每个文件一个 `infer-out`"——那才叫逐文件分析。

### 实测（三文件工程，越界只有合起来才看得见）

| 方式 | 结果 |
|---|---|
| **全量分析** | `src/main.c:5: BUFFER_OVERRUN_L1  Offset: 17 Size: 10 by call to \`mid_fetch\`` |
| 逐文件分析（3 个独立 infer-out） | access.c → 0 条；middle.c → 0 条；main.c → 0 条。**全丢** |

`Offset: 17` = `5`（来自 `main.c`）+ `12`（来自 **`middle.c` 自己的 `-DIDX_BIAS=12`**），
`Size: 10` 来自 `buf.h`。**三个文件、三套编译参数的信息在分析阶段被正确合并。**

跨文件的摘要链（真实输出）：

```
buf_get    : { i < 10 at src/access.c:3:44 }            summary_loads=
mid_fetch  : { k + 12 < 10 at src/access.c:3:44 }       summary_loads= buf_get       ← 跨文件依赖边
             { (k + 12):signed32 at src/middle.c:6:46 } type environments from sources= include/buf.h
main       : { }  → 报 L1                                summary_loads= mid_fetch
```

**MiniOobChecker 在全量模式下同样工作**（实测跨三文件，`arg1` → `arg1+12` → 判定为 `0 <= 17 < extent(a[0..10))`）；
但它不处理 `b->data[i]` 这种 struct 字段数组，这是它作为教学级实现的已知局限，不是全量分析的问题。

### 上真实代码库前必做的两件事

1. **量化 capture 成功率**：`compile_commands.json` 的条目数 vs `capture.db.source_files` 的行数。
   **翻译失败的文件在分析阶段等于不存在，是最隐蔽的漏报来源。** 贵团队用的是 Huawei 内部 LLVM 14 fork，
   Infer 自带定制 Clang 21.1.6，两边对内部扩展语法的支持不一定一致——这个数字是"能不能用 Infer"的硬门槛。
2. **先切子集试水**，别一上来全量。

---

## 附录 C：技术点覆盖自查（诚实版）

被问到"报告是不是把 Infer 的关键技术点都涉及到了"。逐条自查如下。

### C.1 已经展开、且有源码 + 实测双重证据的

| 技术点 | 章节 | 证据强度 |
|---|---|---|
| IR / SIL 五条指令、CFG、Procdesc | §4 | 源码 + 真实 SIL 输出 |
| 摘要数据结构（Payloads / PayloadId / specs 表） | §5、§7 | 源码 + 真实 schema + Marshal 魔数 |
| 摘要生成/存储/读取/组合全链路（含行号） | §6 | 源码逐函数追踪 |
| On-Demand 调度、递归环检测、调用链上限 | §6.3 | 源码 + 默认值 |
| 抽象解释框架、WTO、widen/narrow、阈值来源 | §9 | 源码 + **逐轮真实 widening 轨迹** |
| BufferOverrun 抽象域（Itv / Bound / ArrayBlk / PowLoc） | §5.3、§10 | 源码 |
| 证明义务与判级（L1/L2/S2/SymbolicIssue/L3-L5） | §12.2 | 源码 + 默认启用状态 |
| 摘要代换（ConditionSet.subst / for_summary） | §12.1、§13.3 | 源码 + **三层代换实测** |
| Pulse 析取上限与主动丢状态 | §11 | 源码 + 丢弃计数实测 |
| Bi-Abduction 思想与当前落地形态 | §8.2 | 源码注释 + Pulse 摘要实测 |
| 增量分析（依赖边、传递失效、结构比对） | §17 | 源码 + 三场景实测 |
| 扩展机制（Domain + TransferFunctions + 注册） | §18、§19 | **完整实现并编译跑通** |
| 调用深度与路径爆炸 | §15、§16 | 实测 |
| 全量工程分析 | 附录 B | 实测 |
| 可观测点 | 附录 A | 实测 |

### C.2 报告提到但**没有充分展开**的（如果要深入，这些是下一批）

| 技术点 | 现状 | 对我们重要吗 |
|---|---|---|
| **Clang 前端翻译细节**（`infer/src/clang/` 84 个文件：AST → SIL 具体怎么降级） | 只在 §4.2 给了几个真实降级样例 | **中**。真要评估"Infer 能不能吃下我们的代码"，这一块的翻译覆盖率比分析本身更关键 |
| **Specialization（摘要特化）** | 只在 §6.2 / §22 提了一句 | **高**。它是 Infer 做上下文敏感的手段，与我们"摘要精度不够怎么办"直接相关，值得单独看 `ondemand.ml` 的 `AddNewSpecialization` 分支 |
| **调度器**（File / CallGraph / Restart）与并行模型 | 只在 §3 / 附录 B 提了一句 | **中**。全量分析上真实代码库时的性能主要由它决定 |
| **PulseModels**（对库函数/内建函数的建模框架） | §12.3 只讲了 BufferOverrun 的 `bufferOverrunModels.ml` | **中高**。"为高频库函数手写模型"是我们绕开 unsupported 最划算的手段，值得看 Pulse 那套更完整的模型 DSL |
| **告警去重 / issue hash / reportdiff** | 附录 A 列了字段，没讲 hash 怎么算 | **高**（CI 落地必需）。`report.json` 的 `hash` 字段算法值得单独摸清 |
| **Textual 语言本身**（Infer 新的中间语言，前端与后端的解耦层） | 只当观测手段用了 | **中**。它是 Infer 接新语言的通道，如果我们想"喂自己的 IR 给 Infer 分析"，这是唯一入口 |
| **Cost / RacerD / Starvation 等其它 checker** | 基本没提 | **低**。与内存越界无关 |
| **Java / Hack / Erlang / Python 前端** | 没提 | **低** |

### C.3 明确未确认、需要真实代码库才能回答的

* Infer 对贵团队代码的 **capture 成功率**（内部扩展语法兼容性）——这是"能不能用"的第一道门槛。
* 大型工程（万级文件）的**端到端耗时与内存**；本次全部实验都是单文件/少量文件规模。
* 高灵敏度配置（`--bo-debug 4` + L4/L5）在真实代码上的**误报率**。
* `--pulse-max-disjuncts` 调大到多少会在真实代码上不可接受。

### C.4 结论

**对"我们要做的技术决策"而言，关键技术点已经覆盖完整**：
摘要机制、抽象解释与循环、跨过程与路径爆炸、增量、扩展方式、以及 Infer 自身的能力缺口，
都有源码 + 实测的双重证据，足以支撑 §27/§28 的路线判断。

**但如果下一步要真的把 Infer 用起来（而不只是借鉴）**，C.2 里标"高"的三项——
**Specialization、告警 hash/reportdiff、Clang 前端翻译覆盖率**——需要补做，
其中前端覆盖率必须用贵团队的真实代码来测，无法在本次调研的沙箱里回答。

---

## 附录 D：能力边界与误报漏报（v2.2 增补）

C.3 里列的"高灵敏度配置在真实代码上的误报率"，本轮用一套**正交四象限语料**
做了定量测量，完整文档在 **`07-capability-boundary/01-capability-boundary-fp-fn.md`**，
可用 `07-capability-boundary/run-boundary.sh` 一键复现。这里只放对选型最关键的三块。

### D.1 混淆矩阵（文件级，47 个文件）【实测数据】

语料：22 个含真实越界的文件（TP 组）+ 25 个绝对安全的文件（TN/FP 组）。

|  | 默认档 | 高灵敏档（`--bo-debug 4` + L4/L5/U5） |
|---|---|---|
| TP | **18** | **19** |
| FN | **4** | **3** |
| FP | **0** | **13** |
| TN | **25** | **12** |
| 精确率 | **100 %** | **59.4 %** |
| 召回率 | **81.8 %** | **86.4 %** |

**结论：高灵敏档不值得开。** 召回只多抓到 1 例（+4.5 个百分点），
误报从 0 个文件涨到 13 个文件；即便在本来就有真 bug 的文件里，
也从 19 条膨胀到 30 条告警。§14.2 原本说"精度降级可以靠调参换回来"——
这句话需要限定：**能换回来的量很小，代价很大**。

Pulse 对照：同样 22 个真实越界，`--pulse-only` **检出 0 个数组越界**。
这与 §16 的结论一致，并把它从"抽样观察"提升为"22 个正交用例上的完整测量"。

### D.2 漏报的性质分类 —— 机制 vs 参数

这是本附录对选型最有用的一张表。**两类漏报的应对方式完全不同**：

| 漏报场景 | 性质 | 能不能调参解决 | 修复工作量估计 |
|---|---|---|---|
| struct/union 字段数组（局部/静态/全局基址） | **机制** | ❌ 已验证三个相关旋钮全部无效 | **10–30 行**（`decl_local_loc` 的 `Tstruct` 分支递归字段，复用现成的 `init_c_array_fields`） |
| 全局数组（无初始化器） | **机制** | ❌ | 20–50 行（`find_global_array` 回退到声明类型） |
| C 函数指针调用 | **机制** | ❌ | **人月级**（需要指向集合分析） |
| 无调用者的符号条件 | **参数** | ✅ `--bo-debug 4`，但代价见 D.1 | **不要调参**，改用全量分析（附录 B） |
| 超大函数被跳过 | **参数** | ✅ `--bo-max-cfg-size`（默认 15000） | 先量化：`grep -c "Skipped large procedure" infer-out/logs` |

**给 Summary 2.0 的一条硬约束**：影响**精度**的旋钮（等级、`bo-debug`）不该动；
影响**覆盖**的旋钮（`max-cfg-size`、`timeout`）值得调——因为后者减少的是
"根本没看"造成的漏报，**不牺牲精确率**。这两类在设计里必须分开建模。

### D.3 对 §27 架构建议的修正

附录 D 的数据把 §27 的 P0 优先级往前推了一格：

> **原 P0**：SummaryDB + 依赖边失效、AbstractRange（符号/非线性 Bound）
> **修正后的 P0 之前还有一件事**：**把"哪些基址能拿到 extent"做全**——
> 局部数组、**局部/全局 struct 的字段数组**、全局、堆、形参，一个都不能少。

理由：Infer 的区间域与 widening 是成熟的，但它 4 个漏报里有 3 个
（含最常见的 struct 字段数组）都出在"某类基址拿不到 size"，**而不是算法不够强**。
**内存建模的覆盖面比分析算法更决定召回率**——这是本轮最重要的一条经验教训，
也是 §25「反面借鉴」清单应该加上的第三条：

> 反面借鉴 ③：**不要把"某类变量的 extent"绑定在某个特殊过程的摘要上
> （Infer 绑在初始化器过程上），也不要在声明处对聚合类型止步不前
> （Infer 的 `decl_local_loc` 对 struct 直接返回）。
> extent 必须是内存模型的一等属性，在声明处就建立。**

### D.4 一条方法论教训

场景 #6 的更正过程值得记下来：**只测一种写法就下"支持/不支持"的结论是危险的**。
`b->data[11]`（形参）能检出，`s.d[11]`（局部）完全漏报——同一个 bug，
写法一换结论就反转。建议把 `07-capability-boundary/corpus/` 那套 TP/TN/FP 四象限语料
固化进 CI，每次引擎改动跑一遍，任何一格翻转都要有人解释。
这与贵团队测试用例的正交四象限约定是同一套方法论。

---

## 附录 E：方案 C 的落地设计（v2.3 增补）

§25 给的是**架构建议**，本附录指向的是**可以开工的设计**。
完整内容在调研包 `08-summary2-design/`，这里只放三条决策性的摘要。

### E.1 一处架构修正：谁是主要事实源

§25.2 原来建议"CSA 采集路径级事实、FixpointEngine 做外推"。
**这条要改**，理由是一条推理而不是新数据：

如果**主要**事实源是 CSA 的 ExplodedGraph，那么 CSA 每一个放弃探索的地方——
循环块访问次数到顶、路径预算耗尽、inline 深度到顶——都会**没有事实可采**，
于是没有摘要，于是又回到 `unsupported`。**当前最痛的失败模式会被继承。**

修正后的结构：

* **主**：独立抽象解释器，对 `clang::CFG`，非路径敏感。
  **"全函数性"是硬约束**——任何函数都必须产出合法摘要，精度不够就降级为 Top，绝不缺席。
* **副**：CSA 的路径敏感信息，作为**可选的精度增强**。
  AI 判出 L2 之后再去问 CSA "这次访问在什么路径约束下发生"，
  结果作为解释与排序依据附在告警上，**不参与"有没有摘要"的判定**。

代价是显式的：AI 的区间域非析取，拿不到路径相关精度，
`if (f) i=20; if (!f) a[i];` 会 join 成 L2 而现有规则能精确判安全。
**这正是 §25.3 说的"摘要通道精度天然低于 inline 通道"，两条通道并存。**

> ~~**证伪条件**：若实测发现越界 `unsupported` 里循环占比 < 20%（T0-4），本修正的前提不成立，应回到"CSA 主采"。~~
> **v3.6**：不再需要。立项输入的根因"路径爆炸"本身就是"CSA 放弃探索、无事实可采"，主次倒置据此成立；原条件只盯循环，写窄了。

### E.2 ExtentModel 提到一切之前（P-1）

附录 D 的结论是"内存建模的覆盖面比分析算法更决定召回率"。
落到设计上就是：**先做一个只依赖 `ASTContext` + 声明类型 + 分配点表达式、
不依赖 `ProgramState` 的 `getExtent(AccessPath)`**，
把 21 种形态（局部/静态/全局 struct 字段数组、嵌套、struct 数组元素、union、
全局无初始化器、堆、自定义分配器、VLA、柔性数组成员、指向数组中间的指针再下标…）一次做全。

**一个对我们有利的事实**：CSA 的 RegionStore **天生**把结构体字段建模为
`FieldRegion` 并带完整类型——**这正是 Infer 缺的那一块**（附录 D §3.1）。
也就是说在最决定召回率的这个维度上，**我们的起点比 Infer 高**。
这是方案 C 成立的一条硬理由，但它依赖假设 A1（`getStaticSize` 对 `FieldRegion` 可用），
**必须先验证**（T0-1）。

> **v3.4 T0-1 进展**【实测数据】：fork 上对象层 8/8 正确，且与上游 LLVM 14.0.6 逐行一致；
> 字段层在 fork 上也已实测成立（T0-1b：7 种字段数组形态全部 `FieldRegion` / 32 字节 / 8 个元素，19 行输出与上游逐行一致）—— **A1 成立**。
> 同时发现 CSA 14 借不上的三处（fork 上同样复现）：多维只到整数组级、指向数组中间的指针 + 符号下标连地址都没有、
> 柔性数组 `[1]` 默认按 1 个元素算。详见 `08-summary2-design/03-assumptions-and-test-plan.md` T0-1 / T0-1b。

### E.3 工作量与决策点

| 里程碑 | 内容 | 人日 |
|---|---|---|
| M0 PoC「一条链走通」 | 最小可用：3 层调用链跨文件报出 L1 | 15–22 |
| M1 循环 | 阈值 widening + narrowing，**任何循环不产生 unsupported** | +18–26 |
| M2 覆盖面 | ExtentModel 21 种形态 + GlobalEffect + 模型表 | +23–30 |
| M3 分级与精确率 | S2/StillOpen、告警指纹、与现有规则去重 | +15–22 |
| M4 增量与规模 | 依赖边失效、并行、性能 | +18–28 |
| M5 生产化 | CI、报表、门禁 | +10–21 |
| **合计** | | **99–149 人日（约 5–7.5 人月）**（v3.4 按 T0-1 修正，原 98–151） |

**M1 结束（33–48 人日）是第一个技术检查点**：多层与循环两件最硬的事都跑通，用来校准后续工期。

> **v3.6 更新：已立项，进入实施阶段。**【立项输入】目标确定为支持现有工具因**路径爆炸**而不支持的
> 跨多层调用越界场景（立项统计 **2–17 层**）；1–2 层现有工具已支持，摘要通道只报更深的
> （CSA 已知盲区例外）。原来这里的"更省的备选"（只加深 inline 到 4–5 层，15–20 人日）**不采用**：
> 它够不到 17 层，而且 inline 加深本身就在放大路径爆炸。T0-2 / T0-3 / T0-4 不再作为前置条件。
>
> **摘要方式受不受层数限制**：成本不受——每个函数只分析一次，层间是加法（Infer 200 层 0.76 秒）【实测数据】；
> 精度受"链上每一层做了什么"影响，不受"第几层"影响——线性传递无损，分支合并与循环会让区间变宽（L1 可能降为 L2），
> 调用没建模的外部函数、函数指针、递归会让条件断链。详见 `08-summary2-design/01-architecture.md` §0.1。
