# STATUS — 现在做到哪、下一步做什么

> 每次会话开始先读这里；每次提交一批改动后更新这里（"已完成"挪进 CHANGELOG 时可以删）。
> 规则在 `../CLAUDE.md`，背景与决策在 `HANDOFF.md`，主规格 `06-spec.md`，增量规格 `specs/`。

更新：2026-10-16 · 分支 `oobsum/dev` · 版本 v4.0

## 已完成

* 仓库化：调研包 v3.0–v3.15 逐版导入为提交（`git log -- oobsum/`）；`CLAUDE.md`、`HANDOFF.md`、`COMPANY.md`、`specs/` 流程、
  `history/` 纪要；`devenv/` 改为在本仓库里构建（`cmake-build-release/`）；`land.py` 支持仓库内清单（同步语义、`--check-tree`）。
* checker 骨架（SPEC-0001）：
  * `rb.enhance.ipa.OobSummary` 注册；选项 `Phase`、`WorkDir`、`Policy`、`InlineDepth`、`ReportSafe`、`SuspectReasons`、`Dump`、`DumpFilter`。
  * 标签 DEFINITE / POSSIBLE / SUSPECT / SAFE（`core/Label`）、判定（`core/Decision`）、报告策略（`core/Policy`）、指纹（`core/Fingerprint`）。
  * 告警：每个标签一个 BugType，消息 `[OOB-<标签>[:<原因>]] …`，按（文件、行、列、指纹）排序（`report/Reporter`）。
  * 判定日志 `WorkDir/decisions/<tu-id>.jsonl`，报了的、没报的都在（`core/DecisionLog`）；`oobsum/tools/decisions.py` 汇总。
  * 调试输出 `Dump=options+callgraph+decisions+stats`、`DumpFilter`（`debug/DumpSink`）；`fir`、`state`、`summary`、`apply` 已预留，等引擎。
  * 调试内建函数 `oobsum_debug_emit(label, reason, depth)`：用例里驱动"判定 → 标签 → 告警 / 日志"。
  * lit：`clang/test/Analysis/oobsum/` 6 个用例全过；公司 TestCase `oobsum/testcase/OobSummaryChecker/`（预期 3 条告警）。
* 冒烟第 6 项修复：`replay.py` 在 jsonschema 3.x（Ubuntu 22.04 的 `python3-jsonschema`）下解析 schema 的相对 `$id` 出错崩溃；
  现在校验前去掉 `$id`，校验器自身出错时打印异常并退回内置检查。`40-smoke.sh` 7 项全过。
* S1 回归尺子（SPEC-0002）：`python3 oobsum/cases/run.py csa` 用本仓库的 clang（`-cc1 -analyze`，`armebv7-linux-gnueabi`）跑 67 个用例 / 70 个 TU，
  读判定日志按 `@expect` 对照（口径修订 06- T-03：SUSPECT 计为召回），并与入库基线 `oobsum/cases/csa-baseline.json` 比"召回不回退"
  （原来报出的判定消失、同一处又没有 SAFE → 失败）。**当前基线**（引擎未接入）：134 个入口 通过 69（全是 `-` / TN 的空通过）、漏报 63、
  需跨TU 2（LV1-07、LV6-01），报出 0 条。lit `cases-csa.test`（合成用例在 `Inputs/cases/`）。CLAUDE.md 的每提交检查加上 `run.py csa` 无回退。
* S2 函数键（SPEC-0003）：`core/Key`（`FileId` 按 D-05 含 `make_absolute`，`TuId` 改用它）、`lower/Keys`（`KeyMaker`、`CollectKeys`）；
  `Dump=keys`（stderr + `WorkDir/dump/<tu>/keys/keys.txt`）；调用图输出与调试判定的 `fn` / 指纹入口都用函数键。头文件内部链接函数标 `header`，
  `#<FIR 哈希>` 留到 S4。文件 id 与 `replay.py` 的 Python xxh64 交叉核对一致。lit 8 个全过（新增 `keys.c`）。
* S3a `core/` 单元测试（SPEC-0004）：`oobsum/unittests/`（框架 `Test.h`，独立 CMake 工程链接 `LLVMSupport`）+ `oobsum/devenv/core-tests.sh`；
  17 个用例：键、指纹（与 `replay.py` 的 `fingerprint()` 同值）、报告策略全部分支、标签。lit `core-unittests.test` 驱动，lit 9 个全过。
* S3b FIR（SPEC-0005）：`core/Fir`（结构、类型表去重、表达式池与构造函数、按号查找）、`core/FirJson`（写出、严格读入带出错位置、
  文件读写先 `.tmp` 再改名）；§5.2 没写死的 10 处约定见 SPEC-0005 §2.3。单测 26 个（新增 9 个：§5.2 例子逐字往返、全部
  16 种表达式 / 6 种指令 / 6 种类型往返、18 种坏输入逐条带位置报错、嵌套过深、文件往返）。
* S4a FIR 文本记法（SPEC-0006 §2）：`core/FirText`（模块头、函数、类型、表达式、指令；`bin` / `cmp` 作操作数一律加括号）；
  单测 30 个（新增 4 个：§2.1 例子逐行、全覆盖模块逐行、括号规则、单块函数）；测试夹具抽到 `unittests/core/FirExamples.h`。
* S4b 类型布局与模块骨架（SPEC-0007）：`lower/TypeLayout`（规范类型 → FIR、记录按目标 ABI 布局、typedef 反查记录名、位域）、
  `lower/Lowering`（目标、文件表、全局、有函数体的函数、局部与 `obj`、CFG 块与边——与 `debug.DumpCFG` 一致，先不带条件；`body_pending`）；
  `Dump=fir` 接上（stderr + `WorkDir/dump/<tu>/fir/<tu>.{txt,json}`，读回自检）。lit 11 个全过（新增 `fir-layout.c`、`fir-module.c`）。
  67 个用例 / 70 个 TU / 502 个函数降级无崩溃、自检无错误。顺带修了两处：`extern int a[];` 后的暂定定义 `int a[10];` 被当成声明
  （`getActingDefinition()` 在 extern 那条上取不到，`lower/Keys` 与降级共用新的 `VarDefinition`）；`cc1-run.sh`、`run.py csa`
  对 ARM / AArch64 自动加 `-fno-signed-char`（直接跑 cc1 时 `char` 默认有符号，与公司 aarch64_be 不一致）。

## 下一步（小步，每步一个提交；做完一步勾掉一步）

- [x] **S1 回归尺子**：`oobsum/cases/run.py csa`——用 `cmake-build-release/bin/clang -cc1 -analyze` 跑 67 个用例（含跨 TU 的多文件用例），
      `WorkDir` 收判定日志，按用例头 `@expect`（TP/TN + pa=L1/L2/记账）对照，输出通过表。引擎没接上时全部"未判"——这正是基线。
      SPEC-0002 写清 `@expect` 与标签的对应（L1→DEFINITE、L2→POSSIBLE、记账→SUSPECT 或日志、TN→不报或 SAFE）。
- [x] **S2 函数键**（06- D-01…D-08）：`lower/Keys`；`Dump=keys` 或并入 `Dump=fir`；lit 覆盖 static / extern / 头文件 static inline。
- [x] **S3a `core/` 单元测试框架**（06- §9.3）：`oobsum-core-tests`；补上 S2 的 `core/Key` 单元测试。
- [x] **S3b FIR 结构与 JSON**（D-10…D-14）：`core/Fir`、`core/FirJson`（读写往返、坏输入报错不崩，A-96）；`FirTest`、`FirJsonTest`。
- [x] **S4a FIR 文本记法**（SPEC-0006 §2）：给 `Dump=fir` 与 T-02 快照用。
- [x] **S4b 类型布局与模块骨架**（`lower/TypeLayout`、`lower/Lowering`）：类型 / 记录布局（ILP32 vs LP64）、全局、函数签名与局部（`obj`）、
      CFG 块与边（先不带 assume——无条件边是安全的过近似）；接上 `Dump=fir`（stderr + `WorkDir/dump/<tu>/fir/<tu>.{txt,json}`，读回自检）。
- [x] **S4c-1 直线代码指令**（SPEC-0008 §2.1–§2.7）：常量折叠、转换、算术（回绕形）、比较、指针算术、下标（`sub`）、成员（`fld` / 柔性不收窄 / 位域字节数）、
      `*p` / `&x`、赋值、标量初始化、`return`、调用（直接 / 间接 / builtin / `__builtin_expect`）、结构体按值读的 `load`、内联汇编。lit 13 个全过；
      67 个用例 502 个函数降级无错误，`unk` 只剩 `pending:*`（S4c-2）与 `cond_value`（S5）。06- §5.2 的例子逐字复现。
