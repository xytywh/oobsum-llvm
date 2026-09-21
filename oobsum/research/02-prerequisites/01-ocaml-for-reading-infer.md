# 读懂 Infer 源码需要的 OCaml（只讲必需的）

> 读者假设：会 C/C++，读过 CSA 源码，没写过 OCaml。
> 目标：能看懂报告里引用的那些源码片段，能读 `.mli` 判断一个模块在干什么。
> **不是 OCaml 教程**，只列真正卡住阅读的那几件事。

---

## 1. 先解决你最困惑的那个：`Location.t` 里的 `.t` 是什么

**`t` 就是一个普通的类型名，没有任何魔法。** OCaml 的惯例是：
**一个模块里"最主要的那个类型"一律叫 `t`**。

```ocaml
module Location = struct
  type t = {line: int; col: int; file: SourceFile.t; macro_file_opt: ...; macro_line: int}
end
```

于是在模块外面就写成 `Location.t`。对照 C++ 你就懂了：

| OCaml | C++ 里的等价物 |
|---|---|
| `module Location` + 里面的 `type t` | `namespace Location { struct Location {...}; }` |
| `Location.t` | `Location::Location`（但 C++ 一般直接叫 `Location`） |
| `Location.pp` | `Location::print` |
| `Location.compare a b` | `Location::compare(a, b)` |

**为什么不叫 `Location.location`？** 因为那样会写成 `Location.location`，重复。
OCaml 社区统一用 `t`，于是所有模块长得一样：`Procname.t`、`Typ.t`、`Sil.instr`（这个例外，
因为 `Sil` 模块里有多个同等重要的类型，所以按语义命名）、`Itv.t`、`ArrayBlk.t`……

**看到 `Xxx.t` 就读成"Xxx 这个东西"**，仅此而已。

同理：

* `Procname.Set.t` = "过程名的集合"这个类型（`Set` 是 `Procname` 里的子模块）
* `Procname.Map.t` = "以过程名为键的映射"
* `'a option` = 可能没有的 `'a`（C++ 的 `std::optional<T>`）
* `'a list` = `'a` 的链表（C++ 的 `std::list<T>`，但不可变）

---

## 2. `.ml` 和 `.mli` —— **这是读 Infer 源码效率最高的一招**

* `Foo.ml` = 实现（相当于 `.cpp`）
* `Foo.mli` = **接口**（相当于 `.h`，但强制性更强：`.mli` 里没写的东西，外部根本访问不到）

**几乎所有情况下，你只需要读 `.mli`。** Infer 的 `.mli` 文档注释质量很高，
本报告里大量结论就是直接引用 `.mli` 注释得到的。例如：

```ocaml
(* infer/src/backend/AnalysisDependencyGraph.mli *)
val invalidate : changed_files:SourceFile.Set.t option -> unit
(** Build a dependency graph from the summaries in the specs table, flag any transitive
    dependencies of the [changed_files], then invalidate any flagged transitive dependencies. *)
```

这一段就把增量分析的机制说清楚了，根本不用打开 `.ml`。

**读法建议**：想搞清楚一个子系统 → `ls infer/src/<dir>/*.mli` → 挨个看注释 → 只有需要确认
具体算法时才进 `.ml`。

---

## 3. 函数声明：`val` 那一行怎么读

```ocaml
val get : AnalysisRequest.t -> Procname.t -> t option
```

* `val name : 类型` 声明一个值/函数。
* `->` 分隔参数与返回值，**最后一个是返回值**，前面全是参数。
* 上面这行读作：`get` 接受一个 `AnalysisRequest.t`、一个 `Procname.t`，返回 `t option`
  （可能有也可能没有的摘要）。
* C++ 写法：`std::optional<Summary> get(AnalysisRequest req, Procname pname);`

**为什么参数之间也用 `->`？** 因为 OCaml 里函数默认柯里化（可以只传一部分参数得到新函数）。
读源码时**不用管这件事**，就当成"参数列表"读。

### 带名字的参数（`~`）和可选参数（`?`）

