# SPEC-0001 判定标签、召回优先的报告策略、判定日志、调试输出（checker 骨架）

状态：已实现（v4.0，checker 骨架提交）；§5 的 `fn` 已是函数键、§6.1 新增种类 `keys`（SPEC-0003）
来源：用户 2026-10-14 的要求（原话摘录）——
"函数摘要方案的告警对于明确有问题的，以及明确无问题的都要加标签可以去识别"；
"工具尽量不漏报（可以有误报）……只有当正报和漏报要做权衡的时候，我们倾向于保证尽量不要漏报；后续针对这些告警再用大模型去做复核，做告警过滤"；
"也要有把摘要或者必要的调试信息生成到磁盘的代码……就像 CSA 中 dump 出 AST、观察 MemRegion、SVal、约束"。

## 1. 要解决什么

1. 每条判定都要能一眼认出"明确越界 / 可能越界 / 拿不准 / 明确安全"，告警文本、BugType、机器可读的记录里都要有。
2. 报告策略从"精确优先"（06- F-15、F-16：拿不准就记账不报）改为**召回优先**：拿不准就报，并写明原因，交给大模型复核过滤。
3. 每条判定（包括不报的）都要留下机器可读的记录，作为调试与复核的输入。
4. 引擎的每个环节都要能把中间数据打出来看，且不影响结果。

引擎（FIR、摘要、代换判定）还没写；本 SPEC 先把"判定 → 标签 → 策略 → 告警 / 日志 / 调试输出"这条出口做好，引擎接上后直接产出 `Decision`。

## 2. 标签

| 标签 | 含义 | 对应主规格 | 默认报不报 |
|---|---|---|---|
| `DEFINITE` | 对所有取值都越界：最小末端 > 对象大小 | F-11（L1） | 报 |
| `POSSIBLE` | 上界来自代码给出的界（校验、掩码、循环界、分支常量、返回值范围、守卫），最大末端 > 大小，最小末端不越界 | F-12、F-12b（L2） | 报 |
| `SUSPECT` | 证明不了安全，也判不了越界：上界只来自类型范围 / widening / 数据；条件到入口仍未证明；调用链断开（未知函数、函数指针……）；函数没分析完 | 原 F-15 的 S2 与账本（D-27） | 报（`Policy=recall`） |
| `SAFE` | 证明了安全 | F-10 | 不报，只进判定日志（`ReportSafe=true` 时也报） |

每条判定有一个**原因**（`reason`），取值：

* `SUSPECT`：账本原因（06- D-27 的 27 类，如 `entry_unproven`、`type_range_only`、`pointer_stepping`、`unknown_callee`、`fptr`、
  `cond_truncated`、`unsupported_construct`）；函数没分析的记 `unanalyzed:<原因>`（`unanalyzed:timeout`、`unanalyzed:cfg_too_large`）。
* `SAFE`：怎么证明的——`end_le_have`（最大末端 ≤ 大小）、`guard_false`（守卫在此为假，访问走不到）、`guard_entails`（守卫蕴含）、
  `guard_bound`（F-12b 守卫界判定得出安全）。
* `DEFINITE` / `POSSIBLE`：原因可空；`POSSIBLE` 可以写界的种类（`loop`、`check`…）。

判定在哪里产生（引擎接上后）：调用点代换后能判定时（汇合点）；入口处仍有未证明的 Req 时；depth 0（函数内就能判）时；断链处。

## 3. 报告策略

`core/Policy.cpp` 的 `Classify(decision, options)` → `{emit, why}`：

| 标签 | `Policy=recall`（默认） | `Policy=balanced` | 另外 |
|---|---|---|---|
| DEFINITE / POSSIBLE | 报 | 报 | `max(depth, effDepth) <= InlineDepth` 时不报，`why=inline_depth`（交给现有规则；`InlineDepth=0` 全报） |
| SUSPECT | 报 | 不报，`why=policy_balanced` | `SuspectReasons` 非空且原因不在其中：不报，`why=reason_filtered`；**不按层数过滤**（现有规则也没判出来的） |
| SAFE | 不报，`why=safe` | 同左 | `ReportSafe=true` 时报 |

告警（`report/Reporter.cpp`，06- F-30…F-35 的修订）：

* 每个标签一个 BugType：`Out-of-bound access via call chain (definite|possible|suspect|proven safe)`，category `Memory error`（只用于展示）。
* 文本：`[OOB-DEFINITE] <F-32 正文>`、`[OOB-POSSIBLE] …`、`[OOB-SUSPECT:<原因>] …`、`[OOB-SAFE:<原因>] …`。复核工具按前缀分流。
* 位置：汇合点调用（F-30）；位置换不到本 TU 时退回主文件开头，原位置写进文本（A-91）。链上在本 TU 可见的每一跳一条 note（F-33）。
* 同一 TU 按（文件、行、列、指纹）排序后发出（F-35）。指纹（F-34）的第一项是标签名。