- [x] **S4c-2**（SPEC-0008 §2.8）：`++` / `--`（内存先 load 再 store）、复合赋值（按计算类型）、结构体拷贝 `__oobsum_copy`、初始化列表逐元素；
      去掉 `body_pending`；修正 S4c-1 的赋值表达式取值（`y = (x = x + 1)`）。lit 14 个；67 个用例只剩 `unk(cond_value)`（S5）。
- [x] **S5 控制流**（SPEC-0009）：条件边 `assume`（后继 0 为真，与 `debug.DumpCFG` 逐个对照）、`switch`（`case` / `default` / 区间）、`?:` / `&&` / `||`
      当值用（汇合块前驱补 `set`）。lit 15 个；67 个用例 1451 条边里 260 条带条件，`unk` / `havoc` 一处都不剩。
- [x] **负偏移（下溢）**（SPEC-0039）：规格（06- §3.1）有、实现没有——`p[i - 1]`（`i = 0`）、`p - 4` 原来都被判成 SAFE（**错误的 SAFE**，探针发现）。
      偏移下界不能证明 ≥ 0 时记一条反向 Req（`dir: back`，安全条件 `−lo ≤ bext(基址)`），复用整套代换 / 判定 / 记账；调用点指针实参带 `back`；
      C++ 与 replay.py 同步、文本单写（"N bytes before …"）。67 个用例与金标准都没有负偏移：`run.py csa` 与基线完全相同，replay / engine_check 不变。
- [x] **头文件函数的键加 FIR 哈希**（SPEC-0059，D-02 后半）：各 TU 宏展开不同的 `static inline` 原来共用一个键，solve 只留一份——探针里真越界被判成
      **SAFE（漏报）**，或者反过来误报，取决于 FIR 文件顺序。现在键是 `F@<头文件 id>:<名字>#<规范文本的 xxHash64 前 8 位>`（类型按结构、位置按文件名、
      被调的头文件函数按最终键）；函数体相同的仍共用一个键（只算一次）。`run.py csa` 与基线相同。头文件 `static` 全局因宏不同而大小不同：探针验证过不是问题（各 TU 用自己的完整类型）。
- [x] **S9.1 局部函数指针**（SPEC-0058）：只被赋过函数地址常量的局部函数指针，一个目标当直接调用；多个目标每个目标一条 `alt` 调用点记录，判定时末端下界当 0
      （只判 POSSIBLE，不会把"只在某个目标里越界"报成 DEFINITE）。**LV9-01 从漏报变成通过，`run.py csa` 漏报 0**：通过 106、通过+S 22、级别不同 3、超出预期 3。
      量尺：`fptr` 记账因解析出被调者而消失不算回退（调用点记录带 `ind`，SPEC-0002 §2.4 修订）。
- [x] **间接调用记账 `fptr`**（SPEC-0057）：间接调用原来既不记调用点也不记账（"留到 S9"）——经函数指针交出去的指针路径会悄悄断掉；现在实参有指针时调用点记 `fptr`。
      LV9-02、LV9-03 从漏报变成通过，漏报 3 → 1（只剩 LV9-01，要 S9.1 解析函数指针常量）。
- [x] **无符号区间下界盖住 0 时可信**（SPEC-0056）：`i <= n` 的循环原来把上界多估 2（`n = 15` 的 16 字节数组也报 POSSIBLE，**误报**）；现在循环体 `[0, n]`。
- [x] **`run.py csa` 多文件用例走三阶段**（SPEC-0055，只改测量）：LV1-07（年内承诺）、LV6-01 从"需跨TU"变成通过。
      **年内承诺 32 个用例的入口全部通过**（56 通过、8 通过+S，06- §1.2 的口径）；全部 134 个入口：通过 103、通过+S 22、级别不同 3、漏报 3（函数指针）、超出预期 3。
- [x] **返回值范围 `ret_int`**（SPEC-0054）：取值 / 截断函数（`return (n > 8) ? 8 : n;`）原来不传返回值范围——**LV4-05、LV5-06（年内承诺）和 LV5-03（尽力）
      从"级别不同"变成通过**。级别不同 6 → 3（剩 LV3-07、LV5-05、LV8-01，都是年内尽力）。
- [x] **出参式分配器 `out_new`**（SPEC-0053）：`Buf_Create(&p, len)` 把新对象写进 `*pp`，原来不传给调用方——效果 `out_new`（每条返回路径都写了才记），
      调用点强更新实参所指的格，能传递。**LV4-04（年内承诺）从"级别不同"变成通过**。级别不同 7 → 6。
- [x] **`ret_new` 允许常数偏移**（SPEC-0052）：带管理头的分配器（`return pucRaw + 16;`）原来不算分配器效果——**LV4-03（年内承诺）从"级别不同"变成通过**
      （TP DEFINITE、TN SAFE）。级别不同 8 → 7。
- [x] **结构体整体赋值的内置模型 `__oobsum_copy`**（SPEC-0051）：06- A-06 规定了、默认模型表里漏了——所有 `a = b` 结构体赋值原来只记 `not_in_tu`、
      目的一侧的写漏掉。**LV3-04（年内承诺）从"级别不同"变成通过**。级别不同 9 → 8。
- [x] **从 1 开始的无符号循环**（SPEC-0050）：`for (u = 1; u <= 16; u++)` 原来循环头是 Top（`[1, +inf]` 被认为可能回绕、收不住，`u + 1` 掉下界）；
      widen 后补上类型范围的界，收窄后没收住的去掉——循环头 `[1, 17]`，`p[u - 1]` SAFE、`p[u]` POSSIBLE。`run.py csa` 与基线完全相同。
- [x] **调用点消去局部时进入不透明项**（SPEC-0049）：`4*div(ulUe, 32) + 4` 里的调用方局部原来没换成区间 → 记账；现在 `div` 单调代入、`mod` / `wrap` 在区间内
      等同内层——**LV2-06（年内承诺）从"级别不同"变成通过**（TP L2、TN SAFE）。C++ 与 replay.py 同步。级别不同 10 → 9。
- [x] **`run.py csa` 汇合点口径**（SPEC-0048，只改测量）：TP 入口的 L1 / L2 可以判在它调用链上的汇合点——LV1-04（年内承诺）"级别不同" → "通过"。
      剩下"级别不同" 10 个是真问题（调用链上只有 SUSPECT），其中年内承诺 6 个：LV2-06、LV3-04、LV4-03、LV4-04、LV4-05、LV5-06（下一步逐个查）。
- [x] **`run.py csa` 记账口径**（SPEC-0047，只改测量）：期望"记账"的 TP 入口，也看经静态调用能到达的函数里报出的 SUSPECT——"漏报" 13 → 3，
      剩下的 LV9-01 / 02 / 03 都是函数指针（S9.1）。通过 78 → 88。
- [x] **循环头只 widen 本循环里被赋值的局部**（SPEC-0046）：嵌套循环的外层下标在内层循环头原来被 widen 成 `[0, +inf]`，访问只剩类型范围——
      **LV3-09（年内承诺）从漏报变成通过**（TP L2、TN SAFE）。`run.py csa` 通过 77 → 78、漏报 14 → 13，回退 0；基线已存。
- [x] **mod-ref**（SPEC-0045）：摘要加 `mods`（本函数的写 + 被调者按实参换算过来的；没摘要的被调者按整块 + `g:*`；没分析完的函数 `*`），调用点只让
      写到的位置失效——"被调者只写了 ulType / 别的全局 / 数据区，之后读 ulLen / g_ulLen"从 SUSPECT 变回 DEFINITE / POSSIBLE；写到了的（含经两层）照旧失效。
      `run.py csa` 与基线完全相同；上游用例 0 崩溃。
- [x] **同一调用点同一次访问只报一条**（SPEC-0044）：成员数组越界时成员界与外层界各判一条 DEFINITE（其中一条对象名是 `'?'`），现在只报主判定
      （DEFINITE 优先、对象名写得出来的优先），另一条 `dup_of` 写日志。结构体数组、元素成员、联合体各一条告警。
