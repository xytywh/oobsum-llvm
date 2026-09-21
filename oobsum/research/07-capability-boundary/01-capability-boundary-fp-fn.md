# Infer 能力边界与误报漏报全表（C/C++ 数组越界）

> **这份文档要回答的是选型问题，不是使用问题**：Infer 在 C/C++ 数组越界这条线上
> 到底能看住什么、看不住什么；每一处看不住是**底层机制决定的**（短期解决不了，
> 但可以估工作量）还是**运行参数决定的**（可以调，但有代价）。
>
> 基线：`infer v1.3.0` 官方预编译（bundled clang 21.1.6），commit
> `3d38229b04b649024fee065621f6a4c838f6d029`。全部数字可用同目录
> `./run-boundary.sh` 复现，语料在 `corpus/`。

---

## 0. 三句话结论

1. **默认配置是一把"只说有把握的话"的刀**：47 个文件的正交语料上
   **召回 18/22（82%），误报 0/25（0%）**。这个精确率在工业静态分析里是极好的。
2. **高灵敏度档位不值得开**：召回只从 18/22 涨到 19/22（+1 例），
   误报从 0 个文件涨到 **13/25 个文件**。**换 1 个真阳性，赔 13 个文件的噪声。**
3. **漏报的四类里，三类是机制性的，且其中两类此前被我自己的报告写漏了**——
   尤其是**局部/全局结构体的字段数组完全不生成任何安全条件**，
   而这是 C 代码里最常见的形态之一。详见 §3.1。

---

## 1. 实验设计

四组正交语料，每个 `.c` 单独 `infer run`，统计口径是 `report.json` 的条目数。

| 组 | 文件数 | 语义 | 报了 = | 不报 = |
|---|---|---|---|---|
| `corpus/tp/` | 22 | 每个都含**真实越界** | TP | **FN** |
| `corpus/tn/` | 15 | 每个都**绝对安全**（显式边界检查、精确循环、sizeof 等） | **FP** | TN |
| `corpus/fp/` | 10 | 安全，但**依赖 Infer 不一定会做的推理**（模运算、位掩码、路径相关、递归…） | **FP** | TN |
| `corpus/probe/` | 13 | 结构体字段数组的**专项探针**，用于定位 §3.1 那个缺口 | — | — |

两档配置：

```bash
# 默认档
infer run --bufferoverrun-only -- clang -c x.c

# 高灵敏档（打开所有被默认关掉的等级 + 绕过"符号条件只传不报"）
infer run --bufferoverrun-only \
  --bo-debug 4 \
  --enable-issue-type BUFFER_OVERRUN_L4 \
  --enable-issue-type BUFFER_OVERRUN_L5 \
  --enable-issue-type BUFFER_OVERRUN_U5 \
  --enable-issue-type INTEGER_OVERFLOW_L5 \
  --enable-issue-type INTEGER_OVERFLOW_U5 \
  -- clang -c x.c
```

---

## 2. 混淆矩阵（文件级，47 个文件）

【实测数据】

|  | 默认档 | 高灵敏档 |
|---|---|---|
| TP（真越界被报） | **18** | **19** |
| FN（真越界漏报） | **4** | **3** |
| FP（安全代码被报） | **0** | **13** |
| TN（安全代码正确沉默） | **25** | **12** |
| **精确率** Precision | **100 %** | **59.4 %** |
| **召回率** Recall | **81.8 %** | **86.4 %** |
| F1 | 0.90 | 0.70 |

> 单看这张表就能定调：**默认档是可以直接进 CI 拦门禁的质量；高灵敏档只适合
> 人工专项排查，绝不能进门禁。**

Pulse 作为对照（官方说 BufferOverrun 已废弃、"Use Pulse instead"）：

| | 22 个真实越界 | 15 个安全用例 |
|---|---|---|
| `--pulse-only` | **检出 0 个数组越界** | 1 条 `PULSE_UNINITIALIZED_VALUE` |

