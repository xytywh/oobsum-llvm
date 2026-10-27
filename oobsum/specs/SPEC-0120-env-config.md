# SPEC-0120 环境变量 `OOBSUM_CONFIG` 传选项（clang_check 不用改）；`oobsum_run.py report`

状态：已实现
来源：用户 2026-10-27：公司的 clang_check（`main.py -e <bxen 目录> -j <cdb> -r <规则> -c <gbl_config.json>`）给每个文件拼的命令固定是
`bxen -exec_mode=onlySa -cc_json=<单文件 cdb> -analysis_file=<文件> -checker=<规则> -extra_args="-rbchecker-config=… -funcptr-config=… -analyzer-max-loop 3"`，
不做适配就传不进本规则的选项；告警由 clang_check 读每个进程的输出收进 report.json。问：不适配能不能跑、跨文件怎么跑。

## 2. 设计

1. **不传任何选项也能跑**：全用默认值（`Phase=inline`、不写 WorkDir），照样出告警（clang_check 照常收集），只是没有判定日志、跑不了跨文件。
2. **环境变量 `OOBSUM_CONFIG`**：内容是 `键=值`，空白或 `;` 分隔（`core/Policy` 的 `ParseKeyValues`；值可以为空、可以含 `=`；同一个键后面的为准）。
   键必须是本规则在 Checkers.td 里登记的选项（按 `AnalyzerOptions::Config` 里有没有 `<checker>:<键>` 判断，不另抄一份清单）。
   * 优先级：环境变量 > `-analyzer-config` > 默认值（AnalyzerOptions 里分不出"命令行写了默认值"和"没写"，所以环境变量统一优先）。
   * 写法不对（没有 `=`、键为空）、键不是本规则的选项 → 选项错误 `…:OOBSUM_CONFIG`；整数 / 布尔取值不对 → 该选项的选项错误（`(from OOBSUM_CONFIG)`），
     其余取值照原来的校验（`Phase`、`Dump`…）。
   * `Dump=options` 那行末尾加 ` OOBSUM_CONFIG="<原文>"`（没设就没有）。
   * clang_check 起的 bxen 进程继承环境变量：在 clang_check 命令前面写 `OOBSUM_CONFIG="WorkDir=… Phase=…" python3 main.py …` 即可，不改 clang_check。
3. 我们自己的驱动（`run.py`、`engine_check.py`、`oobsum_run.py`、`gen_chain.py`）启动时去掉 `OOBSUM_CONFIG`（打印一句"忽略"）：它们自己传选项，
   用户为 clang_check 设的环境变量不能混进回归。lit 不把它传给用例（lit 的环境是白名单）。
4. `oobsum_run.py report <report.json>`：clang_check 的报告里本规则的告警（`description` 是规则名，或 `reason` 里有 `[rb.enhance.ipa.OobSummary]`）
   按标签（`[OOB-<标签>…]`）、SUSPECT 按原因计数；只有数字，可以带回，也用来核对 clang_check 收全了没有（与 `stats` 的"报出"比）。
5. solve 的桩 cdb 改用 `command` 写法（与公司 TestCase 的 compile_commands.json 相同；bxen 自己解析 `-cc_json`）。
6. COMPANY.md 重排：§0 这次要做的（已完成的列表 + 本次步骤，命令写全）、§1 规则怎么跑（摘要、两种跑法、和 CSA CTU 的区别、选项怎么传、告警格式）、
   §2 各项操作的完整命令、§3 首次搭建（已完成）、§4 带回什么。CLAUDE.md §5 第 8 条补"做完的移进 §0 的已完成表"。

## 3. 对召回与误报的影响

无（没设环境变量时与原来逐字相同）。

## 4. 实现位置

`core/Policy.{h,cpp}`（`ParseKeyValues`）；`OobSummaryChecker.cpp`（`registerOobSummaryChecker`：`getString` / `getInteger` / `getBoolean`）；
`oobsum/cases/run.py`、`oobsum/tools/engine_check.py`、`oobsum/tools/oobsum_run.py`（`report`、桩 cdb）、`oobsum/cases/gen_chain.py`；COMPANY.md；CLAUDE.md。

## 5. 测试

* 单测 `Policy.ParseKeyValues`：空白 / `;` 分隔、值为空、值含 `=`、重复键、空串、没有 `=`、键为空。
* lit `env-config.c`：只靠环境变量写判定日志、出 3 层 DEFINITE、`Dump=options` 带原文；环境变量的 `Phase=export` 盖过命令行的 `Phase=inline`；
  未知键、没有 `=`、`Phase=bogus`、`MaxBlocks=abc` / `ReportSafe=yes` 报选项错误；没设时没有 `OOBSUM_CONFIG=`。
* lit `driver-tools.test`：`report` 数本规则 5 条（DEFINITE 1、POSSIBLE 1、SUSPECT 3、原因 not_in_tu 2 / entry_unproven 1），别的规则、空的 `{}` 不算。
* 设着 `OOBSUM_CONFIG="Phase=bogus"` 跑全部 lit（106 个全过）与 `run.py csa`（打印"忽略"，结果不变）。
* 本地模拟 clang_check 的默认命令（命令里只有 `-analyzer-max-loop 3`，选项全靠环境变量；67 个用例、8 路线程池）：A 告警 135、`not_in_tu` 5；
  B export → solve → report 告警 135、`not_in_tu` 0、跨文件的 LV1-07 / LV6-01 TP 报 DEFINITE；都没有失败。

## 6. 调试

`Dump=options` 的行尾；`OOBSUM_CONFIG="… Dump=options"` 跑一个文件看是否生效。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §4 选项 | `-analyzer-config <checker>:<键>=<值>` | 另：环境变量 `OOBSUM_CONFIG="键=值 …"`，优先于 `-analyzer-config`（SPEC-0120） |

## 8. 公司侧验证

COMPANY.md §0 清单 2026-10-27-1 第 2 步（bxen 直接跑，只靠环境变量）、第 3 步（经 clang_check 传到每个进程）。

## 9. 待定 / 以后

clang_check 若不继承环境变量（第 3 步会看出来）：在 clang_check 里给本规则的 `-extra_args` 加 `-analyzer-config`，或另想办法。