- [x] **符号形的钳位**（SPEC-0043）：`if (ulLen > ulDstLen) ulLen = ulDstLen;` 后 `memcpy_s(dst, ulDstLen, src, ulLen)` 合并成 `[0, p1]`（原来 Top → SUSPECT），
      调用方按实参判；两个候选都成立时取钳位的目标、不取变量自己的原值。`n - 1`（无符号回绕）的钳位仍证明不了（正确）。`run.py csa` 与基线完全相同。
- [x] **汇合时按各自路上的守卫求常数界**（SPEC-0042）：`if (i > 15) i = 15;`、`i = (i > 16) ? 16 : i;`、`if (len > 8) len = 8;` 这类钳位原来合并成 Top、
      报 SUSPECT（误报）；现在合并成 `[0, 15]:check` 等，证明 SAFE 或报 POSSIBLE。符号形的钳位（钳到形参 `ulMax`）以后。`run.py csa` 与基线完全相同。
- [x] **全局标量的当前值**（SPEC-0041）：调用过可能写全局的函数（不在模型表里的被调者）、经未知指针写过、地址交给过被调者之后，读全局标量是 `data`，
      不再给入口符号 `g:K`——修掉"调用方检查过 `g_ulIdx < 16`、被调者先 `Idx_Inc()` 再用"的错误 SAFE。纯函数 / 访问类模型之后、调用之前读的照旧。
      `run.py csa` 与基线完全相同。
- [x] **形参所指字段的当前值与别名**（SPEC-0040）：读 `pK->f` 时，进函数后改过（被调者拿到过指针、偏移不定的写、同类型结构体形参的写）就读成 `data`，
      不再给入口符号——修掉三种错误的 SAFE（被调者改字段后再读、`((UINT32 *)p)[i] = …` 后再读、`Alias_Fill(&st, &st)`）。对照（调用前读的、只读的、
      不同类型的形参）照旧。`run.py csa` 与基线完全相同。取舍：`char` 缓冲区的写不当成会改结构体形参；形参与全局的别名以后。
- [ ] 二维数组行内越界（`a[0][5]` 在 `UINT8 a[2][4]` 里）、结构体数组元素里的成员数组越界但仍在下一个元素里（`astEnt[0].aucName[8]`）：
      嵌套子对象的界没查，判 SAFE（06- 已列为 P1 #18）。
- [x] **调用点记账原因**（SPEC-0038）：上界只来自 `data`（被调者改过的字段、全局）时记 `type_range_only`（原来误记 `pointer_stepping`）；
      lit `callee-writes.test` 守住"被调者改了字段 / 全局后不再用旧值证明安全"（做 mod-ref 之前的保守做法）。
- [x] **S5b-1 `MaxBlocks`**（SPEC-0037）：CFG 块数超过 `MaxBlocks`（默认 5000）的函数不降级函数体（`cfg_too_large`），建不出 CFG 的（`no_cfg`，原来悄悄当成空函数）同样
      `skipped:…`；`partial:*` / `skipped:*` 的函数都在声明处报 SUSPECT `unanalyzed:<后缀>`。
- [x] **递归调用点记账 `recursion`**（SPEC-0066）：环上不求不动点，经递归传上来的条件会丢——探针里互相递归（下标涨到 20）、带深度参数的自递归（写到第 32 字节）
      **都判成了 SAFE（错误）**。现在调用图的强连通分量内每个调用点记一条 SUSPECT `recursion`；第一层能判的照旧（`Rec(a, 16)` 仍 DEFINITE）。`run.py csa` 回退 0。
- [x] **libc `strncpy` / `snprintf` / `vsnprintf` 模型**（SPEC-0065）：原来只记 `not_in_tu`；现在 `strncpy` 的 `n` 超界判 DEFINITE、`snprintf` 判 POSSIBLE，界内判 SAFE。
      `strcpy` / `strcat` / `sprintf` 取决于源串长度，照旧记账。
- [x] **调用前改过的全局**（SPEC-0064）：被调者 Req 里的 `g:K` 原来代成调用方**入口**的值——探针里先 `g_ulLen = 100`（或经 `SetLen(100)`）再调 `Fill(a)`，
      被入口守卫 `g_ulLen <= 16` **判成 SAFE（错误）**。现在调用点记录带 `gvals`（确切值 / `?` / `g:*`），代换按它；不知道时记 `global_value`。`run.py csa` 与基线相同。
- [x] **代换里没映射的被调者符号**（SPEC-0063）：原来原样保留、被当成调用方的同名形参符号继续上传——探针里经外部函数拿到的 `st.pucRes` 被套上上层
      `outer.pucRes` 的 128 字节，**判成 SAFE（错误）**。现在改名成非接口符号 `_unmapped:…`，对象大小里有它时记 `untracked_pointer`。
      `run.py csa` 回退 0（LV5-02、LV10-05 的同一条 SUSPECT 从入口声明处挪到调用点）。
- [x] **不跟踪的指针实参**（SPEC-0062）：外部函数返回的指针交给下一层时，原来记 `entry_unproven`，告警文本露出内部占位符（"stays within '_unknown0' (size _unknown0)"）；
      现在记 `untracked_pointer`，文本 "the object passed as argument 1 (size unknown)"。只换原因与文本（LV4-07 两个入口仍是 SUSPECT）。
- [x] **出参式分配器写进结构体字段**（SPEC-0061）：`out_new` 的 `target` 可以是 `pK->字段`（`pstUe->pucRes = alloc(64)`）。**LV5-05 从"级别不同"变成通过**
      （TP DEFINITE、TN SAFE）；`run.py csa`：通过 108、通过+S 21、级别不同 2（LV3-07、LV8-01：A-41，需要关系域）、超出预期 3。
- [x] **S5b-2**（SPEC-0060）：访问因不支持的构造（`unk(<类名>)`、`cast:*` 等）进账本时原因记 `unsupported_construct`（内联汇编 `inline_asm`、结构体按值
      `struct_by_value`），原来笼统记 `untracked_pointer` / `type_range_only`。只换原因，记不记、报不报不变；`run.py csa` 与基线相同（用例里没有不支持的构造）。
- [x] **S6a 线性式**（SPEC-0010）：`core/LinExpr`（规范形式、D-21 解析与打印、不透明项的常数求值、溢出 → 未知、`ParseCond` / `ShowLe0`、`Substitute`）；
      单测 39 个，含金标准 `unittests/core/data/lin-golden.txt`（`gen_lin_golden.py` 用 `replay.py` 从 104 份摘要抽出的 101 条表达式 / 条件，逐字一致）。
- [x] **S6b 取值范围与蕴含**（SPEC-0011）：`core/Range`（来源标签与优先级、类型表、`SymEnv` 的单符号守卫收窄、六种原子的界、`RangeOf`、`EntailsLe0`）；
      单测 44 个，含金标准 `range-golden.txt`（`gen_range_golden.py` 用 `replay.py` 的 `Env` / `entails_le0` 算出的 54 条查询，逐条一致）。
      **顺序调整**（SPEC-0011 §1）：带 `min` / `max` 的符号界、合并、widening（A-10、A-15…A-17）移到 S7；先做 S8a。
- [x] **S8a-1 摘要读写**（SPEC-0012 §2.1）：`core/Summary`（schema 的全部机器字段；读入忽略 `doc`、其余严格校验带位置；写出）；
      104 份金标准摘要读入再写出与去掉 `doc` 的原文完全相同；单测 48 个。
- [x] **S8a-2 调用点判定**（SPEC-0012 §2.2–§2.4）：`core/Apply`（`BuildMap` / `SubstObj` / `EliminateLocals` / `Classify` / `RepOf` / `ApplyEff` /
      `CheckCase`）；16 个用例逐调用点核对 0 处不一致，输出与 `replay.py`（机器版）逐行相同（金标准 `apply-golden.txt`，244 行；反向改坏一行会失败，已验证）。
      单测 50 个（T-06 的前一半）。
- [x] **S8b 推到底**（SPEC-0013）：`core/Solver::Propagate` + `core/Alert`；16 个用例推出的 Req / 判定 / 入口账本与 JSON 一致，统计上传 58、
      判定 13、入口账本 8（同 `replay.py`），13 条告警与 `alerts.json` 逐条相同（反向改坏一处即失败，已验证）。单测 54 个。**T-06 的两半都在 C++ 里成立了。**