那 1 条不是误报——`t15_malloc_ok` 确实读了未初始化的堆内存，那是另一类真 bug。
但**数组越界这条线上 Pulse 是 0**。这一条对选型的分量，比本文其他所有内容加起来都重。

---

## 3. 漏报（FN）逐条归因

四个 FN，外加探针组暴露出的一大类。**三类机制性，一类参数性。**

### 3.1 【机制性·影响最大】局部 / 静态 / 全局结构体的字段数组：完全不生成条件

**这是本轮新发现，并且纠正了本包报告 §13 场景 #6 的结论。**
原来的场景 #6 只测了"结构体指针形参 `struct Buf *b; b->data[11]`"这一种写法，
它能检出，于是我写成了"结构体字段数组：支持"。**这个概括是错的。**

【实测数据】探针组 `corpus/probe/` 的完整结果：

| 探针 | 写法 | 默认档 | 高灵敏档 |
|---|---|---|---|
| `v6_local_arr_const` | `int a[10]; a[12]` | ✅ L1 | ✅ |
| `v7_struct_param_val` | `struct S` 按值做**形参**，`s.d[i]` | ✅ L1 | ✅ |
| `v12_malloc_struct` | `malloc(sizeof(struct S))` 后 `p->d[11]` | ✅ L1 | ✅ |
| `v11_global_arr_init_memcpy` | 全局 `char g[8]={0}`，`memcpy(g,…,64)` | ✅ L1 | ✅ |
| `v1_local_struct_const` | **局部** `struct S s; s.d[11]` | ❌ | ❌ |
| `v2_local_struct_var` | 同上，下标是变量 | ❌ | ❌ |
| `v13_static_local_struct` | `static struct S s; s.d[11]` | ❌ | ❌ |
| `v3_global_struct_init` | **全局**且**有初始化器** `gs.d[11]` | ❌ | ❌ |
| `v10_same_fn_ptr` | `struct S s; struct S *p=&s; p->d[11]` | ❌ | ❌ |
| `v4_local_struct_addr` | `int *p = s.d; p[11]` | ❌ | ❌ |
| `v5_nested_struct` | 嵌套 `o.in.d[11]` | ❌ | ❌ |
| `v8_arr_of_struct` | `struct S arr[4]; arr[1].d[11]` | ❌ | ❌ |
| `v9_memcpy_field` | `memcpy(s.d, src, 64)`，`s` 是局部 struct | ❌ | ❌ |

注意 `v10`：把 `.` 换成 `->` 也没用。**区别不在访问语法，在基址是不是形参或堆分配。**

**根因【源码确认】** `bufferOverrunUtils.ml:56-70` 的 `decl_local_loc`：

```ocaml
let rec decl_local_loc ({tenv} as model_env) loc typ ~inst_num ... mem =
  match typ.Typ.desc with
  | Typ.Tarray {elt= typ; length; stride} ->
      decl_local_array model_env loc typ ~length ...        (* ← int a[10] 走这里，建 allocsite *)
  | Typ.Tstruct typname -> (
    match TypModels.dispatch tenv typname with
    | Some (CArray {element_typ; length}) -> decl_local_array ...
    | Some CppStdVector | Some JavaCollection | Some JavaInteger | None ->
        (mem, inst_num) )                                    (* ← 原样返回，什么都不做 *)
  | _ -> (mem, inst_num)
```

局部变量如果是 struct 类型，除非它命中 `TypModels` 里硬编码的几个模型
（`CArray` / `CppStdVector` / …），`decl_local_loc` **直接原样返回 mem，
不递归进字段**。于是 `s.d` 永远拿不到带 size 的 `ArrayBlk`。

验证这一步走到了尽头【实测数据】——`v1` 的摘要是空的：

