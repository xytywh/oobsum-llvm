# 术语对照表：CSA ↔ Infer

读报告时对不上号，回来查这张表。左边是你熟悉的说法，右边是 Infer 的说法与真实源码位置。

---

## 1. 程序表示

| 你熟悉的（CSA / Clang） | Infer 的对应物 | 源码位置 | 差别提示 |
|---|---|---|---|
| Clang AST（几百种 `Stmt` 子类） | **Textual / SIL**，只有 5 条指令 | `IR/Sil.mli` | 降级复杂度被推给前端（`clang/` 84 个文件） |
| `CFG` / `CFGBlock` / `CFGElement` | `Procdesc.t` / `Procdesc.Node.t` | `IR/Procdesc.mli` | — |
| 一个 TU 的 CFG 集合 | `Cfg.t = Procdesc.t Procname.Hash.t` | `IR/Cfg.mli:14` | **"文件的 CFG"只是过程的哈希表**，没有全局 CFG 对象 |
| `Expr` | `Exp.t`（`Var` / `Lvar` / `Lfield` / `Lindex` / `BinOp` / `Const` / `Cast` …） | `IR/Exp.mli` | — |
| `QualType` / `Type` | `Typ.t` | `IR/Typ.mli` | 数组是 `Tarray {elt; length; stride}` |
| `ASTContext` 的类型表 | `Tenv.t`（类型环境，按文件存） | `IR/Tenv.mli` | **增量失效会算上它**（`used_tenv_sources`） |
| `FunctionDecl` 的 mangled name | `Procname.t`，唯一 id 是 `Procname.to_unique_id` | `IR/Procname.mli` | 就是 `specs.proc_uid` |
| `a[i]` 在 AST 里是 `ArraySubscriptExpr` | `Load {e = Lindex (base, idx)}` | — | 读写统一成 `Load`/`Store` |
| 局部变量声明 | `Metadata (VariableLifetimeBegins {pvar; typ})` | `IR/Sil.mli` | 数组长度要从 `Procdesc.get_locals` 的 `Typ.Tarray` 拿 |

**看 IR 最快的办法**：`infer capture --store-textual …` 然后 `infer debug --export-textual out/`，
得到 `.sil` 文本文件。见 `06-practice/02-observability-handbook.md` §1.3。

---

## 2. 分析状态

| CSA | Infer | 关键差别 |
|---|---|---|
| `ProgramState` | `BufferOverrunDomain.Mem.t` / `PulseAbductiveDomain.t` | **CSA 的是路径绑定的；Infer 的是 join 之后的（BO）或析取列表（Pulse）** |
| `SVal` | `BufferOverrunDomain.Val.t = (itv, powloc, arrayblk, …)` | 打印成三元组 `(区间, 指向位置集合, 数组块)` |
| `MemRegion` | `AbsLoc.Loc.t`；一组位置是 `PowLoc.t` | **别名用集合表达，不用路径分裂** |
| `SymbolicRegion` | `Allocsite.t`（分配点） | 每个数组声明 / `malloc` 一个 |
| `ConstraintManager` 里的 range | `Itv.t`（一对 `Bound.t`） | `Bound` 可带**符号**与**非线性项** `mk_MultB` |
| 路径约束 | BO：`LatestPrune`（只用于可达性）<br>Pulse：`PathCondition / Formula` | **BO 不用路径约束去收窄下标区间**（报告 §14 case 3 漏报的原因） |
| `ExplodedGraph` 的一条路径 | Pulse 的一条析取分支（`DisjDomain.t`） | Pulse 默认最多 20 条，超了**主动丢** |
| `ExplodedNode` | `Procdesc.Node.t` + 该节点上的抽象状态 | `--debug` 时每个节点一个 HTML |

---

## 3. 分析框架

| CSA | Infer | 源码位置 |
|---|---|---|
| `ExprEngine`（符号执行引擎） | `AbstractInterpreter`（抽象解释引擎） | `absint/AbstractInterpreter.ml` |
| Checker 回调 `checkPreCall` / `checkBind` / `checkLocation` | **没有对应物**。你要自己写 `exec_instr`，对 5 条指令做模式匹配 | `absint/TransferFunctions.mli` |
| `CheckerContext` | `InterproceduralAnalysis.t`（含 `proc_desc` / `tenv` / `err_log` / **`analyze_dependency`**） | `absint/InterproceduralAnalysis.mli` |
| `CheckerManager` / `Checkers.td` 注册 | `base/Checker.ml` + `backend/registerCheckers.ml` | — |
| `BugReporter::EmitBasicReport` | `Reporting.log_issue proc_desc err_log ~loc <Checker> <IssueType> msg` | `absint/Reporting.mli` |
| `BugType` | `IssueType.t`（`base/IssueType.ml` 里 `register`） | — |
| Inline 深度限制 | `Config.ondemand_callchain_limit`（**默认不限制**） | `base/Config.ml:2225` |
| 循环展开次数限制 | `Config.pulse_widen_threshold`（默认 3）+ widening | `base/Config.ml:3021` |
| —（CSA 没有） | `Config.pulse_max_disjuncts`（默认 20，超了丢状态） | `base/Config.ml:2498` |
| —（CSA 没有） | **摘要**：`Summary.t` / `Payloads.t`，落 SQLite | `backend/Summary.mli` |
| —（CSA 没有） | **按需跨过程调度**：`Ondemand.analyze_proc_name` | `backend/ondemand.ml:592` |

---

## 4. 跨过程

