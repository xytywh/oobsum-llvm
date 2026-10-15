# SPEC-0006 FIR 的文本记法与 `Dump=fir`

状态：已实现（§2 文本记法 S4a；§3 `Dump=fir` 接线 S4b，见 SPEC-0007）
来源：SPEC-0001 §6.1（`fir`：每个函数的 FIR，文本记法 + 文件）、CLAUDE.md §5.5（每个引擎环节配 Dump 种类）、06- T-02（降级快照用例
用 `Dump=fir` 的文本）。

## 1. 要解决什么

降级（S4b 起）结果不对时，要像 `debug.DumpCFG` 那样一眼看出每个函数被降成了什么；T-02 的快照用例也要一个稳定、可 FileCheck 的文本。
JSON 太啰嗦（一条 store 一大段），所以另定一种一行一条的文本记法，由 `core/` 里的纯函数从 `fir::Module` 生成（不碰 Clang，可单测）。

## 2. 文本记法（`core/FirText.h`）

```cpp
std::vector<std::string> ModuleHeaderText(const fir::Module &m);              // module / record / global 行
std::vector<std::string> FunctionText(const fir::Module &m, const fir::Function &f);
std::string TypeText(const fir::Module &m, fir::TypeId t);
std::string ExprText(const fir::Function &f, fir::ExprId e);
std::string InstText(const fir::Module &m, const fir::Function &f, const fir::Inst &inst);
```

### 2.1 模块与函数

```
module tu=abe9fde904012438 file=/src/ue/ue_init.c target=aarch64_be-unknown-linux-gnu ptr=4 long=4 big-endian
record #0 UE_TBL_S size=16 { ulId@0: u32; aucData@4: u8[12] }
global G:g_ulUeNum: u32 def @ue_init.c:12:8
fn F:Ue_InitOne ext -> void @ue_init.c:40:6
  local l0 pstTbl param0: rec#0*
  local l1 ulIdx param1: u32
  b2 entry:
    store4 [padd(l0, l1 * 16)] = 0  @42:5
    -> b0
  b0 exit:
```

* 函数里的位置只写 `@行:列`；不在主文件的写 `@文件名:行:列`（文件名取最后一段）。模块级的行都带文件名。
* 块：`b<号>`，入口 / 出口标 `entry` / `exit`；边一行一条：`-> b<号>`，有条件时 `-> b<号> if <表达式>`。
* `flags` 非空时函数行末尾加 `[cfg_too_large …]`。
* 局部：`local l<号> <名字>[ param<i>][ obj][ tmp]: <类型>`。

### 2.2 类型

| FIR 类型 | 文本 |
|---|---|
| `{int, w, s}` | `i<w>`（有符号）/ `u<w>`（无符号） |
| `{ptr, to}` | `<所指>*` |
| `{arr, n, elem}` | `<元素>[n]`；`n` 为 null 时 `<元素>[]` |
| `{rec, id}` | `rec#<号>`（名字在 record 行里） |
| `{void}` / `{other, size}` | `void` / `other<size>` |

### 2.3 表达式

| FIR | 文本 | 例 |
|---|---|---|
| `c` / `l` | 数字 / `l<号>` | `16`、`-5`、`l1` |
| `bin`（数学整数） | `a <op> b`，op 取 `+ - * / % << >> & \| ^`；**作操作数的 `bin`（含回绕的）/ `cmp` 一律加括号** | `l1 * 16`、`(l1 + 1) * 4` |
| `bin`（回绕） | `(a <op> b):<u\|i><w>` | `(l9 - 1):u32`、`((l9 - 1):u32) * 4`、`(i16)((l9 - 1):u32)` |
| `cmp` | `a <op><s\|u> b`，op 取 `== != < <= > >=` | `l1 <u 16` |
| `lnot` / `neg` / `bnot` | `!a` / `-a` / `~a` | |
| `cast` | `(<u\|i><w>)a` | `(i16)l3` |
| `laddr` / `gaddr` / `faddr` | `&l<号>` / `&<键>` | `&l2`、`&G:g_aucBuf`、`&F:Callee` |
| `str` | `str<长度>` | `str5` |
| `padd` | `padd(p, e)` | `padd(&l2, l1 * 4)` |
| `fld` | `fld(p, <名字>@<偏移>:<大小>)` | `fld(l0, aucData@4:12)` |
| `psub` | `psub(a, b, <元素大小>)` | |
| `unk` | `unk(<原因>)` | `unk(float)` |

### 2.4 指令

| FIR | 文本 |
|---|---|
| `set` | `l<d> = <值>` |
| `load` | `l<d> = load<字节> [<地址>]`，下标访问末尾加 ` sub` |
| `store` | `store<字节> [<地址>] = <值>`；值为 null 时 `= ?`；下标访问加 ` sub` |
| `call` | `[l<d> = ]call <键>(<实参>, …) #<序号>`；间接调用 `call *(<表达式>)(…) #<序号>` |
| `havoc` | `l<d> = havoc(<原因>)` |
| `ret` | `ret <值>` / `ret` |

每条指令后面两个空格再跟位置。

## 3. `Dump=fir`（S4b 接线）

* stderr：每行 `[oobsum:fir] ` 加上 §2 的一行；先打模块头（module / record / global），再按函数打；`DumpFilter` 按函数名过滤函数（模块头总打）。
* 文件：`<WorkDir>/dump/<tu-id>/fir/<tu-id>.txt`（与 stderr 相同的文本，不带前缀）与 `<WorkDir>/dump/<tu-id>/fir/<tu-id>.json`
  （整个模块的 JSON，就是 export 阶段 `fir/<tu>.json` 的格式，不按 `DumpFilter` 过滤）。修订 SPEC-0001 §6.1 的"`fir/<fn>.json`"：
  单个函数的 JSON 离开模块的类型表、文件表就读不了，所以按模块落盘。
* **自检**：写出 JSON 后立刻用 `ModuleFromJson` 读回；读不回来（降级产出了不合法的 FIR）就打一行 `[oobsum:fir] error: <原因>`。
  只读，不影响判定。
* 降级只在 `Dump=fir` 打开时才跑（引擎接上前不增加默认运行的开销）。

## 4. 对召回与误报的影响

只是调试输出，不影响判定。

## 5. 实现位置

`OobSummary/core/FirText.{h,cpp}`（S4a）；`OobSummaryChecker.cpp` 的 `dumpFir`（S4b）。

## 6. 测试

* 单元测试 `oobsum/unittests/core/FirTextTest.cpp`：§2.1 的例子逐行相同；每种类型、表达式（含括号规则、回绕、有无符号比较）、
  指令（含 null 值、下标、间接调用、无返回值的调用）、条件边、flags、非主文件的位置。
* lit（S4b）：`fir-*.c` 用 `Dump=fir` 做快照。

## 7. 调试

本 SPEC 就是调试手段。降级结果不对时：`-analyzer-config rb.enhance.ipa.OobSummary:Dump=fir`（加 `DumpFilter=^函数名$`）。

## 8. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0001 §6.1 `fir` | `WorkDir/dump/<tu>/fir/<fn>.json` | `WorkDir/dump/<tu>/fir/<tu>.{txt,json}`（§3） |

## 9. 公司侧验证

不需要单独验证；S4b 之后可在 bxen 命令里加 `Dump=fir` 看业务代码的降级结果（不带出公司）。

## 10. 待定 / 以后

* 摘要的文本记法（D-29，`Dump=summary`）另写 SPEC，与本记法共用表达式的写法。