```
BufferOverrunAnalysis: MemPure: { return -> (_|_, _|_, { }) }     ← 抽象内存里根本没有 s
BufferOverrunChecker: Safety conditions: {  }                     ← 一条条件都没生成
```

**不是阈值问题，不是报告等级问题，是条件压根没产生。** 所以调任何参数都没用，
已逐一验证 `--bo-field-depth-limit 20`、`--bo-sound-unknown-sets-join`、
`--bo-debug 4` 三个最相关的旋钮，**全部仍为 0 条**。

**为什么形参和 malloc 反而可以？** 形参走的是符号路径实例化（按声明类型生成符号
数组块），malloc 走的是 `bufferOverrunModels.ml:169` → `init_c_array_fields`——
后者**已经把"遍历 struct 字段、给数组字段建 allocsite"这段逻辑完整写好了**
（`bufferOverrunUtils.ml:120-160`），只是没有接到 `decl_local` 上。

**如果要修**：把 `decl_local_loc` 的 `Tstruct` 分支从"什么都不做"改成
`Tenv.lookup tenv typname` 后逐字段递归，复用 `init_c_array_fields` 的字段遍历逻辑。
**估计 10–30 行 OCaml**，是本文列出的所有缺口里**性价比最高的一个修复**。
风险：局部 struct 一旦有了数组字段，条件数会上升，需要重测误报。

> **对我们选型的意义**：C 业务代码里 `struct Foo cfg; cfg.buf[i]` 这种写法极其常见。
> 如果 Infer 在你们代码上"跑出来告警很少"，第一嫌疑就是这个缺口，而不是"代码写得好"。
> 上真实代码库时**必须**先用 `corpus/probe/v1_local_struct_const.c` 这类探针
> 确认基线能力，再解读全量结果。

### 3.2 【机制性】全局数组（无初始化器）

【实测数据】`p12_global_noinit`：`int g[10];`（试探性定义）+ `g[100]` → 两档都漏报。
对照 `p11_global_init`：`int g[10] = {…};` + `g[100]` → ✅ L1。

**根因【源码确认】** `bufferOverrunDomain.ml:2169-2190` 的 `find_global_array`：
它拿到全局数组 `size` 的**唯一通道**是"该全局的初始化器过程
`__infer_globals_initializer_*` 的摘要"。C 的试探性定义没有初始化器 ⇒
`arrayblk` 为 ⊥ ⇒ 连条件都不生成。

**如果要修**：全局变量的 size 本来就在 Tenv / `Procdesc` 的全局表里，
不该依赖初始化器过程。改法是在 `find_global_array` 找不到初始化器摘要时
回退到"按声明类型取 size"。**估计 20–50 行**，但要处理 `extern` 声明
（另一个 TU 定义）的情况。

> 这一项**弱于你们现有的 CSA**，报告 §25 已经把它列为"反面借鉴"：
> **Summary 2.0 里全局变量必须是一等公民，不能挂在初始化器上。**

### 3.3 【机制性】C 函数指针调用

【实测数据】`p13_fnptr`：默认漏报；高灵敏档只报一条 `BUFFER_OVERRUN_L4`
（符号噪声，不是真正解析了调用目标）。

**根因【实测数据】** 前端把 C 函数指针调用降级成对**内建函数**的调用：

```
n$1 = _fun___call_c_function_ptr(n$0:_fn_, &a:int*, 22:int)     ← 不是 Call + Cfun
抽象状态: fp -> (1, { Unknown }, { Unknown -> T })
```

BufferOverrun 未为 `__call_c_function_ptr` 建模，整个调用退化成"未知调用"，
**根本不会去读 `get` 的摘要**。

**如果要修**：需要"函数指针指向集合"分析（哪怕只做同 TU 内的常量传播），
再在调用点对候选集合逐一实例化摘要。这是**独立的一个分析**，
**估计人月级**，不是几十行能解决的。