- [x] **S7a-1 函数内解释第一步**（SPEC-0014）：`core/Interp`——值（确切线性式 / 区间 / 未知）、指针（形参、形参字段与数组成员、全局、局部、字符串）、
      内存格、无环 CFG 的逆后序与汇合、访问检查 → depth 0 的 Req（写优先的覆盖合并）；有回边的函数先标 `partial:loop_pending`。单测 62 个。
- [x] **S7a-2 调用点记录与端到端对照**（SPEC-0015）：`RecordCall`（确切 / 区间 / 未知的整数实参，指针 `{obj, have, alias}`，纯函数模型的指针为 null，
      未知写成非接口符号）、`BuildSummary`、`Dump=summary` / `Dump=state`；`oobsum/tools/engine_check.py`（键、类型规范化后交 `replay.py` 推到底）。
      **LV1-01、LV1-02、LV1-07（三个 TU）从 C 源码端到端与 `alerts.json` 逐条相同，`@expect` 全对**（lit `engine-check.test` 守住）。
      LV2-09：TP 对，TN 误报 L1——缺守卫（`if (ulPos == 0) return`），S7b 解决。
- [x] **S7b 条件边与守卫**（SPEC-0016）：`assume` 边 → 原子（`!=` 只在界正好等于常数时推出严格不等式）、区间局部收窄（标签跟上界走）、常数为假 / 区间为空 → 边不可达；
      只用"忠实"的比较（两边常数或能证明在 C 类型范围内——`if (off + len > max)` 可能被回绕绕过，不当守卫）；守卫让无符号减法不回绕；
      Req 带相关守卫、守卫不同不合并、调用点带 `guards`、汇合取交集；**A-43 / A-44 当场证明安全的访问不生成 Req**（记进 `FunctionFacts::safe`，S8c 写判定日志）。
      `engine_check.py` 加严：告警相同之外，上传 / 判定 / 入口账本条数、每个函数的本地账本原因要与金标准相同，且没有没分析完的函数。
      **16 个用例里 5 个一致**（LV1-01、LV1-02、LV1-07、LV2-09、LV3-03，lit 守住）。其余 11 个缺的东西：
      循环（S7c，LV2-05、LV7-04 已对上）；效果与模型（S7d）LV2-03、LV3-07、LV5-01、LV8-01、LV10-04（`memcpy_s`）、LV4-01（`ret_new`）、
      LV4-06（校验函数的条件效应）、LV9-04（`unknown_callee` 账本）；LV6-01（`extern` 不完整数组 → `ext(g:名)` 符号 + 跨 TU 全局大小表；现在记账 `alloc_size_unknown`，召回不丢但金标准要符号）。
- [x] **S7c-1 循环**（SPEC-0017）：工作表不动点（逆后序、后继 0 在前）、循环头 widening、下降 2 遍、最后一遍才记录；循环头条件收窄的上界标 `loop`；
      区间与确切值的比较也推出原子（`i < X`、`i ∈ [0, +∞)` → `X >= 1`），已成立的原子不加；忠实判断放宽到"盖住整个类型范围的区间"、
      `div` / `mod` 内层要忠实、无符号单调表达式可当"大的一边"（`(n + 31) / 32`）；无符号区间运算回绕感知；`*p++` → `pointer_stepping`；账本按（原因, 位置）去重。
      **LV2-05、LV7-04 对上，16 个用例 7 个一致**；506 个函数全部 `analyzed`。
- [ ] **S7c-2**（以后按需要）：存进局部的"可当上界"的值（来源标记）、带 `min` / `max` 的界（A-10、A-15、A-17）、`do { p[i] … } while (i < n)`（先访问后检查，
      头上的不变式是 `i ≤ max(n − 1, 0)`，线性界表示不了）。
      已做：递减循环 `while (n--)`（SPEC-0075）、有符号 `for (i = n − 1; i >= 0; i--)`（SPEC-0076）、`!=` 结束的符号阈值（SPEC-0082 / 0085）、同余（SPEC-0084）、
      指针比较（SPEC-0081 / 0083）。
- [x] **S7d-1 模型表第一步**（SPEC-0018）：`core/Models`（内置默认表：`memcpy_s` / `memmove_s` / `memset_s` / libc 的访问条件、分配器、纯函数名字与正则、释放函数；
      模型表优先）；调用点按 F-03 / F-04 / F-05 生成访问（securec 的 destMax 规则：0 或过大不写、回绕的减法 → 守卫 `E >= 0`；源只在 `count <= destMax` 时读）；
      `Access` 的字节数可以是区间 / 符号；守卫挑选把 `start` 也算进去；分配器的返回值是 `heap@<调用点>`；函数指针实参写 null；纯函数、释放函数不清内存格。
      **LV2-03 对上，16 个用例 8 个一致**。
- [x] **F-02 的字段访问照生成、入口记 `typed_field`**（SPEC-0019；用户 2026-10-16 定"按你的决定"，即选项 (c)）：摘要形参加 `size`（所指类型字节数）；
      入口上对象是自己形参所指、末端是常数且不超过声明类型的 → `typed_field`（`SuspectReasons` 可整类过滤）；`replay.py`、`core/Solver`、schema 同步。
      金标准有意补齐 F-01 / F-02 漏写的访问（LV10-04、LV5-01、LV3-07），上层用新工具 `oobsum/tools/golden_regen.py` 重算：统计 58 / 13 / 8 → **66 / 13 / 12**，
      13 条告警不变。LV10-04 的条数对上（只差 `unknown_callee`）。06- 里 SPEC-0014…0019 修订过的条目补上"由 SPEC-NNNN 修订"。
- [x] **F-03 destMax 第二段**（SPEC-0020；用户 2026-10-16 定"按你的决定"）：`destMax = wrapW(E)` 分两段——`E >= 0`（destMax = E）与 `E <= 0x7fffffff − 2^W`
      （destMax = E + 2^W，securec 照样写）；模型给出的守卫总挂在这次访问的 Req 上（被其余守卫蕴含的不挂）。金标准 LV2-03 `Tlv_WriteVal_OK` 加两条、上层重算：
      统计 **70 / 13 / 12**，告警不变；LV2-03 仍一致。
- [x] **修：经形参访问成员数组只查成员**（SPEC-0022）：外层对象（`*pK`、`ext(pK)`）的边界也查——否则"按结构体访问更短的缓冲区"（LV7-03）会被证明成安全。
      由引擎接进 checker 时 `run.py csa` 的对照发现。
- [x] **S7d-2a `ModelFile`**（SPEC-0033）：YAML 模型表合并进内置表（同名替换、`defaults: false` 清空）；坏条目跳过并提示一行，读不了 / YAML 错是选项错误；
      `Dump=models` 列出实际生效的表；示例 `oobsum/config/oob_models.example.yaml`（= 内置表）。
- [x] **入口账本按访问去重**（SPEC-0036）：同一入口同一次访问（同读写）的账本，第一条报出了其余只写日志（`dup_of`、`duplicate_access`）；
      指向的那条被 `SuspectReasons` 滤掉时照常报。`Policy::ClassifyBatch`（Reporter 用）。LV8-05、LV10-04、LV10-05 的入口各少一条重复 SUSPECT；基线已存。
- [x] **成员数组当实参带外层对象**（SPEC-0035）：调用点记录的指针实参加 `parent`；代换时成员证明了安全还要按外层对象查、成员要上传时外层也上传
      （修掉"64 字节缓冲区当成结构体、经一层调用写成员"的错误 SAFE）。C++（`apply::ClassifyAll`）与 replay.py（`classify_all`）同步；
      金标准 LV10-04 补 `parent` 后用 golden_regen 重算（上传 70 → 72、入口账本 12 → 14）；基线已存（3 个入口多一条外层的 `typed_field`）。
- [x] **S7d-2b `strcpy_s` 族**（SPEC-0034）：8 个 securec 字符串函数的目的一侧进内置表——访问条件 `upto`（`bytes` 只是上界，实际 1 … destMax 字节），
      新来源标签 `model`（代码给出的界）：destMax 超出 → POSSIBLE、偏移已越界 → DEFINITE、证明了 → SAFE。LV7-05 TP 从未判变成 POSSIBLE、TN 证明 SAFE。
      `run.py` 回退规则加一条：访问在调用链某层证明了 SAFE 且以它为终点的判定都是 SAFE 或报出了（SPEC-0002 §2.4 修订）；基线已存。