**召回不回退原则**：以后任何提高精度的修改，只能把判定从 SUSPECT 改成 SAFE（证明了安全）或改成 DEFINITE / POSSIBLE，
不能让一条原来报的判定消失。回归用例守住这一点（S1 的 `cases/run.py csa` 会对比标签变化）。

## 4. 给大模型复核的约定

* 输入是判定日志（§5），复核工具（以后单独写，不在 checker 里）读 `emitted=true` 的条目，按标签与原因分流：
  `DEFINITE` 抽检；`POSSIBLE` 看界的来源是否真的可达；`SUSPECT` 按原因准备不同的提示（如 `entry_unproven` 去查入口的调用方 / 外部输入）。
* 复核需要的上下文：调用链（`chain`，每跳 `函数@文件:行`）、对象与大小（`obj`、`have`）、末端区间（`end`）、界的来源（`bound`）；
  源码片段由复核工具按位置去取，不存在日志里。摘要与中间数据用 `Dump=summary+apply` 另外打出来。
* 复核结论按指纹 `fp` 回填，不改这里的日志。

## 5. 判定日志（`core/DecisionLog`）

`WorkDir` 非空时，每个 TU 写一份 `<WorkDir>/decisions/<tu-id>.jsonl`；`tu-id` = 规范化主文件路径的 xxHash64（16 位十六进制）。
第一行是 TU 头，后面每条判定一行：

```json
{"count":5,"file":"/path/TC.c","schema":"oobsum-decisions/1","tu":"9c1f…"}
{"bound":"","chain":[],"depth":3,"eff_depth":0,"emitted":true,"end":["",""],"fn":"Tp_Labels","fp":"…","have":"","kind":"W",
 "label":"definite","msg":"debug decision from oobsum_debug_emit()","obj":"","origin":"debug-intrinsic","reason":"",
 "schema":"oobsum-decision/1","site":"/path/TC.c:40:5"}
{"…":"…","emitted":false,"label":"safe","not_emitted_because":"safe","reason":"guard_false"}
```

| 字段 | 含义 |
|---|---|
| `label`、`reason` | §2 |
| `emitted`、`not_emitted_because` | §3 的去向与理由（`safe` / `policy_balanced` / `reason_filtered` / `inline_depth`） |
| `site` | 告警位置 `文件:行:列` |
| `fn` | 汇合点函数（引擎接上后是函数键，D-01…D-08） |
| `kind`、`obj`、`have`、`end`、`bound`、`depth`、`eff_depth`、`chain` | 与摘要的判定记录同义（06- D-26） |
| `msg`、`fp` | F-32 正文（不含标签前缀）、F-34 指纹 |
| `origin` | 谁产生的：`engine` / `debug-intrinsic` |

`oobsum/tools/decisions.py <WorkDir>` 汇总：按标签、原因、去向计数，列出报了的条目；`--label suspect --reason entry_unproven` 过滤。

## 6. 调试输出（`debug/DumpSink`）

### 6.1 种类

`Dump` 选项取下面的种类，多个用 `+` 连接（`-analyzer-config` 会把逗号当成键值对分隔符，实测）；`all` = 全部。

| 种类 | 打印什么 | 状态 |
|---|---|---|
| `options` | 生效的选项值 | 已实现 |
| `callgraph` | 自底向上的求解顺序（SCC，被调者先），成环的标 `cycle` | 已实现 |
| `decisions` | 每条判定一行 JSON（与判定日志同形） | 已实现 |
| `stats` | 每 TU 一行：判定数、报出数、日志路径 | 已实现 |
| `fir` | 每个函数的 FIR（文本记法 + `WorkDir/dump/<tu>/fir/<fn>.json`） | 预留（S3–S5） |
| `state` | 不动点后每个基本块入口的抽象状态（值、指针的对象与偏移区间、守卫） | 预留（S7） |
| `summary` | 每个函数的摘要（带 `doc` 的可读 JSON，06- D-30） | 预留（S8） |
| `apply` | 每个调用点的代换过程（06- §6.6 的五步，同 `replay.py -v` 的格式） | 预留（S8） |

### 6.2 位置与格式

* stderr：每行 `[oobsum:<种类>] <内容>`，lit 用例可以直接 FileCheck。
* 文件：`<WorkDir>/dump/<tu-id>/<种类>/…`（`WorkDir` 为空时只打 stderr）。
* `DumpFilter=<正则>`：函数级的输出只留名字匹配的函数。
* **调试输出不得影响判定**：只读，不改任何状态；新种类配一个 lit 用例。

### 6.3 调试内建函数

```c
void oobsum_debug_emit(const char *label, const char *reason, int depth);
```

