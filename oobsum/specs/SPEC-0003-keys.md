# SPEC-0003 函数 / 全局的键（06- D-01…D-08）与 `Dump=keys`

状态：已实现（S2）
来源：`STATUS.md` S2；06- §5.1 D-01…D-08（决策 D13：自定义键，不用 USR）；SPEC-0001 §5（判定日志的 `fn` "引擎接上后是函数键"）。

## 1. 要解决什么

摘要、FIR、调用点、判定日志、指纹都要用一个**跨 TU 稳定**的名字指函数和全局：外部链接的同名即同一个；`static` 的要带定义文件，
不同文件里的同名 `static` 不能混；头文件里的 `static inline` 在每个 TU 里各有一份，要能认出是"同一个"。
骨架里调用图输出用的是临时写法（`static` 函数带 TU id 而不是定义文件 id），`TuId` 也没做 D-05 的 `make_absolute`。

## 2. 设计

### 2.1 键的写法（06- D-01…D-05，原样实现）

| 对象 | 键 | 判据（Clang 14） |
|---|---|---|
| 外部链接函数 | `F:<名字>` | `FunctionDecl::isExternallyVisible()` |
| 内部链接函数 | `F@<定义文件 id>:<名字>` | 其余函数；定义文件取定义（没有定义取第一处声明）的展开位置所在文件 |
| 外部链接全局 | `G:<名字>` | 文件作用域或块作用域 `extern` 的 `VarDecl`，`isExternallyVisible()` |
| 内部链接全局 | `G@<定义文件 id>:<名字>` | 文件作用域 `static` |
| 函数内 `static` | `S@<所在函数的键>:<名字>` | `VarDecl::isStaticLocal()` |
| 其它局部 | 没有键（空串） | 普通局部、形参不进键表 |

* **文件 id（D-05）**：路径先 `make_absolute`（相对于进程当前目录，即 compile_commands 的 `directory`）、`remove_dots`、统一成 `/`，
  再取 xxHash64 的 16 位小写十六进制。**TU id 用同一个函数**（`TuId` 改为调用 `FileId`）：以前相对路径调用时 TU id 按相对路径算，
  现在与绝对路径调用一致（判定日志文件名随之一致）。
* **头文件定义（D-02 后半、D-08）**：定义文件不是主文件的内部链接函数，键表里标 `header`。D-02 要求它的键再加 `#<FIR 哈希前 8 位>`，
  FIR 在 S4 才有，**本 SPEC 先不加**；S4 补上后，同一头文件里同名、但因宏展开不同而函数体不同的 `static inline` 才能区分开。
  在此之前它们共用一个键（`F@<头文件 id>:<名字>`），这正是 D-08 期望的"FIR 相同只求解一次"的那一半。
* D-06（位置）、D-07（同名外部函数多个定义）不在本 SPEC：前者随 FIR，后者是 solve 阶段的解析规则（A-80）。
* `KeyName(key)`：从键取回名字（最后一个 `:` 之后、`#` 之前），与 `run.py` 的 `_fn_name` 同义。

### 2.2 用到键的地方（本 SPEC 改的）

* `Dump=callgraph`：每个函数写它的键（原来 `static` 函数带 TU id；主文件里的 `static` 函数键不变，因为主文件的文件 id 就是 TU id）。
* 判定的 `fn`：调试内建函数产出的判定，`fn` 由函数名改为**所在函数的键**；指纹（F-34）的入口键随之改为函数键。
  这是 SPEC-0001 §5 原定的写法，现在有了键就照做，引擎接上后的判定也一样。

### 2.3 `Dump=keys`（新种类）

每个 TU 按 AST 遍历顺序打印有键的实体：有函数体的函数、文件作用域 / 块作用域 `extern` 的全局、函数内 `static`。同一实体的多次声明只打印一次
（取规范声明，`def` = 任何一次声明是定义）。某个定义文件第一次被引用时先打印一行 `file`，把 id 对回路径：

```
[oobsum:keys] file 3fa1c2d4e5f60718 /src/ue/ue_init.c
[oobsum:keys] F:Ue_InitOne func def /src/ue/ue_init.c:40:6
[oobsum:keys] file 9c02aa11bb22cc33 /src/inc/pub.h
[oobsum:keys] F@9c02aa11bb22cc33:Pub_Min func def header /src/inc/pub.h:12:20
[oobsum:keys] G:g_ulUeNum global decl /src/inc/ue.h:8:15
[oobsum:keys] S@F:Ue_InitOne:s_ulCnt static def /src/ue/ue_init.c:42:19
```