- [x] **S7d-3 `ret_new` 效果**（SPEC-0024）：每条返回路径都是同一个堆对象或空指针 → `ret_new`（`eff_depth` 逐层加 1）；调用方拿到 `heap@<调用点>`、
      实参写 `eff_depth`；`RunInline` 按 FIR 调用图自底向上解释，`Dump=summary` / `state` 取同一份结果。**LV4-01 端到端一致（16 个金标准用例 9 个）**；
      `run.py csa`：LV4-01 TP POSSIBLE、LV4-02 TP DEFINITE、LV7-01（柔性数组）TP DEFINITE / TN SAFE——**通过 72、漏报 14**，回退 0（尺子：账本判定同一处有 SAFE 就算证明）。
- [ ] **S7d-4**：偏移与长度的抵消（A-41，LV3-07、LV8-01）。条件效应（LV4-06，SPEC-0025）、局部结构体字段带进调用点（LV5-01，SPEC-0026 / 0069）已做。
      A-41 的难点（2026-10-17 查过）：securec 的 destMax 回绕到 `(0, 0x7fffffff]` 时照样写（SPEC-0020 段 B），widening 后的 `ulOff` 排除不了段 B；
      要先有跨轮不变式 `ulOff <= ulPktLen`（关系域）。
- [x] **S7e 全局大小表**（SPEC-0032）：`extern` 不完整数组在 solve 时取定义所在 TU 的大小（D-34）；solve 时"哪里都没有定义"的被调者也写进调用方的本地账本；
      `Phase=solve` 打 `Dump=summary`；`engine_check.py` 改为把用例的文件走 export + solve（金标准按"用例的文件就是整个程序"写）。
      **金标准 16 个用例 14 个端到端一致**（只剩 LV3-07、LV8-01：偏移与长度的抵消 A-41）。LV1-07、LV6-01 的"需跨TU"入口在三阶段下报出 DEFINITE（`run.py csa` 仍按 inline 对照）。
- [x] **S8c-1 引擎接进 checker（`Phase=inline`）**（SPEC-0021）：`core/Engine::RunInline`——每个函数解释 → 摘要 → `Propagate` → 带标签的判定：
      调用点 L1 / L2（DEFINITE / POSSIBLE，F-32 文本、`rep.fp`）、调用点判安全 / 记账（SAFE `guard_false` / `within` / `guard_entailed` / `guard_bounds`，
      SUSPECT）、入口账本（`typed_field` / `type_range_only` / `entry_unproven`）、本函数当场证明安全（SAFE `local_entailed`）、本函数账本、没分析完（`unanalyzed:*`）、
      指针流进本 TU 外的被调者（`not_in_tu`）；位置里的文件名按 TU 的文件表换回完整路径；`Dump=apply`；调试内建函数按纯函数。
      **`run.py csa`（67 个用例 134 个入口）：通过 68、通过+S 32、级别不同 16、漏报 15、超出预期 1、需跨TU 2；TN 上误报（DEFINITE / POSSIBLE）0 条；回退 0**
      （原来 通过 69、漏报 63）。基线有意更新。查过 TP 上的每条 SAFE：没有把越界证明成安全的（LV7-03 的那条已由 SPEC-0022 修掉）。
      剩下 15 条漏报大多是"在被调者里报了 SUSPECT、但不在入口上"（`run.py` 只按入口对照）；真正一条都没报的用例：无。
- [x] **汇合、收窄时比较符号界**（SPEC-0023）：`max(0, p1) = p1`、出口 `i >= ulLen` → `i == ulLen`；区间的界在忠实判断里只查类型范围。
      LV3-05 TP 从"只在被调者里 SUSPECT"变成调用点 DEFINITE：**通过 69、漏报 14**，回退 0（尺子认"改报在汇合点"，SPEC-0002 §2.4 修订）。
- [x] **`ret_cond` 校验函数的条件效应**（SPEC-0025）：值带"情形"（取各常数时成立的守卫，汇合时产生，至多 4 个）；全是常数的返回值 → `ret_cond` + `ret_int`；
      调用方代入（形参换成实参）、条件边按情形筛选（一个不剩 → 不可达）。**LV4-06 端到端一致（金标准 10 个）**；`run.py csa`：LV4-06 TP POSSIBLE、TN SAFE——
      **通过 74、漏报 14**，回退 0。尺子按"访问"认召回（判定换了位置不算回退，SPEC-0002 §2.4 修订，单独提交）。
- [x] **局部结构体的字段值带进调用点**（SPEC-0026）：实参 `fields`；常数偏移的写只失效重叠的内存格；**修了别名实参的旧值问题**——形参所指结构体的字段
      在本函数里改过（或指针交给过被调者）时，别名代换会用入口时的旧值，链顶代入字段值后能把越界证明成安全（LV5-04 TP 一度变成 SAFE，已修、单测守住）。
      **LV5-01 端到端一致（金标准 11 个）**；`run.py csa`：LV5-01、LV5-04 TP → DEFINITE——**通过 78、漏报 14**，回退 0。
- [x] **内层界：带窗口的实参**（SPEC-0104 第 3b 步）：调用点记实参的内层剩余 `sub`；外层 safe / up 时按内层再判，越出 → SUSPECT `subobject_overflow`、
      上传带 `dir: sub`。`Fill(st.aucBuf, 20)`、`Fill(a[0], 5)`、经中间函数都报；外层已报的不加。C++ 与 `replay.py` 一起改。
      **D22 的三步做完**；没覆盖的：被调者里的负偏移越到成员前面（仍只查外层）、结构体字段里存的指针不记 `sub`。
- [x] **内层界：下标是形参**（SPEC-0104 第 3a 步）：证明不了且含接口符号 → 内层 Req（`dir: sub`）往调用方传，L1 / L2 改判 SUSPECT `subobject_overflow`；
      LV7-02 的 SUSPECT 落到 TP 链（`Sched_Alloc`）、TN 链证明安全不报；上游用例 43 → 30 条（少掉的都按 A-44 并进覆盖它的那条）。
- [x] **内层界：函数内检查**（SPEC-0104 第 2 步，D22）：指针值带至多 4 个内层窗口，访问证明不了落在窗口里 → SUSPECT `subobject_overflow`；
      上游用例 43 条（真越出成员 / 行的，与下标是形参、本地证明不了的）；compiler-rt 不变；`run.py csa` 回退 0（LV7-02 被调者里多一条）。
- [x] **FIR `subobj(指针, 大小)`**（SPEC-0104 第 1 步，D22）：成员数组、多维数组的一行退化成指针时记下子对象大小（整个数组、柔性数组不包）；Interp 暂当恒等，结果不变。
- [x] **选项 `CharAlias`**（SPEC-0103，D20）：char / void 的写当成可能改了任何外部内存；实测循环界丢得多，默认先关，等用户确认（见"等用户提供"）。
- [x] **指针字段判空的守卫**（SPEC-0101）：`if (pst->pucBuf != NULL)` 推出 `nn:p0->pucBuf >= 1`，调用方把字段置 `NULL` → `guard_false`（原来 SUSPECT）；
      经中间函数按别名改写。C++ 与 `replay.py` 一起改。健壮性复查（SPEC-0094…0100 之后）：上游 234 个用例 0 崩溃 0 超时；`perf-chain` 300 层 0.3 秒、加 2000 全局 1 秒。
- [x] **`replay.py` 同步**（SPEC-0100）：SPEC-0095 / 0099 只改了 C++ 的 Apply，`replay.py`（参考实现，`engine_check` 用它推到底）补上 `off_*` 与 `nn:p<k>`。
      **注意**：以后改 `apply::Classify` / `BuildMap` 时同一个提交里改 `replay.py`。
- [x] **指针判空的守卫**（SPEC-0099）：新接口符号 `nn:p<k>`（形参不是空指针）。`if (pOut != NULL) memset_s(pOut, …)` 的调用方传 `NULL` 原来报 SUSPECT
      `untracked_pointer`，判断的是另一个指针时报 DEFINITE；现在 → `guard_false`。调用点：0 → 0，局部 / 全局 / 字符串地址 → 1，调用方形参 → 它的 `nn:p<j>`，堆等不换。
      compiler-rt：`__udivmodti4(a, b, 0)` 这类可选余数出参的 3 处 → `guard_false`（核对过）。仍不行：指针字段判空、堆对象判空后传下去。
- [x] **区间非空的守卫**（SPEC-0098）：`while (n--) p[n + 10]`、`for (i = n; i != 0; i--)` 传 0 原来报 POSSIBLE（`!=` 收窄不推守卫）；
      收窄出的区间 `[lo, hi]` 非空就记 `lo − hi ≤ 0`（整数约简），调用点代 0 → `guard_false`。
