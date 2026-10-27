# SPEC-0121 C++ 的 TU 照常分析：C 的写法与 C 相同，C++ 特有的写法保守；修 C++ 文件上的崩溃

状态：已实现（第 1 步）
来源：用户 2026-10-27 在公司用 clang_check 跑业务子系统，一个 `.cpp` 文件 bxen 段错误；debug 版断言
`NamedDecl::getName(): Name is not a simple identifier`，栈在 `oobsum::LowerTranslationUnit`（Lowering.cpp:1334）。
用户明确：**不能整个跳过 C++ 文件**——`.cpp` 里大量是 C 写法的代码，跳过就是漏报；只能对不支持的 C++ 特性保守处理。
（06- §1.3 的"P0 不做 C++"指不为 C++ 特性专门建模，不是不分析 C++ 文件——由本 SPEC 澄清。）

## 1. 原因

降级按 C 写的：函数、成员、记录的名字都调 `getName()`——运算符（`operator[]`）、构造 / 析构、类型转换函数的名字不是简单标识符，断言失败
（release 版是野指针、段错误）。修掉名字之后又暴露：成员函数作被调者时 `MemberExpr` 的类型是占位类型（绑定的成员函数），问它的大小
`getTypeSizeInChars` 断言 "Unknown builtin type"。另外几处按 C 理解会**算错**（不只是不精确）：

* 引用（`T &r`）被当成普通局部：所引对象的大小、被调者经引用写回调用方的值都会错——可能误报 DEFINITE，也可能推出错误的 SAFE；
* 成员运算符调用（`a[i]`）的第 0 个实参是对象本身，按下标对形参会整体错开；
* 重载、模板实例化共用一个名字：同一个键，摘要混在一起；
* 成员函数调用、构造函数会改对象（经 `this`），不建模就以为对象没变。

## 2. 设计

| 写法 | 处理 |
|---|---|
| 名字 | 不是简单标识符的用 `getNameAsString()`；显示名（告警、调用链）C 的 TU 与原来相同，C++ 带作用域（`Buf::operator[]`），只拼各层的名字、**不打印模板实参**（嵌套模板打印出来能无限长：上游 `CodeGenCXX/pr29160.cpp` 实测超时） |
| 键 | C++ 链接的函数 / 全局用 mangle 名（`ASTNameGenerator`：重载、命名空间、模板实参各不相同，跨 TU 一致）；`extern "C"` 的与 C 的 TU 相同（C 与 C++ 互相调用时接得上）；C 的 TU 逐字不变 |
| 引用 | **按指针建模**：引用形参 / 引用局部的值是所引对象的地址，用它就是访问那个对象；实参传给引用形参时传地址；返回引用 = 返回地址，调用表达式本身是那个对象。复用 C 指针的整套机制（被调者经引用写回、调用方能看到）。绑定到引用的变量算"被取了地址"。引用成员、全局的引用：所引对象不跟踪（未知地址） |
| 成员函数调用（含虚函数、成员运算符） | `this` 不建模：当作目标未知的调用（`calleeExpr = unk(cxx_call)`）——对象的地址和实参都交出去，经它们可达的内存失效；实参里有指针时调用点记 SUSPECT **`cxx_call`**（新原因）。绝不因此推出 SAFE |
| 构造函数 | 平凡的默认构造什么也不做；平凡的拷贝 / 移动 = 结构体拷贝（与 C 的 `S t = s;` 相同）；其余与成员调用相同（被构造对象的地址 + 实参）。构造函数在 CFG 里是单独的元素，降级时按 `ctorTarget` 找到被构造的局部 |
| `new` | 构造函数照上一行；结果是未知指针（`cxx_new`；以后可按 operator new 的字节数建模成 heap） |
| 模板 | 只降实例化（`shouldVisitTemplateInstantiations`），原型（依赖上下文）跳过；依赖类型、未推导的 `auto`、占位类型的大小一律当未知（不问 ASTContext） |
| lambda | 闭包的 `operator()` 照常降级（捕获的变量经 `this`，不跟踪） |
| `ExprWithCleanups`、`CXXBindTemporaryExpr` | 看里面的表达式 |
| 其余 C++ 特有的节点（`this`、`typeid`、`throw`、成员指针…） | 落到原来的"未知"（A-08）：值 Top、地址未知，访问照样查（报 SUSPECT） |