测试源码里声明并调用；checker 认出它（名字、3 个参数、前两个是字符串字面量、第三个是整数常量）后，在调用处产出一条判定：
`label` 取 `definite` / `possible` / `suspect` / `safe`，`fn` = 所在函数，`kind = W`，`origin = debug-intrinsic`。
它让"判定 → 标签 → 策略 → 告警 / 日志"在引擎接上之前就能测，引擎接上后也保留（单独测策略与日志）。业务代码里不会有这个名字。

## 7. 分层与文件

| 文件 | 依赖 | 内容 |
|---|---|---|
| `OobSummaryChecker.cpp` | Clang SA | 入口：选项、TU 回调、调试内建函数、调用图输出、把判定交给 Reporter |
| `OobSummary/core/Label.{h,cpp}`、`Decision.h`、`Policy.{h,cpp}`、`Fingerprint.{h,cpp}`、`DecisionLog.{h,cpp}` | LLVM Support | 不依赖 Clang（06- §9.1） |
| `OobSummary/report/Reporter.{h,cpp}` | Clang SA | 判定 → BasicBugReport |
| `OobSummary/debug/DumpSink.{h,cpp}` | LLVM Support | 调试输出 |

## 8. 选项（`Checkers.td` 的 `CheckerOptions`）

| 选项 | 类型 | 默认 | 说明 | 非法值 |
|---|---|---|---|---|
| `Phase` | string | `inline` | `inline` / `export` / `solve` / `report`（06- §7；目前只有 inline 有行为） | 报错 |
| `WorkDir` | string | `""` | 判定日志与 dump 的目录；空则不写文件 | —— |
| `Policy` | string | `recall` | §3 | 报错 |
| `InlineDepth` | int | `2` | §3；`0` 全报 | 负数报错 |
| `ReportSafe` | bool | `false` | §3 | —— |
| `SuspectReasons` | string | `""` | `+` 连接的原因；空 = 全部 | —— |
| `Dump` | string | `""` | §6.1 | 未知种类报错 |
| `DumpFilter` | string | `""` | §6.2 | 非法正则报错 |

非法值用 `reportInvalidCheckerOptionValue` 报错，不悄悄用默认值（lit：`options-invalid.c`）。

## 9. 对主规格（06-spec.md）的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-15 | S2（符号可疑）P0 不输出 | 记为 `SUSPECT`，按 `Policy` 输出（默认输出） |
| F-16 / I-14 `ReportL2` | `ReportL2=false` 只报 L1 | 由 `Policy` 取代：`balanced` = 报 DEFINITE + POSSIBLE |
| F-20 / I-12 `InlineDepth` | 所有判定按层数过滤 | 只过滤 DEFINITE / POSSIBLE；被过滤的仍写判定日志（`inline_depth`） |
| F-22 / I-13 `EvalMode` | 不过滤、全报 | 由 `InlineDepth=0`（+ `ReportSafe=true`）取代 |
| F-31 | 两个 BugType（definite / possible） | 四个，每个标签一个 |
| F-32 | 消息正文 | 加 `[OOB-<标签>[:<原因>]]` 前缀 |
| F-34 | `level` | 标签名 |
| D-26 | `reps[]` 与 `reports.json` | 另有判定日志 `decisions/<tu-id>.jsonl`，所有判定（含不报的） |
| I-21 / I-22 | `Dump`（逗号组合）/ `FuncFilter` | `Dump`（`+` 连接，§6.1）/ `DumpFilter` |
| 新增 | —— | `Policy`、`ReportSafe`、`SuspectReasons` |

## 10. 测试

| lit 用例（`clang/test/Analysis/oobsum/`） | 覆盖 |
|---|---|
| `registration.c` | checker 与 8 个选项已注册 |
| `labels.c` | 四个标签、`Policy=balanced`、`ReportSafe`、`InlineDepth=0`、`SuspectReasons` 的五种组合 |
| `decision-log.c` | 判定日志：报了的与没报的都在、`not_emitted_because`、TU 头；`Dump=stats` |
| `dump-callgraph.c` | 求解顺序、`cycle`、`DumpFilter` |
| `dump-kinds.c` | `+` 连接的多个种类与原因 |
| `options-invalid.c` | 非法 `Policy`、`Dump` 报错 |

## 11. 公司侧验证

`oobsum/testcase/OobSummaryChecker/TC_OobSummaryChecker.c`（文件头有 bxen 命令）：默认选项下 3 条告警
（`[OOB-DEFINITE]`、`[OOB-POSSIBLE]`、`[OOB-SUSPECT:entry_unproven]`），判定日志 5 条（另 2 条 `safe`、`inline_depth`）。

## 12. 待定 / 以后

* 断链处（指针流进未知函数）产出 SUSPECT 的条件：全报会很多，留到 WP5 / WP8 有模型表后另写 SPEC。
* `unanalyzed:*` 的 SUSPECT 报在函数声明处，每个函数至多一条。
* 大模型复核工具本身（输入输出格式、提示词）另写 SPEC。