> 你们的 CSA 侧同样不支持。这一条在 Summary 2.0 里值得单独立项：
> 先做最保守的版本——只处理"函数指针被同一函数内的常量赋值"，
> 就能覆盖相当一部分实际代码。

### 3.4 【参数性】符号条件"只传不报"

先说清楚口径：**TP 语料的 4 个 FN 全部由 §3.1–§3.3 三个机制性缺口造成**
（`p05_struct` 和 `p20_union` → §3.1，`p12_global_noinit` → §3.2，`p13_fnptr` → §3.3），
本节这一类**不在那 4 个里**——它是另一种形态的"看不见"：
**函数的安全性依赖调用者，而本 TU 内没有调用者**，
`corpus/fp/f05_leaf_nocaller.c` 就是这个样子。之所以要单列，是因为
它在真实工程里比前三类更常见，而且**归因完全不同：它是参数性的，也是可以靠"全量分析"消除的**。

默认档下这类条件被分类为 **SymbolicIssue**，`propagate=true` 但**不报告**——
等待调用者代换。TU 内没有调用者 ⇒ 永远等不到 ⇒ 表现为漏报。

**这是参数性的**：`--bo-debug 4` 会绕过 SymbolicIssue 分支直接报出来。
代价见 §4——这正是高灵敏档 13 个 FP 文件的主要来源。

**权衡建议**：不要全局打开。正确做法是**全量分析**（`compile_commands.json`），
让真实调用者出现在同一次 analyze 里，条件自然被代换判定。
本包 `06-practice/01-whole-program-analysis.md` 的三文件 demo
就是这个机制的最小证明：逐文件分析 0 条，全量分析报出 L1。

### 3.5 【参数性·容易被忽略】超大函数被整个跳过

【实测数据】`--bo-max-cfg-size` 默认 **15000**。超过就**整个过程不分析**：

```bash
$ infer run --bufferoverrun-only --bo-max-cfg-size 5 -- clang -c p02_loop_off1.c
Skipped large procedure (p02, size:50) in bufferoverrun-analysis.
  No issues found                                        ← 本来能报 L2 的
```

同类的还有 `--timeout`（默认 **120 秒/过程**）、`--pulse-max-cfg-size`。

**这条在真实工业代码上是实实在在的漏报来源**（几千行的状态机函数很常见），
而且提示只在日志里，不进 `report.json`。**审计手段**：

```bash
grep -c "Skipped large procedure" infer-out/logs
grep    "Skipped large procedure" infer-out/logs | sort -u | head -20
```

把这个数当成质量指标跟踪——它不为 0，就说明你的"0 告警"里有一部分是"没看"。

### 3.6 四个 FN 的归属总结

| 用例 | 两档结果 | 归因 |
|---|---|---|
| `p05_struct` (`struct S s; s.d[11]`) | 都漏报 | §3.1 |
| `p20_union` (`union U u; u.i[9]`) | 都漏报 | §3.1（union 局部变量同样走 `Tstruct` 分支） |
| `p12_global_noinit` | 都漏报 | §3.2 |
| `p13_fnptr` | 默认漏报 / 高灵敏只有符号噪声 | §3.3 |

对照组很能说明问题：`p05b_struct_ptr` / `p20b_union_ptr` 是**完全相同的越界**，
只是把基址改成形参 → **两者都 ✅ L1**。
**同一个 bug，写法换一下就从检出变漏报**——这正是"机制性缺口"的典型特征，
也是为什么这类缺口在真实代码上的影响远大于它在最小用例里看起来的样子。

---

## 4. 误报（FP）逐条归因

默认档 **0 误报**，所以本节讨论的全部是**高灵敏档**的代价。

### 4.1 高灵敏档的 16 条误报，来源只有两类

【实测数据】

