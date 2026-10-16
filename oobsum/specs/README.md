# specs：增量规格

`06-spec.md` 是 P0 主规格（v3.12 起草，v3.13–v3.15 修订）。从 v4.0 起，**每一次设计或行为上的修改都先在这里写一份 SPEC**，
再写代码；代码注释引用 SPEC 号（`SPEC-0001 §3`），提交信息也写上。这样从任何一行代码、任何一条告警，都能追到"当时为什么这样设计"。

## 规则

* 文件名 `SPEC-NNNN-<英文短名>.md`，编号递增、不复用。一次提交可以只改一个 SPEC 的一部分，但 SPEC 先于或同时于代码提交。
* 修订 `06-spec.md` 已有条目时：在 SPEC 的"对主规格的修订"一节列出条目号与新写法，并在 `06-spec.md` 原条目旁注明"由 SPEC-NNNN 修订"。
* SPEC 写定后不改历史结论；后来改主意就写新的 SPEC，在新 SPEC 里写"取代 SPEC-MMMM §x"，并在旧 SPEC 的状态栏标"部分被 SPEC-NNNN 取代"。
* 每个 SPEC 都要有：可执行的测试（lit / 单元 / 用例）、调试手段（哪个 `Dump` 种类能看到什么）、对召回的影响（会不会让原来报的不报）。

## 模板

```markdown
# SPEC-NNNN 标题

状态：草案 / 已实现（提交 abc1234）/ 部分被 SPEC-MMMM 取代
来源：用户要求 / 主规格条目 / 用例 / 公司验证发现（写清楚原话或现象）

## 1. 要解决什么
## 2. 设计（数据结构、算法、选项、输出格式；必要时给例子）
## 3. 对召回与误报的影响（会不会把报变成不报？为什么安全？）
## 4. 实现位置（文件、函数）
## 5. 测试（lit 用例名、单元测试、用例对照；预期结果）
## 6. 调试（打开哪个 Dump、看什么字段）
## 7. 对主规格的修订（条目号 → 新写法）
## 8. 公司侧验证（bxen 命令、预期）
## 9. 待定 / 以后
```

## 索引