```ocaml
val widen : prev:t -> next:t -> num_iters:int -> t
val analyze_proc_name : AnalysisRequest.t -> ?specialization:Specialization.t
                        -> caller_summary:Summary.t -> Procname.t -> _ AnalysisResult.t
```

* `~name:` = **具名参数**，调用时必须写 `widen ~prev:a ~next:b ~num_iters:3`。
  在 `.mli` 里写成 `prev:t`，调用处写成 `~prev:`。
* `?name:` = **可选参数**，可以不传。

这两个在 Infer 里极其常见，见到 `~` 就理解成"这个参数有名字"。

---

## 4. 代数数据类型（ADT）—— 读懂 `Sil.instr` 的关键

```ocaml
type instr =
  | Load of {id: Ident.t; e: Exp.t; typ: Typ.t; loc: Location.t}
  | Store of {e1: Exp.t; typ: Typ.t; e2: Exp.t; loc: Location.t}
  | Prune of Exp.t * Location.t * bool * if_kind
  | Call of (Ident.t * Typ.t) * Exp.t * (Exp.t * Typ.t) list * Location.t * CallFlags.t
  | Metadata of instr_metadata
```

这是"**一个值只能是这五种形态之一**"，相当于 C++ 的 `std::variant` / 带 tag 的 union，
或者 LLVM 那种 `Instruction` 基类 + 各种子类，但**编译器强制你处理所有情况**。

* `| Load of {…}` = 这一支叫 `Load`，携带一个有名字段的记录（像 struct）。
* `| Prune of Exp.t * Location.t * bool * if_kind` = 这一支携带一个 4 元组（**`*` 是元组，不是乘法**）。

**用法是模式匹配**：

```ocaml
match instr with
| Load {id; e= Lindex (base, idx); loc} ->      (* 同时匹配"是 Load" 且 "e 是 Lindex" *)
    (* 这里 id / base / idx / loc 都已经解构出来了，直接用 *)
| Call ((ret_id, _), Const (Cfun callee), actuals, loc, _) ->
    (* 只有直接调用（函数名是常量）才走这支 *)
| _ ->
    astate                                      (* 其余情况 *)
```

对照 CSA：你在 CSA 里要写 `if (const auto *AS = dyn_cast<ArraySubscriptExpr>(S))` 层层 `dyn_cast`，
OCaml 一行模式匹配就把嵌套结构拆开了。这也是为什么 Infer 的 Checker 代码比 CSA Checker 短。

`_` 表示"这个位置我不关心"。

---

## 5. `option` 和 `result` —— 跨过程 API 全靠这两个

```ocaml
type 'a option = None | Some of 'a
type ('a, 'b) result = Ok of 'a | Error of 'b
```

Infer 里最重要的一处：

```ocaml
analyze_dependency : Procname.t -> 'payload AnalysisResult.t
(* AnalysisResult.t 本质就是 result *)

match analyze_dependency callee with
| Ok callee_summary -> (* 拿到摘要了，用它 *)
| Error _           -> (* 没有摘要：库函数 / 未定义 / 递归环 *)
```

**读 Infer 的跨过程代码，看懂这一个 match 就看懂了 80%。**

---

## 6. 模块与 Functor —— 读框架代码必须过的一关

### 模块

```ocaml
module Val = struct
  type t = {itv: Itv.t; powloc: PowLoc.t; arrayblk: ArrayBlk.t; ...}
  let pp fmt x = ...
end
```

就是个命名空间 + 里面的类型和函数。

### Functor（"用模块生成模块"）

```ocaml
module Analyzer = AbstractInterpreter.MakeRPO (TransferFunctions (CFG))
```

读法：**`MakeRPO` 是一个"以模块为参数的函数"**，你把自己的 `TransferFunctions` 模块喂进去，
它还给你一个完整的分析器模块 `Analyzer`（里面有 `compute_post`）。

C++ 的类比是模板：`using Analyzer = AbstractInterpreter<TransferFunctions<CFG>>;`

Infer 框架里到处是这个：