| 来源 | 条数 | 出现在（共 13 个安全文件） | 本质 |
|---|---|---|---|
| `BUFFER_OVERRUN_L4` | 6 | `t06` `t07` `t10` `t11` `f05` `f07` | 符号条件本该等调用者，被 `--bo-debug 4` 强行报出 |
| `INTEGER_OVERFLOW_L5` / `U5` | 10 | `t02` `t03` `t07` `t08` `t13` `t15` `f07`×2 `f08` `f10` | 循环变量 / 指针步进的符号上界外推后，形式上可能溢出 |

噪声还不止体现在"安全文件被报"上：**在本来就有真 bug 的 22 个文件上，
高灵敏档打出 30 条告警去描述 19 个真 bug**——也就是说即便在真阳性文件里，
也混进了 11 条要人去甄别的噪声（`p08_nested3` 一个文件就报了 4 条，其中 3 条是噪声）。

举两个具体的，体会一下噪声长什么样：

- `t02_loop_exact.c`：`for (int i = 0; i < 10; ++i) s += a[i];` —— **完全正确的代码**，
  高灵敏档报 `INTEGER_OVERFLOW_L5`（`s += a[i]` 的累加形式上可能溢出 int）。
- `t11_strncpy.c`：`strncpy(buf, s, sizeof(buf)-1)` —— 教科书写法，报 `BUFFER_OVERRUN_L4`。

**这两种模式会在真实代码里遍地都是。** 这就是为什么 `Checker.ml` 里 L4/L5/U5
默认 `~enabled:false`——**不是保守，是这些等级在真实代码上不可用。**

### 4.2 参数与代价对照表

这张表是本文档给选型用的核心：

| 参数 | 默认值 | 调大/打开的收益 | 代价 | 建议 |
|---|---|---|---|---|
| `--bo-debug 4` | 0 | 绕过 SymbolicIssue，跨 TU 边界的条件也报 | **FP 暴涨**（本实验的主要来源） | ❌ 不要进 CI；专项排查可临时开 |
| `--enable-issue-type BUFFER_OVERRUN_L4/L5/U5` | 关 | 召回 +1/22 | 同上 | ❌ 同上 |
| `--enable-issue-type INTEGER_OVERFLOW_L5/U5` | 关 | 整数溢出线索 | 11/16 的误报来自这里 | ❌ 除非专门查整数溢出 |
| `--bo-max-cfg-size` | 15000 | 调大 ⇒ 少跳过大函数，**减少漏报** | 单函数分析时间可能失控 | ⭕ **建议先量化被跳过的函数数再决定** |
| `--timeout` | 120 s | 同上 | 总时长上升 | ⭕ 大工程可适当调大 |
| `--bo-field-depth-limit` | 无限制 | — | 调小=更快但更不准 | 保持默认 |
| `--bo-sound-unknown-sets-join` | 关 | 未知集合的 join 更保守（更少漏报） | 精度下降 ⇒ FP 上升 | ⭕ 值得在你们代码上 A/B 一次 |
| `--bo-context-sensitive-allocsites` | 关 | 同一函数不同调用点的 allocsite 区分开 ⇒ 更准 | 状态数上升 | ⭕ 值得 A/B |
| `--pulse-max-disjuncts` | 20 | 调大 ⇒ Pulse 少丢状态 | 耗时上升；**对数组越界无用** | — |
| `--jobs` | nproc | 并行 | 内存 × 并发数 | ⭕ 大工程必开 |
| `--scheduler` | file | `callgraph` 在某些工程上更快 | — | ⭕ 大工程可 A/B |

> **一句话**：影响**精度**的旋钮（L4/L5/bo-debug）**都不该动**；
> 影响**覆盖**的旋钮（max-cfg-size / timeout）**值得调**，因为它们减少的是
> "没看"造成的漏报，不牺牲精确率。这两类要分开谈——这正是你问题里
> "机制决定还是参数决定"最有价值的那条分界线。

---

## 5. 能力边界总表

把前面所有实测归到一张表。**"性质"一列是本文档的核心产出。**

