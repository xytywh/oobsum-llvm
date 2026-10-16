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
- [ ] **S5b**：`MaxBlocks`（A-09，要改 Checkers.td）；不支持的构造（`unk(<类名>)`）计入判定日志（SUSPECT:unsupported_construct）——引擎接上时一起做。
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
- [ ] **S7c-2**（以后按需要）：阈值 widening、递减循环的下界、存进局部的"可当上界"的值（来源标记）、带 `min` / `max` 的界（A-10、A-15、A-17）。
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
- [ ] **S7d-2**：`ModelFile`（YAML，I-40）、`strcpy_s` 等其余 securec 函数（I8）。
- [ ] **S7d-3**：`ret_new` 效果（封装分配器，D-24、A-63…A-65；LV4-01）——要求被调者的摘要先算出来（自底向上），与 S8c 的调度一起做。
- [ ] **S7d-4**：条件效应（校验函数的返回值，LV4-06）；局部结构体字段带进调用点（LV5-01 的 `fields`）；偏移与长度的抵消（A-41，LV3-07、LV8-01）。
- [ ] **S7e** `extern` 不完整数组的大小符号 `ext(g:名)`（LV6-01；全局大小表在 S8c / WP9 接上）。
- [ ] **S8c 接进 checker**（WP4 其余）：`core/Solver`（同 TU 的调用图、SCC 逆拓扑序求解，A-80…A-82）、`Dump=summary` / `Dump=apply`；
      S7 产出的摘要经 `Apply` 判定 → 标签 / 告警 / 判定日志；Lv1 + Lv2 用例出告警，`run.py csa` 对照 `@expect`（G1）。
- [ ] S9 之后：WP5 安全函数族模型 → WP7 循环与守卫 → WP8 效果与分配器 → WP9 大小模型 → WP10 驱动 → WP11 真实代码子集（公司）→ WP12 全量与性能 → WP13 冻结。

## 等用户提供 / 在公司做的

* **点头**：SPEC-0002 §7 提议把 07- / 06- T-09 的 G2"期望为 `-` / `记账` 的入口零告警"改成"零 DEFINITE / POSSIBLE 告警，SUSPECT 条数单列"
  （D16 召回优先的直接推论；不点头的话，G2 时 TN 上的 SUSPECT 都算不通过）。
* 公司（可选，很快）：bxen 命令加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=fir` 跑 TestCase，看 `module` 行是否 `ptr=4 long=4 big-endian`
  （fork 是否按 ILP32 分析，对应探针 R2），以及 `char` 字段是否 `u8`。只带回"是/否"。
* 公司：`oobsum/devenv/fork-facts.sh` 的输出；I0 探针 R1–R3；本骨架在 bxen 下的 TestCase 结果（预期 3 条告警 + 判定日志 5 条）。
* 其余输入见 `HANDOFF.md` §6。

## 已知问题与注意

* `-analyzer-config` 的列表值用 `+` 连接（逗号会被当成键值对分隔符）。
* 上游 clang 14 不支持 aarch64 ILP32；本地布局近似用 `TRIPLE=armebv7-linux-gnueabi`。
* 调试内建函数只用于测试；引擎接上后保留它（便于单独测策略与日志），但业务代码里不会出现。
* `summary.schema.json` 的 `$id` 是相对的（`oobsum-summary/1`）：别的脚本要用 jsonschema 校验摘要，也要先去掉 `$id`（jsonschema 3.x 会解析失败，见 `replay.py`）。
* `run.py csa` 的基线只在有意改变时 `--save`（不能与 `--only` 同用），提交信息里写清楚哪些入口的实际变了；`csa-result.json` 在
  `cmake-build-release/oobsum-csa/`。`alerts.json` / `replay.py` 仍把 `记账` 当成不报，补不补 SUSPECT 条目留到 S8 另写 SPEC。
* 直接跑 cc1 时 ARM / AArch64 的 `char` 默认**有符号**（driver 才会加 `-fno-signed-char`）：本地脚本已处理；手写 cc1 命令时记得加。
* **LLVM 14.0.0 的 `json::parse` 把超出 int64 的整数悄悄饱和成 INT64_MAX / INT64_MIN**（实测，不报错）。FIR 读入因此把这两个值本身也拒绝，
  降级遇到这么大的常量要写 `["unk", "const_overflow"]`（SPEC-0005 §2.3 #5）；S8 的 C++ 摘要读入同样要守这条。
* 头文件里的 `static inline` 目前同名同键（SPEC-0003 §2.1）：S4 加 `#<FIR 哈希前 8 位>` 之前，同一头文件里因宏展开不同而函数体不同的版本
  会共用一个键；S8 摘要接上之前必须补上。
* `InlineDepth` 默认 2：浅层的 DEFINITE / POSSIBLE 交给现有规则（仍写日志，原因 `inline_depth`）；要全报设 0。
