# SPEC-0118 用 clang_check 跑全量：几十个进程并行写同一个 WorkDir；`oobsum_run.py solve`

状态：已实现
来源：用户 2026-10-26：公司的单文件规则都用 clang_check 跑——先加工全量 `compile_commands.json`（加参数、头文件），再把每个文件拼成一条
`bxen -cc_json=… -analysis_file=…`，线程池并行（并发度约等于 CPU 核数，约 40）。问本规则怎么用 clang_check 跑全量。

## 2. 设计

### 2.1 跑法（COMPANY.md §4.2）

* **默认模式（单 TU）**：与其他规则相同，clang_check 跑一遍；`-extra_args` 带 `WorkDir`（所有进程同一个绝对路径，每轮一个新的空目录）。
* **跨文件（06- §7）**：clang_check 跑两遍、中间一条 solve——第 1 遍 `Phase=export`（每个 TU 写 `fir/`，不跑引擎、不报）；
  `oobsum_run.py solve` 生成桩 TU、打印 / 执行 solve 那一条 bxen 命令（单进程读全部 FIR，写 `solved/`，没有判定的 TU 也写空列表）；
  第 2 遍 `Phase=report`，用同一个全量 cdb（每个 TU 读自己的 `solved/<tu>.json` 报告警，不跑引擎）。SPEC-0031 §8 的"bxen 跑多个文件的写法"由此定为
  clang_check 的逐文件写法（`-cc_json` + `-analysis_file`）；`oobsum_run.py bxen` 的 `-analysis_folder` 写法只用于挑子目录试跑。
* 默认模式设了 WorkDir 时也写 `fir/`（X-06），跑过默认模式的 WorkDir 可以直接 solve（第 2 遍会覆盖 `decisions/`）。

### 2.2 并行写同一个 WorkDir

每个 TU 的文件名按 TU id（主文件路径的哈希）区分，不同 TU 不会写同一个文件。两种情况会：cdb 里同一个文件出现多次（同时被两个进程分析）、
进程写到一半被超时杀掉。WorkDir 里的所有文件（`fir/`、`solved/`、`decisions/`、摘要文件、`dump/`）统一经 `core/AtomicFile.h` 的 `WriteFileAtomic` 写：
临时文件 `<路径>.<进程号>.tmp`（原来是固定的 `<路径>.tmp`，两个进程会写进同一个临时文件；判定日志与 dump 原来直接写），写完 `rename`（原子）；
失败时删掉临时文件。读的一方按 `.json` / `.jsonl` 扩展名找文件，看不到 `.tmp`。同一个文件出现多次时，判定日志 / FIR 留后改名的那份
（告警不受影响：每个进程照常报）。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`core/AtomicFile.h`（新，只有头文件）；`core/FirJson.cpp`（`WriteModuleFile`）、`core/Summary.cpp`（`WriteSummaryFile`）、`core/DecisionLog.cpp`、
`debug/DumpSink.cpp`、`OobSummaryChecker.cpp`（`solved/`）；`oobsum/tools/oobsum_run.py`（`solve` 子命令、`write_stub_cdb`）；COMPANY.md §4、§4.2；06- X-01 注明。

## 5. 测试

* 单测 `AtomicFile.WritesAndRenames`（临时文件名带进程号、写完只剩目标文件、再写覆盖）、`AtomicFile.FailureLeavesNothing`。
* lit `parallel-workdir.test`：8 个 cc1 进程同时分析同一个文件、写同一个 WorkDir：退出码都是 0、判定日志 / FIR / dump 都能解析、没有 `.tmp`。
* lit `driver.test`：`solve` 只打印 solve 那一条；`fir/` 为空时提示先 export。
* 本地模拟 clang_check（67 个用例的 cdb、8 路线程池，cc1 不给 resource-dir）：export 70 个进程 0 失败 → solve → report 70 个进程 0 失败、告警 135 条；
  跨文件的 LV1-07、LV6-01 TP 报 DEFINITE、TN 为 SAFE；没有 `.tmp`。

## 6. 调试

`oobsum_run.py status --workdir W`（`fir/`、`solved/`、`decisions/` 各多少）；`stats` 的"目标："一行的 TU 数 = 成功分析的 TU 数，与 cdb 的文件数比。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| X-01 | 紧凑 JSON，先写 `.tmp` 再改名 | 缩进 JSON（SPEC-0114），先写 `<路径>.<进程号>.tmp` 再改名（SPEC-0118） |

## 8. 公司侧验证

先在一个子系统上（cdb 过滤出一部分文件）用 clang_check 跑 COMPANY.md §4.2 的 A 与 B，带回：文件数、各遍耗时、solve 的耗时与峰值内存
（`/usr/bin/time -v` 的 `Elapsed` 与 `Maximum resident set size`）、WorkDir 大小（`du -sh`）、`stats` 的计数。

## 9. 待定 / 以后

solve 在 1 万个 TU 上的耗时与内存未实测（06- N-03 的目标：≤ 1 小时、≤ 64 GB）；超了再做 X-07（按 SCC 分批加载）。
