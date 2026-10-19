# SPEC-0112 驱动的 `stats` 与 `explain`（WP10；06- I-30、F-42）

状态：已实现
来源：WP10 驱动补全。06- I-30 的 `stats`（告警按级别 / 层数、账本按原因）、F-42 的 `explain <指纹 | 函数名>`（逐层摘要与代换过程，供误报研判）。
`decisions.py` 已有按标签 / 原因的计数表；`stats` 补层数、没报的原因、热点；`explain` 是新的。

## 2. 设计

两个子命令都加在 `oobsum/tools/oobsum_run.py`，只读 WorkDir（本地 `local` 跑的、公司 bxen 跑的都行），不调 clang：

* `stats --workdir W [--top N]`：判定总数 / 报出数 / TU 数；报出的 DEFINITE / POSSIBLE / SUSPECT 按 `max(depth, eff_depth)` 分桶（0、1、2、3、4+）；
  报出的 SUSPECT 按原因；没报的按 `标签 / not_emitted_because`；报出最多的函数、TU（前 N）。
* `explain --workdir W [--all] <指纹 | 函数名>`：按指纹或函数名找判定（默认不列 SAFE，`--all` 连 SAFE）。每条打印标签与原因、位置、指纹、文字、
  函数、报没报（没报的原因）、末端、大小、来源、层数、`dup_of`、调用链（逐层缩进）；跑的时候带了 `Dump=summary` 的，再列链上每个函数的摘要：
  形参名、各条 Req（读写、对象、方向、末端、大小、守卫、访问点）、各调用点（被调者、实参、局部的区间）。
  WorkDir 里没有 `dump/*/summary` 时提示加 `Dump=summary`。

耗时不在判定日志里：bxen 的耗时看它自己的输出；`local` 的看各阶段命令的耗时（以后需要再加）。

## 3. 对召回与误报的影响

只是工具。

## 4. 实现位置

`oobsum/tools/oobsum_run.py`（`cmd_stats`、`cmd_explain`、`_decisions`、`_summary_of`）；COMPANY.md §4。

## 5. 测试

lit `driver-tools.test`：`local --config Dump=summary` 跑两个 TU 之后，`stats` 的分桶表与小节、`explain Top_A` 的文字 / 调用链 / 两个函数的摘要、
找不到时退出码 1。

## 6. 调试

本身就是调试工具。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| I-30 `stats` / `explain` | `oobsum.py stats` / `explain` | `oobsum_run.py stats` / `explain`（SPEC-0112）；`stats` 不含耗时 |

## 8. 公司侧验证

bxen 跑完一个子目录（带 `Dump=summary`）后 `stats`、对一条 DEFINITE `explain`，看输出是否完整。

## 9. 待定 / 以后

`sqlite`（06- §4.6，可选）未做。
