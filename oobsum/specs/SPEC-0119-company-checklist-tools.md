# SPEC-0119 公司侧的清单与两个小工具：`oobsum_run.py cdb`、`stats --counts-only`

状态：已实现
来源：用户 2026-10-26 问"接下来在公司电脑上做啥、按什么顺序"。清单里有两处原来要手写脚本或容易带出业务信息：挑子系统要过滤 cdb、数重复文件；
`stats` 最后两张表列出报得最多的函数名和文件名（业务信息，不能带回）。

## 2. 设计

1. `oobsum_run.py cdb --cc-json <目录或文件> [--under <目录>]… [--out <目录>]`：打印条目数、文件数（`directory` + `file` 规范化后去重）、出现多次的文件数；
   `--under` 只留这些目录下的文件；`--out` 把过滤后的写到 `<目录>/compile_commands.json`（条目原样复制）。
2. `oobsum_run.py stats --counts-only`：只打印目标、标签 × 层数、SUSPECT 按原因、没报的按原因，不列函数名 / 文件名；最后一行注明，可以整段带回。
3. COMPANY.md §0：下一次在公司按顺序做的 8 步（更新编译 → TestCase → 用例对照 → 各架构的目标 → 挑子系统 → A → B → 带回的模板），每步写预期。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`oobsum/tools/oobsum_run.py`（`cmd_cdb`、`cmd_stats`）；lit `driver-tools.test`；COMPANY.md §0、§4.2。

## 5. 测试

lit `driver-tools.test`：`--counts-only` 有计数表、没有"报出最多的函数"；`cdb`：4 条（含一个重复、一个别的目录的文件）→ 文件 3 个、重复 1 个；
`--under` 过滤后 3 条、写出的文件 3 条。

## 6. 调试

无。

## 7. 对主规格的修订

无（工具）。

## 8. 公司侧验证

COMPANY.md §0 第 5 步、第 6 步。

## 9. 待定 / 以后

无。
