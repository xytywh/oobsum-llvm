# SPEC-0007 降级第一步：类型布局与模块骨架（`lower/TypeLayout`、`lower/Lowering`），接上 `Dump=fir`

状态：已实现（S4b）
来源：`STATUS.md` S4b；06- §5.2 D-10（类型）、D-11（局部）、§5.4（D-30…D-39 大小与布局，D-39：布局取被分析 TU 的 `ASTContext`）、
A-01（CFG 用 `AnalysisManager::getCFG`）、X-01（导出主文件与头文件里所有有函数体的函数、所有全局、用到的记录类型、目标信息）；
SPEC-0006 §3（`Dump=fir`）；`csa-checker-dev` 的结构体名约定（typedef 反查 → tag 名 → `<anonymous struct/union>`）。

## 1. 要解决什么

把一个 TU 降成 `fir::Module` 的"骨架"：目标信息、文件表、类型与记录布局、全局、每个有函数体的函数的签名 / 局部 / CFG 块与边。
指令（赋值、访问、调用、返回）在 S4c，条件边的 `assume` 在 S5。先把骨架做对，`Dump=fir` 就能在业务代码上看布局（ILP32 是否生效、
typedef 名是否取对），这是后面一切大小判定的基础。

## 2. 设计

### 2.1 类型（`TypeLayout::Map`，取规范类型，去掉限定符与 typedef）

| Clang 类型 | FIR |
|---|---|
| 整数（含 `char`、`_Bool`、枚举取底层类型） | `{int, w: 存储位宽, s: 有无符号}`；**`char` 的符号取 TU 的编译选项**：driver 按目标给（ARM / AArch64 传 `-fno-signed-char` → 无符号，x86 有符号）。**直接跑 cc1 时默认有符号**（实测），所以 `cc1-run.sh`、`run.py csa` 对 ARM / AArch64 目标自动加 `-fno-signed-char`；公司经 driver / cdb 跑，不受影响 |
| 指针 | `{ptr, to}`；指向函数、不完整结构体的指针 → `to: {other, size: 0}` |
| 定长数组 | `{arr, n, elem}`（`[0]`、末尾 `[1]` 照实写，是否当柔性由引擎按 D-37 判） |
| `[]` / VLA | `{arr, n: null, elem}` |
| 完整的结构体 / 联合体 | `{rec, id}`，记录进 `records` |
| `void` | `{void}` |
| 函数类型、不完整的结构体 / 联合体 / 数组 | `{other, size: 0}`（大小未知） |
| 浮点、复数、向量等其它 | `{other, size: 字节数}` |
| `_Atomic(T)` | 同 `T` |

### 2.2 记录

* 号按第一次用到的顺序从 0 分配（确定）；先分号再算字段，`struct node *next` 这种自引用不会无限递归。
* `name`：**① 反查 typedef 的干净名**（TU 顶层 `typedef struct XxxTag {…} Xxx;` → `Xxx`，同一记录有多个 typedef 取源码里第一个）
  **→ ② tag 名 → ③ `<anonymous struct/union>`**。
* `size`：`ASTContext::getTypeSizeInChars`（柔性数组成员不计入）；字段 `off`：`ASTRecordLayout::getFieldOffset / 8`——**全部按被分析 TU 的目标 ABI**（D-39），
  所以 `-triple armebv7-linux-gnueabi` 下指针 / `long` 是 4 字节，x86_64 下是 8 字节。
* 联合体：所有字段 `off = 0`（FIR 不另加标记）。C11 匿名成员：字段名为空串、类型是记录。
* 位域：`off` 取所在位偏移 / 8（向下取整），类型 `{int, w: 位宽, s}`（w 可以不是 8 的倍数，就是位域）；无名位域（填充）不导出。

### 2.3 模块

* `tu` = TU id（SPEC-0003），`file` = 规范化主文件路径；`target`：`triple`、`ptr`（指针字节数）、`long`、`big_endian`，都取 `TargetInfo`。
* `files`：主文件是 0 号，其余按第一次用到的顺序；所有位置取宏展开位置（D-06）。
* `globals`：文件作用域的变量、块作用域的 `extern`、函数内 `static`，同一实体只出一条（规范声明去重）；`key` 见 SPEC-0003；
  `def` = 本 TU 有定义（含暂定定义）；位置取定义（没有定义取第一处声明），类型取**最新一条声明**（Sema 把合成类型放在它上面）——
  `extern int a[];` 后面又有 `int a[10];` 时是 `i32[10] def`。实测坑：`VarDecl::getActingDefinition()` 只在"这一条本身是暂定定义"时非空，
  在 extern 那条上调用拿不到；`lower/Keys` 的 `VarDefinition` 沿声明链找（S2 的键表同样受益，`fir-module.c` 的 `KEYS` 检查守住）。
* `funcs`：本 TU 里（含头文件）**所有有函数体的函数**，AST 遍历顺序。