| 概念 | CSA | Infer |
|---|---|---|
| 跨过程方式 | Inline callee 的 CFG 到 caller 的 ExplodedGraph | 读 callee 的**摘要**，做代数代换 |
| 触发时机 | 走到 `CallExpr` 且深度未超限 | 走到 `Sil.Call` 且 `e_fun` 是 `Const (Cfun _)` |
| 拿到的东西 | callee 的完整函数体 | `analyze_dependency callee : payload AnalysisResult.t` |
| 分析方向 | 自顶向下 | 自底向上（按需触发，等价于深度优先展开调用图） |
| callee 被分析几次 | 每个调用点一次 | **一次**（`ondemand_procs_analyzed` 可验证） |
| 递归 | 深度截断 | `ActiveProcedures` 环检测 + 有限次重启（`ondemand_recursion_restart_limit=100`） |
| 间接调用 | 依赖具体实现 | C 函数指针被降级成内建 `__call_c_function_ptr`，**BufferOverrun 不解析**（报告 §14.4） |

---

## 5. 摘要相关（你们方案最关心的一块）

| 你们方案里的说法 | Infer 的对应物 | 源码 / 输出 |
|---|---|---|
| Summary（一份） | `Summary.t`，内含 `Payloads.t`（**17 个独立 payload**） | `backend/Summary.mli` / `Payloads.mli` |
| Summary Schema | **不存在统一 schema**，每个分析一个类型、一列 | `base/PayloadId.ml`（17 变体 → specs 表 17 列） |
| PreCondition / requires | `ArrayAccessCondition {offset; idx; size; …}`，成组叫 `ConditionSet` | `bufferoverrun/bufferOverrunProofObligations.ml:185` |
| Summary Composition / 实例化 | `ConditionSet.subst`（形参→实参代换） | 同上，`subst` |
| "这条条件还判不了，留给上层" | `SymbolicIssue` + `propagate = true`，经 `for_summary` 进摘要 | 同上，`check` / `for_summary` |
| PostCondition | BO：`Mem`（抽象内存）；Pulse：`post` | 输出里的 `BufferOverrunAnalysis:` 段 |
| MemoryEffect | `ArrayBlk`（Allocsite → offset/size/stride） | `bufferoverrun/arrayBlk.mli` |
| AliasEffect | BO：`PowLoc` 集合；Pulse：地址图 | — |
| SummaryDB | SQLite `infer-out/results.db` 的 `specs` 表 | `base/Database.ml` |
| SummaryManager / 调度 | `ondemand.ml` + `Summary.OnDisk.Cache`（两层 LRU） | `backend/ondemand.ml` / `Summary.ml:160` |
| 增量失效 | `Dependencies.record_pname_dep` + `AnalysisDependencyGraph.invalidate` | `IR/Dependencies.mli` / `backend/AnalysisDependencyGraph.mli` |
| 摘要序列化 | OCaml `Marshal`（魔数 `0x8495A6BE`） | 实测 |

---

## 6. 运行与产物

| 你想做的事 | CSA / CodeChecker 里 | Infer 里 |
|---|---|---|
| 分析整个工程 | `CodeChecker analyze compile_commands.json` | `infer capture --compilation-database compile_commands.json` + `infer analyze` |
| 看单文件 IR | `clang -cc1 -ast-dump` | `infer capture --store-textual` + `infer debug --export-textual` |
| 看 CFG 图 | `clang -cc1 -analyze -analyzer-checker=debug.ViewCFG` | `infer debug --procedures --procedures-cfg --select all` → `.dot` |
| 看分析中间状态 | `-analyzer-viz-egraph-graphviz` | `infer run --debug` → `infer-out/captured/*/nodes/*.html` |
| **看摘要** | （CSA 没有） | **`infer debug --procedures --procedures-summary --procedures-summary-skip-empty --select all`** |
| 报告 | `report-*.plist` / CodeChecker DB | `infer-out/report.json` / `report.txt` |
| 告警指纹（去重） | CodeChecker 的 issue hash | `report.json` 的 `hash` 字段 |
| 版本间 diff | CodeChecker `cmd diff` | `infer reportdiff --report-previous … --report-current …` |
| 并行 | `CodeChecker analyze -j N` | `infer analyze --jobs N --scheduler callgraph` |
| 增量 | CodeChecker 的 skip / 文件级 | `infer analyze --incremental-analysis --changed-files-index …` |

---

## 7. 容易混淆的几个词

| 词 | 在 Infer 里的确切含义 |
|---|---|
| **capture** | 前端翻译阶段（Clang AST → SIL），产物进 `capture.db`。**不是"运行"** |
| **analyze** | 分析阶段，产物进 `results.db`。**这一步才是全量跨文件的** |
| **payload** | 一种分析产出的摘要类型（17 种之一），不是"负载" |
| **spec** | `specs` 表的一行，即一个过程的全部摘要 |
| **procedure** | 函数（Infer 统一叫过程，含 C 函数、C++ 方法、合成的全局初始化器） |
| **ondemand** | 按需分析调度器，不是"可选" |
| **specialization** | 摘要特化（针对特定调用上下文重算一份更精确的摘要），与 CSA 的上下文敏感对应 |
| **prune** | 分支裁剪（`Sil.Prune`），对应 CSA 的 `assume` |
| **L1/L2/L3/L4/L5** | BufferOverrun 的**置信度分级**，L1 最确定。**L4/L5/U5 默认关闭** |
| **模型（model）** | 对库函数手写的抽象效果（如 `malloc`），不是"机器学习模型" |