| # | 场景 | 默认档 | 高灵敏 | 性质 | 要修的话 |
|---|---|---|---|---|---|
| 1 | 常量下标越界 | ✅ L1 | ✅ | — | — |
| 2 | for 差一 | ✅ L2 | ✅ | — | — |
| 3 | 指针算术 | ✅ L1 | ✅ | — | — |
| 4 | `memcpy` 尺寸错 | ✅ L1 | ✅ | — | — |
| 5 | `strcpy` 溢出 | ✅ L1 | ✅ | — | — |
| 6 | 指针别名 | ✅ L1 | ✅ | — | — |
| 7 | 跨函数（2 层） | ✅ L1 | ✅ | — | — |
| 8 | 跨函数（3 层嵌套） | ✅ L1 | ✅ | — | — |
| 9 | 递归 | ✅ L1 | ✅ | — | — |
| 10 | switch 多分支 | ✅ L2 | ✅ | — | — |
| 11 | VLA | ✅ L1 | ✅ | — | — |
| 12 | 二维数组 | ✅ L1 | ✅ | — | — |
| 13 | 数组形参退化 `int a[10]` | ✅ L1 | ✅ | — | — |
| 14 | `malloc` 尺寸不足 | ✅ L1 | ✅ | — | — |
| 15 | 全局数组（**有**初始化器） | ✅ L1 | ✅ | — | — |
| 16 | struct 字段数组（基址是**形参**） | ✅ L1 | ✅ | — | — |
| 17 | struct 字段数组（基址是 **malloc**） | ✅ L1 | ✅ | — | — |
| 18 | **struct/union 字段数组（局部/静态/全局）** | ❌ | ❌ | **机制** | **10–30 行**，`decl_local_loc` 的 `Tstruct` 分支递归字段 |
| 19 | **全局数组（无初始化器）** | ❌ | ❌ | **机制** | 20–50 行，`find_global_array` 回退到声明类型 |
| 20 | **C 函数指针调用** | ❌ | ⚠️ 仅符号噪声 | **机制** | **人月级**，需要指向集合分析 |
| 21 | 无调用者的符号条件 | ❌ | ✅ L4 | **参数** | 用全量分析解决，不要调参数 |
| 22 | 超大函数（CFG > 15000） | ❌ 静默跳过 | ❌ | **参数** | 调 `--bo-max-cfg-size`，先量化 |
| 23 | 路径相关（`if(f) i=20; if(!f) a[i]`） | ✅ 正确沉默 | ✅ | — | 非析取域反而不会误报 |
| 24 | 模运算 / 位掩码下标 | ✅ 正确沉默 | ✅ | — | — |
| 25 | 外部函数返回值做下标（有检查） | ✅ 正确沉默 | ✅ | — | — |

---

## 6. 对 CSA Summary 2.0 的直接启示

从这份边界表倒推，有五条是可以直接写进设计约束的：

1. **内存建模的覆盖面比分析算法更决定召回。**
   Infer 的区间域 + widening 是成熟的，但 #18 / #19 两个漏报都出在
   "某类基址拿不到 size"。**Summary 2.0 的第一优先级是把"哪些基址能拿到 extent"
   这件事做全**（局部数组、局部 struct 字段、全局、堆、形参），而不是先做精度。
   这一条和报告 §27 把 `AbstractRange` 列为 P0 是一致的，但**优先级要再提前**。

2. **不要走"用报告等级换召回"这条路。**
   实测 +1 个 TP 赔 13 个文件的 FP。你们的低漏报诉求要从
   **建模覆盖面**（上一条）和**全量跨文件分析**（§3.4）去拿，
   不是从放宽判定阈值去拿。

3. **分级 + "只传不报" 这个设计要抄。**
   `SymbolicIssue(propagate=true, 不报告)` 是 Infer 精确率能到 100% 的关键机制：
   **拿不准的不是丢掉，是继续往上传**，等上层有了实参再判。
   这恰好是你们"有限层次 inline"做不到的事。