### 2.4 函数

* `key`、`name`、`ext`（外部链接）、`loc`、`ret`。
* `locals`：先形参（`id = param = 下标`），再函数体里的局部变量（遍历顺序，同名的嵌套作用域变量各有各的号）；函数内 `static`、块作用域 `extern` 不是局部（在 `globals` 里）。
  **`obj`**（D-11）：类型是数组或记录，或者**在函数体里被取过地址**（`&x`）。
* 块与边：`AnalysisManager::getCFG(fd)`（与 CSA、`debug.DumpCFG` 同一份 CFG，块号一致）；块按号**从大到小**（入口在前、出口 `b0` 在后）；
  边取每个后继里可达的那个（Clang 已判定不可达的边不导出）。**本步的边都不带 `assume`**——无条件边是对执行的过近似，对召回是安全的（S5 加条件）。
* **本步还没有指令**：函数带 `flags: ["body_pending"]`（临时标记，S4c 去掉），引擎接上前必须补齐。取不到 CFG 的函数：`flags: ["no_cfg"]`，
  只有一个块 `b0`（既是入口也是出口）。
* `MaxBlocks`（A-09）选项还没注册，留到 S5 / 选项 SPEC。

### 2.5 `Dump=fir`（SPEC-0006 §3）

只有 `Dump=fir` 打开时才降级。stderr 每行 `[oobsum:fir] …`：先模块头，再按 `DumpFilter` 过滤后的函数；同样的文本写
`<WorkDir>/dump/<tu-id>/fir/<tu-id>.txt`，整个模块的 JSON（缩进 1）写 `<tu-id>.json`。写完立刻自检：JSON 读回来（`ModuleFromJson`）、
再写出与原来逐字相同，否则打 `[oobsum:fir] error: <原因>`。

## 3. 对召回与误报的影响

不产生判定。`Dump=fir` 不开时不降级，默认运行不变（lit、`run.py csa` 基线不变）。

## 4. 实现位置

| 文件 | 内容 |
|---|---|
| `OobSummary/lower/TypeLayout.{h,cpp}` | `TypeLayout`：`QualType` → `fir::TypeId`、记录布局、typedef 名反查（缓存） |
| `OobSummary/lower/Lowering.{h,cpp}` | `LowerTranslationUnit`：模块骨架；`FunctionLowerer`（局部、`obj`、CFG 骨架；S4c 在这里加指令） |
| `OobSummaryChecker.cpp` | `dumpFir` |

## 5. 测试

| lit | 覆盖 |
|---|---|
| `fir-layout.c` | 同一份源码在 `armebv7-linux-gnueabi`（ILP32 + 大端）与 `x86_64-pc-linux-gnu`（LP64）下的记录大小与字段偏移不同；`char` 的符号随目标；typedef 名反查（`typedef struct XxxTag … Xxx` 取 `Xxx`；只有 tag 名；匿名）；联合体；嵌套记录；数组成员；柔性数组 `[]` 与 `[0]`；位域；枚举、`_Bool`；指向不完整结构体 / 函数的指针 |
| `fir-module.c` | 全局（定义 / `extern` / `static` / 函数内 `static` / 块作用域 `extern`，`extern int a[]` + 定义取完整类型）；函数（外部 / `static` / 头文件 `static inline`）；局部的 `param` / `obj`（数组、结构体、取过地址的标量、没取地址的标量）、同名嵌套变量；`if` / `while` 的 CFG 块与边（块号与 `debug.DumpCFG` 对照）；单块函数；`body_pending`；`DumpFilter`；`txt` / `json` 落盘；自检无 `error` |

## 6. 调试

`-analyzer-config rb.enhance.ipa.OobSummary:Dump=fir`（`DumpFilter=^函数名$`）；块号与 `-analyzer-checker=debug.DumpCFG` 的 `[B<n>]` 一致。
布局可疑时对照 `-fdump-record-layouts`（`clang -cc1 -fdump-record-layouts -triple …`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-10 | 类型的六种形式 | 补充 §2.1 的映射（函数类型 / 不完整类型 → `{other, size: 0}`；`char` 符号随目标；位域见 §2.2） |
| D-14 | `if` / 循环条件降成两条边上的 `assume` | S4b 先导出无条件边（过近似），S5 加 `assume` |

## 8. 公司侧验证

bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=fir`（可加 `DumpFilter`）跑 TestCase：看 `module` 行的 `ptr=4 long=4 big-endian`
是否成立（即 fork 是否按 ILP32 分析，对应探针 R2）；业务代码上看几个常用结构体的 `size` 是否与 `sizeof` 一致。只带回"是否一致"，不带回内容。

## 9. 待定 / 以后

* `MaxBlocks`（A-09）、超大 TU 的降级耗时（全量 export 时量）。
* 全局的常量初值（A-28）、位域的读写语义：S4c / S7 用到时补。