- [x] **标志位判断产生守卫**（SPEC-0097，修 **DEFINITE 误报**）：`if (f & 0x4)` 原来推不出守卫（`f & 4` 只是区间），传不含该位的调用方也报 DEFINITE；
      单个位的掩码写成 `4·mod(div(f, 4), 2)`，`!=` / `==` 推出守卫，调用点代常数后为假 → `guard_false`。多位掩码（`f & 0x6`）不做。
- [x] **Req 带上全部守卫**（SPEC-0096，修 **DEFINITE 误报**）：`if (c) p[i] = 0;` 的调用方传 `c = 0` 原来也报 DEFINITE（只关于别的形参的路径条件被丢掉）；
      现在全部守卫都带上，调用点代成常数假 → SAFE `guard_false`。compiler-rt：原来报出的 8 处变成 `guard_false`，逐条核对确实走不到；判定 1564 → 1628 条（多的主要是 SAFE）。
      仍不行：指针判空（`if (q != NULL)`）没有守卫。
- [x] **`ret_ptr` 多条返回偏移不同**（SPEC-0094）：取子缓冲区的函数分支返回 `m->buf + 4` / `m->buf + 8` 时记 `range [4, 8]`，调用方按区间判（原来 SUSPECT
      `untracked_pointer`）。先做了 SPEC-0095，否则区间交给下一层会按最小剩余判成 DEFINITE。
- [x] **指针实参偏移是区间时的级别**（SPEC-0095，修 **DEFINITE 误判**）：调用点按最小剩余记 `have`，拿它判 L1 就判过了头——`p = c ? a + 2 : a + 6; Put(p, 12)`
      判成 DEFINITE（`c` 为真时界内）；下标只有类型范围时剩余甚至是负数（"(-2147483347 bytes)"）。现在另记 `off_lo` / `off_hi` / `off_bound`，按对象起点 + 偏移区间判：
      POSSIBLE / SUSPECT，偏移区间都越界才 DEFINITE。判 SAFE 的条件不变，召回不变。
- [x] **setter 写效应传区间**（SPEC-0093）：`SetLen(&s, k < 3 ? 2 : 100)` 原来实参是区间就换不了，字段不知道（SUSPECT）；写效应的值正好是 `p<k>` 时直接写实参的值。
      错误 SAFE 排查第七批（写效应 + 别名 7 个，如被调者写两个形参、调用方传同一个结构体）没有错误 SAFE。
- [x] **经包装函数传自己的形参**（SPEC-0092）：`Mid(p, n) { Put(p, Id(n)); }` 原来 `Id(n)` 的结果因为"结果里有 p<k>"被当成没换掉、未知，链断在 `Mid`（SUSPECT）；
      现在只按 `_unmapped:` 判，2 层链判到底。正文里的 `_unknown<k>` 写成 "an unknown value"。
      已知局限：`Twice(x) { return Idx(Idx(x)); }`（`x + 1` 两层）第二次的实参可能回绕、不忠实，照旧不代换（确切值按数学值的约定，SPEC-0014）。
      错误 SAFE 排查：第四批（控制流 / 下标运算 13 个）、第五批（跨函数 7 个）都没有错误 SAFE。
- [x] **外部内存的别名**（SPEC-0091，修三处**错误 SAFE**）：形参就是全局数组（调用方传 `g_idx`）、形参就是另一形参的 `m->buf` 所指、`UINT8 *` 与 `UINT32 *`
      指向同一块——原来只让所指类型相同的结构体 / 宽整数形参互相失效（SPEC-0040）。现在形参所指、指针字段所指、全局按类型键互相失效，`char` / `void` 指针视角被任何写影响；
      `char` 写仍不影响别的类型（SPEC-0040 的取舍：先写输出缓冲区、再读输入结构体长度的编码函数照旧按入口值判）。
- [x] **成员数组与外层对象的别名**（SPEC-0090，修**错误 SAFE**）：形参结构体的数组成员单独当基址（`P0.idx`），经 `pc->idx[1]` 与经 `((UINT8 *)pc)[5]`
      写同一个字节时，另一种写法上的旧格还在。现在 store / 结构体拷贝写一边时失效另一边重叠的格。
- [x] **访问类模型只失效目的对象**（SPEC-0089）：原来 `memcpy_s` / `memset_s` / `strcpy_s` 等调用后清掉全部内存格，写 `tmp` 也让无关的 `m.len` 丢了；
      现在只失效 `W` 实参所指对象（连同形参结构体的成员数组 / 外层对象，同一块内存）。securec 调用之后其它对象的字段、全局按真实值判。
- [x] **结构体整体赋值按字段拷值**（SPEC-0088）：原来 `__oobsum_copy` 之后清掉全部内存格，`y = x` 后 `y.len` 不知道（SUSPECT）；现在先按源读出整数 / 指针字段
      （形参所指的读成 `p0->len`），只失效目标那一段再写入，其余对象的格不动。`*pstOut = st;` 也变成写效应。数组成员不展开；`memcpy` / `memcpy_s` 仍按访问模型清格。
      错误 SAFE 排查：两批 17 个探针（经指针 / 函数指针 / memcpy_s / 没定义的函数改全局、别名、负下标、联合体、宽写、堆、二维数组…）都没有错误 SAFE。
- [x] **入口函数自己的常数访问判级**（SPEC-0087）：`void T(void) { UINT8 a[4]; a[100] = 0; }` 原来记 SUSPECT `entry_unproven`（入口函数上推不下去的 Req 一律记账），
      现在深度 0、`have` 与末端都是常数时按 F-11 / F-12 判成 DEFINITE / POSSIBLE。**报出的变化**：默认 `InlineDepth` 下深度 0 的 DEFINITE / POSSIBLE 照 F-20
      交现有规则、只写判定日志（同非入口函数在调用点判出的那些），所以这类入口函数里的函数内常数越界不再由本规则报出；`InlineDepth=0` 照报。
      公司侧可选确认：现有规则在 bxen 下确实报函数内的常数越界（F-20 分工的前提）。
- [x] **全局符号的身份**（SPEC-0086，修三处**错误 SAFE**）：① 不同 TU 的同名 static 全局、② 同一 TU 两个函数的同名函数内 static 都叫 `g:<名字>`，
      调用方自己那个的值被代进被调者那个；③ 被调者改了本模块没声明的全局（`d.c` 调 `SetX(100)` 再调 `PutX`），后面调用点不知道它改过。
      现在内部链接的全局符号写成 `g:<名字>__S<键的哈希前 16 位>`（正文显示时去掉），`mods` 用同一个符号；查不到的记进 `writtenAll`，后面调用点 `gvals` 写 `?`。
- [x] **`!=` 阈值的候选扩到循环体**（SPEC-0085）：`while (1) { if (i == n) break; p[i] = 0; i++; }` 越界 POSSIBLE、界内 SAFE（原来记账）；
      `do { … } while (i != n)` 照旧记账（`n = 0` 时真的回绕）。仍不行：`do { p[i] … } while (i < n)`（先访问后检查）。
- [x] **区间带同余**（SPEC-0084）：值 ≡ 锚点 mod 步长，汇合 / widen / 加常数 / 乘常数时保持，条件收窄时上界取整到同余类（`4n − 1` → `4n − 4`，
      符号的用 `s·div(…, s)`）。结构体 / `UINT32 *` 指针循环、`i += 4` 循环、`!=` + 按元素走的循环都判准（越界 POSSIBLE、界内 SAFE）；
      结束指针按字节长度算（`(ENT_S *)(buf + len)`）时取整成 `16·div(len − 1, 16)`，真越界照样报。SPEC-0083 的已知局限解决。
- [x] **宽元素指针比较的 POSSIBLE 误报**（SPEC-0083）：SPEC-0081 按字节收窄 `UINT32 *p < e` 到 `4n − 1`，访问末端 `4n + 3`，安全代码被判 POSSIBLE；
      现在被收窄的指针指向宽于 1 字节的类型时上界标 `widen` → 超出时 SUSPECT `pointer_stepping`（文字给末端），没超出 SAFE。
      **已知局限（区间域本来就有）**：整数 `for (i = 0; i < n; i += 4) *(UINT32 *)(p + i)` 的 TN 报 POSSIBLE（区间里的值不一定都取得到）。
      下一步候选：同余信息（偏移 ≡ r mod 步长），能让宽元素指针循环、`i += 4` 都判准。