4. **"没分析"和"分析了没问题"必须在产物里区分开。**
   `--bo-max-cfg-size` 跳过的函数只在日志里留一行，`report.json` 里毫无痕迹。
   Summary 2.0 应该给每个过程一个显式状态：
   `analyzed / skipped(reason) / timeout / unsupported(reason)`，
   并在 CI 里把 `skipped` 比例当成质量指标。**这条投入极小、收益极大。**

5. **能力边界必须用探针语料持续守护。**
   本次发现 #18 的过程说明：只测一种写法就下"支持"的结论是危险的。
   建议把 `corpus/` 这套 TP/TN/FP 四象限语料固化进 CI，
   每次引擎改动跑一遍，任何一格翻转都要有人解释。
   （这和你们测试用例的正交四象限约定是同一套方法论。）

---

## 7. 本文档没有回答的（诚实清单）

* **真实业务规模的数字**。以上全部是单文件级最小用例。

  > **【2026-09-20 部分补上】capture 阶段的真实数字已经有了**：
  > 使用方在 1383 全量工程（cdb 10640 条 / **10232 个唯一源文件**）上跑通了
  > 跨编译配置下的 capture，**100% 落库，328602 个过程**。
  > 这说明「Infer 能不能吃下你们的代码」这道门槛**已经过了**——
  > 这是报告 §31 列为第一风险的那一项。
  > 详见 `06-practice/03-cross-toolchain-capture-fix.md` §7。
  >
  > **但 analyze 阶段的数字还没有**：耗时、内存、告警数、抽样 TP/FP 仍然空白。
  > 而且这次 capture 用了 `-U__LP64__ -D__ILP32__=1` 人为改写目标宏，
  > `__SIZEOF_POINTER__` 仍是 8（builtin 改不了），
  > 所以**下一步必须先跑能力探针**（`./run-boundary.sh tp default`，预期 18/22），
  > 确认基线能力没被这套配置改坏，再解读全量告警。

  真实代码上的 FP 率会被"跨 TU 的符号条件被真正代换"改变——大概率**更好**，
  因为全量分析能判定掉一部分符号条件。必须在你们代码库上重测。
* **C++ 的情况**。本文全部是 C。C++ 的容器、模板、引用另有一套建模
  （`TypModels` 里的 `CppStdVector` 等），边界完全不同，未测。
* **`extern` 声明、在另一 TU 定义的全局数组**是否也漏报（§3.2 的变体），未测。
* **`--bo-sound-unknown-sets-join`、`--bo-context-sensitive-allocsites` 的实际效果**，
  只读了帮助文本，未做 A/B。这两个是参数表里唯一两个"可能白赚精度"的旋钮，
  值得在你们代码上各跑一次。
* **Infer 在你们内部 LLVM 14 fork 语法上的 capture 成功率**。
  见 `06-practice/03-cross-toolchain-capture-fix.md`——
  那份文档说明这一步本身就可能是主要成本。

---

## 8. 复现

```bash
cd 07-capability-boundary
./run-boundary.sh                 # 全部四组 × 两档，约 3–5 分钟
./run-boundary.sh tp default      # 只跑某一组某一档
./run-boundary.sh tp pulse        # Pulse 对照
```

单独复现 §3.1 那个最关键的发现：

```bash
cd 07-capability-boundary/corpus/probe
infer run -o /tmp/a --bufferoverrun-only -- clang -c v1_local_struct_const.c   # 0 条
infer run -o /tmp/b --bufferoverrun-only -- clang -c v7_struct_param_val.c     # 1 条 L1
# 再看 v1 的摘要为什么是空的：
infer debug -o /tmp/a --procedures --procedures-name --procedures-summary --select all
#   → BufferOverrunChecker: Safety conditions: {  }      ← 条件压根没生成
```