## 3. 对召回与误报的影响

* C 的 TU：逐字不变（`run.py csa`、`replay.py`、`engine_check` 与原来相同）。
* C++ 的 TU：原来崩溃（整个文件没有结果）→ 现在照常分析。C 写法的部分与 C 相同；成员函数里经 `this` 的访问、成员调用之后的值报 SUSPECT
  （`untracked_pointer`、`cxx_call`、`type_range_only`…）——C++ 类写得多的文件会比较吵，第 2 步建模 `this` 后会少。

## 4. 实现位置

`lower/Keys.{h,cpp}`（`SetContext`、`NameOf`、`DisplayName`、模板实例化）；`lower/Lowering.cpp`（`ProtoOf`、`ByReference`、`IsMemberCall`、`AddressTaken`、
`RefVar`、`ArgValue`、`CallResult`、`MemberCall`、`CtorCall`、`LowerDecl`、`LowerReturn`、`SizeOf` / `Width`、`ModuleCollector`）；`lower/TypeLayout.cpp`；
`core/Interp.cpp`（`RecordCall`：`cxx_call`）；`core/Engine.cpp`（正文）；`core/Summary.cpp`（原因表）；`OobSummaryChecker.cpp`（`SetContext`、调试内建函数与调用图的名字）；
`oobsum/cases/summaries/replay.py`（原因表）；`oobsum_run.py cdb` 按扩展名数语言。

## 5. 测试

* lit `cxx.test`（`Inputs/cxx/cases.cpp`）：① `.cpp` 里的 C 写法 ② 引用形参经三层 ③ 经引用写回 / 引用局部 ④ 重载 ⑦ 模板——TP 报 DEFINITE、TN 不报；
  ⑤ 成员函数 / 构造函数改了对象之后报 SUSPECT（`cxx_call` + `type_range_only`，不推 SAFE）；⑥ 平凡的结构体拷贝与 C 相同（TN 不报）；⑧ 虚函数 `cxx_call`；
  Dump=keys：两个 `Put` 重载 `_ZL3PutPhj` / `_ZL3PutPh`、`TLeaf<UINT8>` / `TLeaf<UINT32>` 的键不同；`mixed.cpp`（运算符、构造 / 析构、类型转换、模板、成员调用）
  `Dump=all` 不崩；`impl.cpp` + `ccall.c`：C 文件经三层调用 `.cpp` 里 `extern "C"` 的函数，三阶段 TP 报 DEFINITE、TN 不报。
* 崩溃压力（开断言的 clang）：上游 `clang/test/Analysis`、`SemaCXX`、`CodeGenCXX` 的 2473 个 `.cpp`：1582 个正常、891 个本身编译不过（分析器不跑）、
  **0 崩溃、0 超时**（修 `DisplayName` 之前 `pr29160.cpp` 超时）。
* C 的回归：llvm-lit oobsum 107/107；单测 161；`run.py csa` 回退 0；`replay.py` 0 处不一致；`engine_check` 14/16。

## 6. 调试

`Dump=keys` 看 C++ 函数的键（mangle 名）；`Dump=fir` 看引用形参的类型（`ptr`）与成员调用（`call unk(cxx_call)`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §1.3 不做（P0） | C++ | 不为 C++ 特性专门建模；C++ 的 TU 照常分析，C 的写法与 C 相同，C++ 特有的写法保守（SPEC-0121） |
| D-27 账本原因 | … | 加 `cxx_call`：经 C++ 成员函数 / 构造函数 / 虚函数调用交出指针（SPEC-0121） |
| D-01 键 | 名字 | C++ 链接的用 mangle 名（SPEC-0121） |

## 8. 公司侧验证

COMPANY.md §0 清单 2026-10-27-2：崩溃的那个文件单独重跑（不崩、正常出结果）；全量 B 里 C++ 文件的 TU 数与失败数。

## 9. 待定 / 以后

第 2 步（看业务代码里 C++ 有多少再定）：`this` 建模成方法的第 0 个形参、成员调用传对象地址并按摘要代换（成员函数里经 `this` 的访问、成员调用之后的值都会精确）；
`new` 按字节数建模成 heap；lambda 的捕获。
