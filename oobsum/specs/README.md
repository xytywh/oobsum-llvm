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
| [SPEC-0022](SPEC-0022-member-array-parent.md) | 经形参访问成员数组：成员边界与外层对象边界都查（修 LV7-03 被证明成安全） | 已实现 |