| 编号 | 标题 | 状态 |
|---|---|---|
| [SPEC-0001](SPEC-0001-labels-recall-debug.md) | 判定标签、召回优先的报告策略、判定日志、调试输出（checker 骨架） | 已实现（v4.0） |
| [SPEC-0002](SPEC-0002-cases-csa-yardstick.md) | 用例回归尺子 `cases/run.py csa`：`@expect` ↔ 标签的对照口径（修订 T-03）、召回不回退检查 | 已实现（S1） |
| [SPEC-0003](SPEC-0003-keys.md) | 函数 / 全局的键（D-01…D-05、D-08）与 `Dump=keys`；判定的 `fn` 改为函数键 | 已实现（S2） |
| [SPEC-0004](SPEC-0004-core-unittests.md) | `core/` 单元测试 `oobsum-core-tests`（不依赖 gtest，`oobsum/unittests/`，进 lit） | 已实现（S3a） |
| [SPEC-0005](SPEC-0005-fir-json.md) | FIR 的 C++ 结构与 JSON 读写（`oobsum-fir/1`，D-10…D-14；§5.2 未写死处的约定） | 已实现（S3b） |
| [SPEC-0006](SPEC-0006-fir-text-dump.md) | FIR 的文本记法（`core/FirText`）与 `Dump=fir` | 已实现（S4a、S4b） |
| [SPEC-0007](SPEC-0007-lower-skeleton.md) | 降级第一步：类型布局与模块骨架（`lower/TypeLayout`、`lower/Lowering`），接上 `Dump=fir` | 已实现（S4b） |
| [SPEC-0008](SPEC-0008-lower-straight-line.md) | 降级：直线代码的指令（A-01…A-03、A-05…A-08） | 已实现（S4c-1、S4c-2） |
| [SPEC-0009](SPEC-0009-lower-control-flow.md) | 降级：条件边（`assume`）、`switch`、`?:` / `&&` / `\|\|` 当值用（D-14、A-04） | 已实现（S5） |
| [SPEC-0010](SPEC-0010-linexpr.md) | 线性式 `core/LinExpr`：规范形式、D-21 文本语法、条件、代换（与 `replay.py` 同语义） | 已实现（S6a） |
| [SPEC-0011](SPEC-0011-range-entail.md) | 取值范围与蕴含 `core/Range`（符号环境、`range_of`、守卫收窄、`entails_le0`） | 已实现（S6b） |
| [SPEC-0012](SPEC-0012-summary-apply.md) | 摘要读写 `core/Summary` 与调用点判定 `core/Apply`，在 104 份金标准摘要上对照 `replay.py` | 已实现（S8a-1、S8a-2） |
| [SPEC-0013](SPEC-0013-propagate-alerts.md) | 从局部事实推到底（`core/Solver` 的 `Propagate`）与告警成品（`core/Alert`），与 `alerts.json` 逐条相同 | 已实现（S8b） |
| [SPEC-0014](SPEC-0014-interp-straight.md) | 函数内解释第一步：无环代码的值与访问检查（`core/Interp`），产出 depth 0 的 Req | 已实现（S7a-1） |
| [SPEC-0015](SPEC-0015-local-facts-dump.md) | 调用点记录与摘要组装、`Dump=summary` / `Dump=state`、引擎对照脚本（端到端 T-06） | 已实现（S7a-2） |
| [SPEC-0016](SPEC-0016-interp-guards.md) | 函数内解释第二步：条件边与守卫、当场证明安全（A-14、A-30、A-43、A-44） | 已实现（S7b） |
| [SPEC-0017](SPEC-0017-interp-loops.md) | 函数内解释第三步：循环（工作表不动点、widening、narrowing、`loop` 标签、`pointer_stepping`） | 已实现（S7c-1） |
| [SPEC-0018](SPEC-0018-models-access.md) | 模型表第一步：securec / libc 访问函数、纯函数、分配器的直接调用（F-03…F-05、I-40） | 已实现（S7d-1） |
| [SPEC-0019](SPEC-0019-typed-field.md) | F-02 的字段访问照生成、入口记 `typed_field`；形参带 `size`；金标准补齐漏写的访问、`golden_regen.py` | 已实现 |
| [SPEC-0020](SPEC-0020-securec-second-range.md) | F-03 destMax 回绕的第二段；模型给出的守卫总挂在这次访问上 | 已实现 |
| [SPEC-0021](SPEC-0021-inline-engine.md) | 引擎接进 checker（`Phase=inline`）：单个 TU 内推到判定、带标签报出、全部进判定日志；`Dump=apply` | 已实现（S8c-1） |
| [SPEC-0023](SPEC-0023-symbolic-join.md) | 汇合、收窄时用类型范围与守卫比较符号界（循环后 `i == ulLen`，LV3-05）；尺子认"改报在汇合点" | 已实现 |
| [SPEC-0024](SPEC-0024-ret-new.md) | 返回新对象的效果（`ret_new`）：自底向上解释，封装分配器逐层推出（LV4-01） | 已实现（S7d-3） |
| [SPEC-0025](SPEC-0025-ret-cond.md) | 校验函数的条件效应（`ret_cond`）：值的"情形"、调用方代入、条件边筛选（LV4-06） | 已实现 |
| [SPEC-0026](SPEC-0026-arg-fields.md) | 局部结构体的字段值带进调用点；别名实参改过的字段写当前值（修旧值证明安全） | 已实现 |
| [SPEC-0027](SPEC-0027-callee-key.md) | 调用点记被调者的函数键（`callee_key`），引擎内部按键找被调者（跨 TU 的准备） | 已实现（S9-1） |
| [SPEC-0028](SPEC-0028-phase-export.md) | `Phase=export`：每个 TU 写 `fir/<tu>.json`，不求解、不报 | 已实现（S9-2） |
| [SPEC-0029](SPEC-0029-phase-solve.md) | `Phase=solve`：读全部 FIR，跨 TU 自底向上解释与求解，判定按 TU 落盘 | 已实现（S9-3） |
| [SPEC-0030](SPEC-0030-phase-report.md) | `Phase=report`：各 TU 按求解结果报出，写判定日志 | 已实现（S9-4） |
| [SPEC-0031](SPEC-0031-driver.md) | 跨 TU 的驱动 `oobsum_run.py`：本地跑三阶段、生成 bxen 命令、`status` | 已实现（S9-5） |
| [SPEC-0032](SPEC-0032-global-size-table.md) | 全局大小表：`extern` 不完整数组在 solve 时取定义所在 TU 的大小；engine_check 走 export + solve | 已实现（S7e） |
| [SPEC-0033](SPEC-0033-model-file.md) | `ModelFile`：YAML 模型表合并进内置表；`Dump=models` | 已实现（S7d-2a） |
| [SPEC-0034](SPEC-0034-securec-string.md) | `strcpy_s` 族的目的一侧：`upto` 条件与来源标签 `model` | 已实现（S7d-2b） |
| [SPEC-0035](SPEC-0035-member-arg-parent.md) | 成员数组当实参：调用点记录带外层对象，代换时外层也查 | 已实现 |
| [SPEC-0036](SPEC-0036-entry-ledger-dedupe.md) | 入口账本按访问去重：同一入口同一次访问只报一条，日志全记 | 已实现 |
| [SPEC-0037](SPEC-0037-max-blocks.md) | `MaxBlocks`：块数超限 / 没有 CFG 的函数不分析、报一条 SUSPECT | 已实现（S5b-1） |
| [SPEC-0038](SPEC-0038-open-reason-data.md) | 调用点记账原因：上界只来自 `data` 时记 `type_range_only`；被调者改字段 / 全局的回归用例 | 已实现 |
| [SPEC-0039](SPEC-0039-underflow-back-req.md) | 负偏移（下溢）：反向 Req `dir: back` 与 `bext(p)` | 已实现 |
| [SPEC-0040](SPEC-0040-field-current-value.md) | 形参所指字段的当前值：改过就不用入口符号；同类型形参的别名 | 已实现 |
| [SPEC-0041](SPEC-0041-global-current-value.md) | 全局标量的当前值：调用过可能写全局的函数之后不用入口符号 | 已实现 |
| [SPEC-0042](SPEC-0042-join-per-branch-bounds.md) | 汇合时按各自路上的守卫求常数界（钳位写法不再丢界） | 已实现 |
| [SPEC-0043](SPEC-0043-join-symbolic-clamp.md) | 汇合时的符号界：一边的界在另一边自己的守卫下也成立（符号形的钳位） | 已实现 |
| [SPEC-0044](SPEC-0044-rep-dedupe.md) | 同一调用点同一次访问的判定只报一条（成员界与外层界） | 已实现 |
| [SPEC-0045](SPEC-0045-mod-ref.md) | mod-ref：摘要记可能写哪些位置，调用点只让这些位置失效 | 已实现 |
| [SPEC-0046](SPEC-0046-widen-loop-vars.md) | 循环头只对本循环里被赋值的局部做 widening（嵌套循环的外层下标不丢界） | 已实现 |
| [SPEC-0047](SPEC-0047-csa-ledger-reachable.md) | `run.py csa`：期望"记账"的入口，账本可以在它调用链上的函数里 | 已实现 |
| [SPEC-0048](SPEC-0048-csa-summit-reachable.md) | `run.py csa`：TP 入口的 L1 / L2 可以判在它调用链上的汇合点 | 已实现 |
| [SPEC-0049](SPEC-0049-eliminate-in-opaque.md) | 调用点消去调用方局部时也进入不透明项（`div` / `mod` / `wrap`） | 已实现 |
| [SPEC-0050](SPEC-0050-widen-unsigned-cap.md) | 无符号局部 widening 后补上类型范围的界（从 1 开始的循环不再丢界） | 已实现 |
| [SPEC-0051](SPEC-0051-struct-copy-model.md) | 结构体整体赋值的内置访问模型 `__oobsum_copy`（规格有、模型表里漏了） | 已实现 |
| [SPEC-0052](SPEC-0052-ret-new-offset.md) | `ret_new` 允许常数偏移：带管理头的分配器（返回 `pucRaw + 16`） | 已实现 |
| [SPEC-0053](SPEC-0053-out-new.md) | 出参式分配器：效果 `out_new`（把新对象写进 `*pp`） | 已实现 |
| [SPEC-0054](SPEC-0054-ret-int-range.md) | 返回值范围 `ret_int`：不是常数的返回也推出（取值 / 截断函数） | 已实现 |
| [SPEC-0055](SPEC-0055-csa-cross-tu.md) | `run.py csa`：多文件用例走 export → solve → report 三阶段 | 已实现 |
| [SPEC-0056](SPEC-0056-faithful-low-covered.md) | 无符号区间下界盖住 0 时可信：`i <= n` 的循环不再多估 2（误报） | 已实现 |
| [SPEC-0057](SPEC-0057-fptr-ledger.md) | 间接调用把指针交出去时，调用点记账 `fptr` | 已实现 |
| [SPEC-0058](SPEC-0058-local-fptr.md) | S9.1：局部函数指针当直接调用；多个目标时只判 POSSIBLE | 已实现 |
| [SPEC-0059](SPEC-0059-header-fn-hash.md) | 头文件里的内部链接函数：键加 `#<FIR 哈希前 8 位>`，跨 TU 不再用错函数体 | 已实现 |
| [SPEC-0060](SPEC-0060-unsupported-construct.md) | S5b-2：访问因不支持的构造进账本时，原因记 `unsupported_construct` 等 | 已实现 |
| [SPEC-0061](SPEC-0061-out-new-field.md) | 出参式分配器写进结构体字段：`out_new` 的 `target` 可以是 `pK->字段` | 已实现 |
| [SPEC-0062](SPEC-0062-unknown-arg-ledger.md) | 调用点实参是不跟踪的指针：记 `untracked_pointer`，告警不再露出占位符 | 已实现 |
| [SPEC-0063](SPEC-0063-unmapped-callee-symbols.md) | 调用点代换：没映射的被调者符号不再变成调用方的符号（错误的 SAFE） | 已实现 |
| [SPEC-0064](SPEC-0064-global-at-call.md) | 调用前改过的全局：调用点记录带 `gvals`，被调者的 `g:K` 不再当成入口值（错误的 SAFE） | 已实现 |
| [SPEC-0065](SPEC-0065-libc-strncpy-snprintf.md) | 内置模型补上 libc 的 `strncpy` / `snprintf` / `vsnprintf` | 已实现 |
| [SPEC-0066](SPEC-0066-recursion-ledger.md) | 递归调用点记账 `recursion`：经递归传上来的条件不再悄悄丢掉（错误的 SAFE） | 已实现 |
| [SPEC-0067](SPEC-0067-unmapped-text.md) | 告警文本：调用点上不知道的被调者符号按被调者的形参名写 | 已实现 |
| [SPEC-0068](SPEC-0068-text-placeholders.md) | 告警文本：字符串常量、调用点上不知道的字段值不再露出内部名字 | 已实现 |
| [SPEC-0069](SPEC-0069-range-field-args.md) | 实参所指结构体的字段值是区间时也带进调用点（经 `locals`） | 已实现 |
| [SPEC-0070](SPEC-0070-ret-ptr.md) | 效果 `ret_ptr`：被调者返回实参指针（或它所指结构体的指针字段）加偏移 | 已实现 |
| [SPEC-0072](SPEC-0072-callee-expr-fields.md) | 代入被调者的式子：字段符号按实参所指对象的当前值换，换不了算未知（错误的 SAFE） | 已实现 |
| [SPEC-0071](SPEC-0071-scalar-pointee.md) | 经指针传进来的整数：`*pK` 当成形参符号 `pK->_0` | 已实现 |
| [SPEC-0073](SPEC-0073-field-effect.md) | 写效应 `field`：被调者写形参所指对象的整数字段（含 `*pulLen`） | 已实现 |
| [SPEC-0074](SPEC-0074-field-effect-range.md) | 写效应 `field` 的值是几个常数之一时记区间 | 已实现 |
| [SPEC-0075](SPEC-0075-dependent-narrowing.md) | 条件边收窄后重算同一块里依赖它的局部：`while (n--)` 不再丢界 | 已实现 |
| [SPEC-0076](SPEC-0076-signed-bound-faithful.md) | 有符号区间的符号界按"溢出是未定义行为"认作可信：有符号递减循环收得住 | 已实现 |
| [SPEC-0077](SPEC-0077-global-effect.md) | 写效应 `global`：被调者写整数全局标量，调用方知道新值 | 已实现 |
| [SPEC-0078](SPEC-0078-f33-notes.md) | 告警 note 按 F-33：总链 note、每一跳说清做了什么、分配点 | 已实现 |
| [SPEC-0079](SPEC-0079-conditional-write-effect.md) | 只在某个返回值下成立的写效应（`ret == OK ⇒ pst->ulLen := …`） | 已实现 |
| [SPEC-0080](SPEC-0080-range-perf.md) | 性能：区间一遍求上下界、不透明项嵌套 ≤ 4 层、全局查表 | 已实现 |
| [SPEC-0081](SPEC-0081-pointer-compare.md) | 同一对象的两个指针比较按偏移收窄（`while (p < pEnd)`） | 已实现 |
| [SPEC-0082](SPEC-0082-ne-loop-threshold.md) | 循环头 widening 的符号阈值：`!=` 结束的循环（`i != n`、`q != p + n`） | 已实现 |
| [SPEC-0083](SPEC-0083-wide-ptr-compare.md) | 元素宽于 1 字节的指针比较：收窄出的上界不当代码给出的界（修 SPEC-0081 的 POSSIBLE 误报） | 部分被 SPEC-0084 取代 |
| [SPEC-0084](SPEC-0084-congruence.md) | 区间带同余（值 ≡ 锚点 mod 步长）：宽元素指针循环、步长大于 1 的整数循环判准 | 已实现 |
| [SPEC-0085](SPEC-0085-ne-threshold-body.md) | `!=` 阈值的候选扩到循环体里的比较（`while (1) { if (i == n) break; … }`） | 已实现 |
| [SPEC-0086](SPEC-0086-global-symbol-identity.md) | 全局符号的身份：内部链接带哈希后缀；被调者改了本模块看不到的全局要记账（修三处错误 SAFE） | 已实现 |
| [SPEC-0087](SPEC-0087-entry-constant-level.md) | 入口函数上深度 0 的常数 Req 按 F-10 / F-11 / F-12 判级（不再一律记"入口未证明"） | 已实现 |
| [SPEC-0022](SPEC-0022-member-array-parent.md) | 经形参访问成员数组：成员边界与外层对象边界都查（修 LV7-03 被证明成安全） | 已实现 |