- [x] **`!=` 结束的循环**（SPEC-0082）：`for (i = 0; i != n; i++)`、`for (q = p; q != p + n; q++)` 原来上界被 widen 掉、`!=` 收不回来，只能记账；
      现在循环头 widening 用 `c` 作符号阈值（汇合两边都证明 `x ≤ c` 才用，下一圈证明不了就照常 widen），17 → POSSIBLE、16 → SAFE。
      还不行的：步长大于 1（`i += 2`、结构体数组指针 `pst != ast + N`，需要同余）、退出测试不在循环头上（`while (1) { if (i == n) break; }`）。
- [x] **同一对象的两个指针比较按偏移收窄**（SPEC-0081）：`e = p + n; while (p < e) { *p = 0; p++; }` 原来只能记 `pointer_stepping`（指针比较不产生信息），
      现在循环体里 `p` 的偏移是 `[0, p1 − 1]`，17 → POSSIBLE、16 → SAFE。还不行的：`q != p + n` 结束、结束指针是另一个形参、计数器与指针同步前进（`for (i < n) *p++`）。
- [x] **性能：长调用链的指数爆炸、全局多时的线性开销**（SPEC-0080）：合成 TU（`oobsum/devenv/perf-chain.py`）里 24 层链要 5 秒、40 层超过 2 分钟、
      300 层加 2000 个全局超过 10 分钟——`SymEnv::RangeOf` 对嵌套的 `wrap` 分别求上下界（2^N），找全局每次遍历整张表。现在一遍求上下界、不透明项嵌套 ≤ 4 层、
      全局查表：300 层 250 ms，加 2000 / 5000 个全局 0.7 / 1.2 秒。结果不变（lit、单测、csa、replay、engine_check 都相同）。
- [x] **只在某个返回值下成立的写效应**（SPEC-0079）：`Init` 成功才写长度（`if (...) return ERR; pst->ulLen = x; return OK;`），推出 `when: "ret == 0"` 的写效应；
      调用方 `if (Init(&st) != OK) return;` 之后按写的值判（返回值的情形挂上写入，筛出这个返回值、格没被改过时写回）。**LV5-02（2027H1 的 later=L1）提前做到**：
      TP DEFINITE、TN SAFE。`run.py csa`：通过 108、通过+S 20、级别不同 2、超出预期 4，回退 0。
- [x] **告警 note 按 F-33**（SPEC-0078）：总链 note（"Call chain: A (a.c:72) -> B (b.c:63) -> …"）、每一跳说清做了什么、访问点、分配点；一跳的账本不加 note。
- [x] **写效应 `global`**（SPEC-0077）：被调者写整数全局（`SetLen(100)`、`g_ulIdx += 8`）的新值传给调用方；调用点上区间值的全局经 `locals` 带下去。
      原来记 `global_value` / `type_range_only` 的几处对照现在判出 DEFINITE 100 / POSSIBLE。`run.py csa` 与基线相同。
- [x] **有符号递减循环**（SPEC-0076）：`for (i = n − 1; i >= 0; i--)` 原来因"上界 `p1 − 1` 可能溢出"不收窄；按 06- A-21（有符号溢出是未定义行为）有符号区间的符号界认作可信，
      盖住类型最值的界也认作可信。17 → POSSIBLE、16 → SAFE（原来记账）。`run.py csa` 与基线相同。
- [x] **`while (n--)` 不再丢界**（SPEC-0075）：条件收窄的是临时局部 `t`，`n = t − 1` 在分支前就按收窄前的 `t` 算好了；现在条件边上重算同一块里依赖它的局部。
      `while (n--) p[n] = 0;` 的循环体 `[0, n − 1]`：17 → POSSIBLE、16 → SAFE（原来都记账）；`if (len-- > 0)` 同样。
- [x] **写效应 `field` 的区间**（SPEC-0074）：几条路径写不同的常数（32 / 8）时记 `range [8, 32]`，调用方按区间判（原来记账）。
- [x] **写效应 `field`**（SPEC-0073）：被调者写形参所指对象的整数字段（`pst->ulLen = 64`、`*pulLen = 32`）传给调用方；有路径没写（出错提前返回）时取旧值与新值的并；
      没写 `return` 的函数按"隐式返回"也记路径。原来记账的"被调者改过字段"对照（4 处 lit）现在判出确切值。`run.py csa` 与基线相同。
- [x] **经指针传进来的整数**（SPEC-0071）：`UINT32 *pulLen` 的 `*pulLen` 原来读成 `data`，只在被调者里记 `type_range_only`；现在是伪字段 `pK->_0`，
      调用方 `&n` 的值带下去（`n = 20` → POSSIBLE、`n = 16` → SAFE）；同类型整数指针形参算别名（`F(&n, &n)`）。`run.py csa` 与基线相同。
- [x] **代入被调者式子时的字段符号**（SPEC-0072）：`CallerExpr` 原来只认 `p<数字>`，被调者的 `p0->ulLen` 漏过去被当成调用方自己 `p0` 的字段——
      `GetLen(&st)`（`st.ulLen = 100`）借用调用方守卫 `pstMine->ulLen <= 10` **判成 SAFE（错误，SPEC-0054 起就有）**。现在字段按实参所指对象的当前值换，换不了算未知。
- [x] **效果 `ret_ptr`**（SPEC-0070）：被调者返回"实参指针 / 实参所指结构体的指针字段 + 同一个偏移"（取子缓冲区的小函数）时，调用方拿到真实对象上的指针
      （原来未知 → `untracked_pointer`）；经包装函数也能传递。`run.py csa` 与基线相同。
- [x] **区间值的字段带进调用点**（SPEC-0069）：分支汇合后不同的内存格值原来直接丢掉；现在汇合成区间，实参所指结构体的字段是代码给出的区间时写成调用点局部
      `_fld<k>_<字段>`（区间在 `locals`）。探针：分支里赋 5 / 100 → POSSIBLE（原来记账），两个分支都在界内 → SAFE（原来记账）。`run.py csa` 与基线相同。
- [x] **告警文本里的内部名字**（SPEC-0068）：字符串常量写成 `a string literal`（原来 `'__str'`）；调用点上不知道的字段值写字段名（原来 `_unknown_ulLen`）。只改文本。
- [x] **告警文本里的被调者符号**（SPEC-0067）：调用点上不知道的被调者符号按被调者的形参名写（"c->ulLen"，原来 "p1->ulLen" 像是调用方自己的形参）。只改文本。
- [x] **三阶段规模压测**（2026-10-16，无代码改动）：`compiler-rt/lib/builtins`、`compiler-rt/lib/profile`、`llvm/lib/Support/reg*.c` 里 cc1 能编的 177 个 C 文件当一个整程序，
      `oobsum_run.py local`（x86_64）：solve `tus=177 functions=342 decisions=1493`，**无崩溃**，三阶段合计约 4 秒。报出的全是 SUSPECT：`untracked_pointer` 153
      （125 条在 `regengine.inc`，超过两层的字段指针，按 D-27 不跟踪）、`type_range_only` 53、`unknown_callee` 43、`entry_unproven` 33、`typed_field` 22、`pointer_stepping` 17、
      `recursion` 13、`alloc_size_unknown` 3。
- [x] **健壮性复查**（2026-10-17，SPEC-0080…0092 之后）：上游 `clang/test/Analysis/*.c` 234 个文件：213 正常、21 本身编译不过、**0 崩溃、0 超时**，最慢 54 ms；
      compiler-rt 三阶段压测判定与 SPEC-0080 时完全相同（989 SAFE、503 SUSPECT）。
- [x] **健壮性**（2026-10-16，无代码改动）：上游 `clang/test/Analysis/*.c` 234 个文件（x86_64）跑引擎：213 个正常、21 个本身编译不过（不加本规则也一样），
      **无崩溃、无超时**，最慢 0.5 秒。
- [x] **S9-1 调用点记被调者的函数键**（SPEC-0027）：`callee_key`；引擎的摘要表、`Propagate`、`TopoOrder`、效果代入、`not_in_tu` 按键找被调者（金标准没有
      `callee_key` 时照旧按名字）；调用链仍写函数名。