| 写法 | 意思 |
|---|---|
| `AbstractInterpreter.MakeRPO(TF)` | 用逆后序遍历跑 TF 的分析器 |
| `TransferFunctions (CFG)` | 把 TF 参数化到某种 CFG（`ProcCfg.Normal` / `ProcCfg.Exceptional`） |
| `PrettyPrintable.MakePPMap (Var)` | 生成"以 Var 为键、能打印"的 Map |
| `AbstractDomain.Pair (A) (B)` | 把两个域组合成积域 |

**不懂 functor 就读不懂调度框架**，但也只需要理解到"它是个模板"这个程度。

---

## 7. `[@@deriving …]` —— 看到就跳过，但要知道它干了什么

```ocaml
type t = {base: Base.t; idx: Idx.t; loc: Location.t} [@@deriving compare, equal]
```

这是 **PPX 预处理器**，在编译期自动生成样板函数。上面这行等价于自动写出了
`compare : t -> t -> int` 和 `equal : t -> t -> bool`。

常见的几个：

| deriving | 自动生成了什么 | 你会在哪见到 |
|---|---|---|
| `compare` / `equal` / `hash` | 比较/相等/哈希函数 | 几乎所有类型 |
| `yojson_of` | 转 JSON | `Summary.t`、`Payloads.t` |
| `fields` | 字段访问器 `Fields.xxx` | `Payloads.t` → `Payloads.Fields.mini_oob` |
| `variants` | 变体元信息 | `PayloadId.t` → `Variants.descriptions` → **specs 表的列名就是这么来的** |
| `enumerate` | `all` 列表（所有变体） | `Checker.t` |

**重要**：如果你在源码里 grep 一个函数名 grep 不到，先想想是不是 deriving 生成的。

---

## 8. 几个高频写法速查

| 写法 | 意思 |
|---|---|
| `x \|> f \|> g` | 管道：`g (f x)`。Infer 里极常见，**从左往右读** |
| `let x = e in body` | 局部绑定（`let` 后面跟 `in` 才是局部的） |
| `let f a b = ...` | 定义函数 f，两个参数 |
| `fun x -> ...` | 匿名函数（lambda） |
| `a.field` / `{r with field= v}` | 读记录字段 / 复制记录并改一个字段（**不可变**，返回新值） |
| `ref` / `!x` / `x := v` | 可变引用：`!` 是解引用（**不是"非"**），`:=` 是赋值 |
| `mutable field` | 记录里的可变字段（`Summary.t` 的 `dependencies` 就是） |
| `;` vs `;;` | `;` 顺序执行；`;;` 顶层分隔（源码里少见） |
| `(* 注释 *)` | 注释 |
| `(** 文档注释 *)` | 文档注释（`.mli` 里的这种最值得读） |
| `[%blob "./x.md"]` | 编译期把文件内容嵌进来（`Checker.ml` 的文档用了这个） |
| `L.die InternalError "..."` | 抛异常终止（Infer 自己的 Logging 模块） |
| `L.d_printfln` / `L.progress` | 写调试日志 / 打到终端 |

---

## 9. 不需要掌握的

GADT、多态变体（`` `Foo ``，Infer 里有但只用作简单 tag）、Effect handlers、
lazy 的细节（知道 `SafeLazy.t` 是"用到才反序列化"就够了）、
first-class modules、object system。**本报告涉及的核心分析模块基本没用到这些。**

---

## 10. 一个练手路径（半天）

1. 读 `infer/src/IR/Sil.mli` 的 `type instr` —— 对照上面第 4 节。
2. 读 `infer/src/absint/InterproceduralAnalysis.mli` 全文（只有 40 行）——
   看懂 `analyze_dependency` 那个字段的类型。
3. 读 `infer/src/labs/04_interprocedural/ResourceLeaks.ml` 全文（~110 行）——
   这是官方教学 Checker，结构和我们的 MiniOobChecker 完全一致。
4. 读本包 `03-MiniOobChecker/src/MiniOobChecker.ml` —— 带中文思路注释的同构实现。

读完这四个，你就能看懂报告里所有源码引用了。