字段：键、种类（`func` / `global` / `static`）、`def` / `decl`、可选 `header`、位置（规范化路径:行:列，展开位置）。
同样的内容（不带 `[oobsum:keys] ` 前缀）写进 `<WorkDir>/dump/<tu-id>/keys/keys.txt`。
`DumpFilter`：函数按名字、函数内 `static` 按所在函数名、全局按自己的名字过滤；`file` 行只在有实体被打印时出现。

## 3. 对召回与误报的影响

没有判定逻辑变化。键只决定"哪些是同一个函数 / 全局"，规则与 D-01…D-05 一字不差；头文件 `static inline` 暂时同名同键，
最坏情况是两份不同的函数体被当成一个（S4 加哈希后消除），这只会在 S8 摘要接上之后才影响判定，届时已补上。

## 4. 实现位置

| 文件 | 内容 |
|---|---|
| `OobSummary/core/Key.{h,cpp}` | `NormalizePath`、`FileId`、`FunctionKey`、`GlobalKey`、`StaticLocalKey`、`KeyName`（不依赖 Clang） |
| `OobSummary/core/Fingerprint.cpp` | `TuId` 改为调用 `FileId` |
| `OobSummary/lower/Keys.{h,cpp}` | `KeyMaker`：`FunctionDecl` / `VarDecl` → 键、定义文件、是否在头文件（只有这里碰 Clang AST） |
| `OobSummaryChecker.cpp` | `Dump=keys`（`dumpKeys`）；调用图输出与调试判定的 `fn` 改用 `KeyMaker` |
| `OobSummary/debug/DumpSink.cpp` | 已知种类加 `keys` |

## 5. 测试

| lit 用例 | 覆盖 |
|---|---|
| `keys.c`（+ `Inputs/keys.h`、`Inputs/keys-b.c`） | 外部 / `static` 函数；外部 / `static` / `extern` 声明的全局；块作用域 `extern`；函数内 `static`；头文件 `static inline` 标 `header`、键带头文件 id；`DumpFilter`；`keys.txt` 落盘；两个 TU 里同一个头文件函数键相同、各自的同名 `static` 键不同；相对路径与绝对路径调用得到同一个文件 id |
| `dump-callgraph.c`（不改） | `static` 函数的键形如 `F@<id>:名字` |
| `cases-csa.test`（改期望） | 判定日志的 `fn` 是键：`fn=F:D_Helper` |
| `decision-log.c`（加一行检查） | `"fn":"F:f"` |

`core/Key` 的单元测试在 S3 建好 `oobsum-core-tests` 后补上（S3 的验收项）。

## 6. 调试

`-analyzer-config rb.enhance.ipa.OobSummary:Dump=keys`（可加 `DumpFilter=<正则>`）；`WorkDir` 非空时看 `<WorkDir>/dump/<tu-id>/keys/keys.txt`。
判定日志里的 `fn`、摘要里的 `key` 对不上时，先用它看两边 TU 给同一个函数算出的键与文件 id。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-02 | 定义在头文件里的再加 `#<FIR 哈希前 8 位>` | 不变；S2 先不加（FIR 未就绪），S4 补上 |
| SPEC-0001 §5 `fn` | 引擎接上后是函数键 | 现在就是函数键（调试判定也是） |
| SPEC-0001 §6.1 种类表 | —— | 新增 `keys` |

## 8. 公司侧验证

`oobsum/testcase/OobSummaryChecker/` 的告警数不变（3 条）。可选：bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=keys`，
确认 fork 上 `isExternallyVisible()` 与本地一致（输出里 `static` 函数是 `F@…`，其余 `F:…`）。

## 9. 待定 / 以后

* S4：头文件内部链接函数的 `#<FIR 哈希前 8 位>`。
* 宏生成的函数（定义位置在宏展开里）以展开位置定文件，与 D-06 一致；如果业务代码里有大量"一个宏在多个文件里生成同名 `static` 函数"，
  键按展开所在文件区分，不会混。