- [x] **S9-2 `Phase=export`**（SPEC-0028）：写 `WorkDir/fir/<tu>.json`，不求解、不报、不写判定日志；inline + `WorkDir` 另写一份 `fir/`（X-06）。
- [x] **S9-3 `Phase=solve`**（SPEC-0029）：桩 TU 读 `WorkDir/fir/*.json`（坏文件跳过并提示），所有 TU 的函数按键去重、全局自底向上解释、一次推到底；
      被调者哪里都没有定义 → `unknown_callee`；判定按汇合点函数所在的 TU 写 `WorkDir/solved/<tu>.json`。`DecisionFromJson` 供 report 读回。
- [x] **S9-4 `Phase=report`**（SPEC-0030）：各 TU 读 `solved/<tu>.json` 报出（策略在此生效）、写判定日志；没被求解的 TU 提示一行。
      **本地 export → solve → report 端到端跑通**（lit `phase-solve.test`）。
- [x] **S9-5 驱动**（SPEC-0031，`oobsum/tools/oobsum_run.py`）：`local` 按源文件列表跑三阶段（cc1，同 run.py csa）；`bxen` 生成桩 TU 的 cdb、report 的 cdb
      （原样复制有判定的 TU）并打印 / 执行三段 bxen 命令；`status`。每条命令先打印再执行、输出存 `logs/<阶段>.log`、失败即停并提示单独重跑。
- [x] **S8c-2**：只在某个返回值下成立的写效应（SPEC-0079，原计划 2027H1）；告警 note 按 F-33（SPEC-0078）。
      已做：`field` / `global` 写效应（SPEC-0073 / 0074 / 0077）、mod-ref（SPEC-0045）、`unknown_callee` 与跨 TU 三阶段（SPEC-0028…0031）；A-41 见 S7d-4。
- [ ] S9 之后：WP5 安全函数族模型 → WP7 循环与守卫 → WP8 效果与分配器 → WP9 大小模型 → WP10 驱动 → WP11 真实代码子集（公司）→ WP12 全量与性能 → WP13 冻结。

## 等用户提供 / 在公司做的

* **等用户确认（SPEC-0103）**：`CharAlias` 的默认值（现在先关）与"假设不别名、在调用点核对"的提议——打开后 `for (i < pst->ulLen) pucBuf[i]` 这类循环的界丢掉、
  真报降成 SUSPECT（本仓库 9 个功能 lit 的结果变了），比预想的噪声大。
* **已决定（2026-10-17，HANDOFF D20…D23）**：① `char *` 写当成可能改了任何外部内存，选项 `CharAlias` 控制（SPEC-0103，默认先关）——**公司实测**：业务子目录上
  `CharAlias=true` / `false` 各跑一遍，带回 SUSPECT 条数差，再定默认；② G2 口径改为 TN 零 DEFINITE / POSSIBLE、SUSPECT 单列（SPEC-0102，已改）；
  ③ 多维数组每一维、成员数组都查内层界，越出内层没越出外层记 SUSPECT（新原因）；④ `strcpy_s` 族出错时写多少：公司确认后在模型文件里按需配置
  （出错按 destMax 清零 → 去掉 `upto`），不影响开发；确认后再按 SPEC 改 LV7-05 的 `@expect`。

* **跨 TU 的 bxen 命令格式**（SPEC-0031 §8）：bxen 跑多个文件时 `-cc_json` 给目录还是文件、`-analysis_folder` 与过滤后的 cdb 怎么配合。驱动的模板按 COMPANY.md §4
  的写法猜的；在公司先用 `oobsum_run.py bxen …`（不加 `--run`）打印三段命令，跑一遍 TestCase 目录，不对就把正确的写法告诉我。

* 公司（可选，很快）：bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=fir` 跑 TestCase，看 `module` 行是否 `ptr=4 long=4 big-endian`
  （fork 是否按 ILP32 分析，对应探针 R2），以及 `char` 字段是否 `u8`。只带回"是/否"。
* 公司：`oobsum/devenv/fork-facts.sh` 的输出；I0 探针 R1–R3；TestCase 在 bxen 下的结果（**现在预期 4 条告警 + 判定日志 7 条**：
  骨架的 3 条 + 引擎的三层链 `Tc_Engine_TP` 一条 DEFINITE；`Dump=stats` 应为 `decisions=7 emitted=4`）。
* 公司（可选）：引擎已在默认的 `Phase=inline` 下跑。拿 bxen 跑一个业务子目录，
  带回各标签 / 原因的条数与耗时（`tools/decisions.py` 汇总，只要数字），用来定 `SuspectReasons` 的默认值（`not_in_tu`、`typed_field` 预计很多）。
* 公司（可选）：业务代码里函数 CFG 块数的分布（P99、最大值），用来定 `MaxBlocks` 的默认值（现在 5000，06- I-18"探针给出 P99 后再定"；SPEC-0037 §8）。
* **I5 / I8 / I9 现在写进模型文件即可，不用改代码**（SPEC-0033）：复制 `oobsum/config/oob_models.example.yaml`，改 `VOS_MemAlloc` 的 `size`（I5）、
  加内部封装的拷贝 / 分配函数（I8）、纯函数清单（I9），bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:ModelFile=<路径>`，用 `Dump=models` 确认生效。
  文件留在公司；只带回"哪几类、各多少条"即可（如果想让我把某些通用的放进内置表，告诉我函数原型）。
* 其余输入见 `HANDOFF.md` §6。

## 已知问题与注意

* **"先访问、后检查"的循环推不出界**（2026-10-16 探针）：`do { p[i] = 0; } while (++i < n);`、`while (1) { p[i] = 0; if (++i >= n) break; }`、`goto` 回跳：
  循环头要汇合入口的 0 与回边的 `[0, n − 1]`，真实上界是 `max(0, n − 1)`，符号界表示不了 → 被调者里记 `type_range_only`（照样报出，不漏）。要做得剥第一轮或分情形。
* **`(UINT32)i < 16` 不当成 `i >= 0`**：有符号下标先转无符号再比较的写法，前向判安全，负偏移那一侧仍多一条 SUSPECT（只是噪声）。


* **负偏移的误报风险**（SPEC-0039 §3）：内存池内部"从用户指针往回找块头"（`(HDR *)p - 1`）若在分析范围内且分配器被建模，会报 DEFINITE；
  带符号下标只检查了上界的会多一条下溢 SUSPECT。公司跑业务子目录时留意 `dir: back` 的判定数量与分布。

* **入口函数自己的访问只在入口记账**：判定只在调用点定级（Apply），没有调用方的函数里、本函数内就越界的访问（`snprintf_s(buf8, 64, …)` 直接写在入口里）
  报 SUSPECT `entry_unproven`，不是 DEFINITE / POSSIBLE（照样报出，标签偏保守）。深度 0 的越界按 06- 交给现有规则；要不要在入口就地定级，以后另写 SPEC。


* `-analyzer-config` 的列表值用 `+` 连接（逗号会被当成键值对分隔符）。
* 上游 clang 14 不支持 aarch64 ILP32；本地布局近似用 `TRIPLE=armebv7-linux-gnueabi`。
* 调试内建函数只用于测试；引擎接上后保留它（便于单独测策略与日志），但业务代码里不会出现。
* `summary.schema.json` 的 `$id` 是相对的（`oobsum-summary/1`）：别的脚本要用 jsonschema 校验摘要，也要先去掉 `$id`（jsonschema 3.x 会解析失败，见 `replay.py`）。
* `run.py csa` 的基线只在有意改变时 `--save`（不能与 `--only` 同用），提交信息里写清楚哪些入口的实际变了；`csa-result.json` 在
  `cmake-build-release/oobsum-csa/`。`alerts.json` / `replay.py` 仍把 `记账` 当成不报，补不补 SUSPECT 条目留到 S8 另写 SPEC。
* 直接跑 cc1 时 ARM / AArch64 的 `char` 默认**有符号**（driver 才会加 `-fno-signed-char`）：本地脚本已处理；手写 cc1 命令时记得加。
* **LLVM 14.0.0 的 `json::parse` 把超出 int64 的整数悄悄饱和成 INT64_MAX / INT64_MIN**（实测，不报错）。FIR 读入因此把这两个值本身也拒绝，
  降级遇到这么大的常量要写 `["unk", "const_overflow"]`（SPEC-0005 §2.3 #5）；S8 的 C++ 摘要读入同样要守这条。
* `InlineDepth` 默认 2：浅层的 DEFINITE / POSSIBLE 交给现有规则（仍写日志，原因 `inline_depth`）；要全报设 0。
