# SPEC-0031 跨 TU 的驱动 `oobsum/tools/oobsum_run.py`：本地跑三阶段、生成 bxen 命令、`status`

状态：已实现（S9-5）
来源：`STATUS.md` S9；06- §7 X-04（桩 TU 与只含它的 cdb）、X-05（report 的 cdb 从原 cdb 原样复制有判定的 TU 的条目）、X-10（每条命令先打印再执行、
输出存 `logs/<阶段>.log`、非零退出即停并提示单独重跑、`status` 子命令）；SPEC-0028…0030（三个 Phase）。

## 1. 要解决什么

三个 Phase 都有了，要有一个入口把它们串起来：本地用来回归、对照；公司用来生成 bxen 的三段命令。

## 2. 设计

```
oobsum_run.py local  --workdir W [--clang C] [--triple T] [--config K=V]… [--cc1-arg A]… 源文件…
oobsum_run.py bxen   --workdir W --bxen B --cc-json 原cdb目录 [--extra "…"] [--run]
oobsum_run.py status --workdir W
```

* **local**：① 每个源文件跑 `cc1 … Phase=export WorkDir=W`；② 写 `W/solve_stub/stub.c`，跑 `Phase=solve`；③ 读 `W/solved/*.json`，**判定不为空**的 TU
  （按文件里的 `file` 对回源文件）跑 `Phase=report`。cc1 的参数同 `run.py csa`（`-triple`、resource dir、ARM 加 `-fno-signed-char`）。
  结束打一行汇总：`export N 个 TU；solve 判定 K 条；report M 个 TU，告警 E 条`（告警数从各 TU 的判定日志 `emitted` 数）。
* **bxen**：不跑（除非 `--run`），只**打印**三段命令，并生成 `W/solve_stub/`（`stub.c` 与只含它的 `compile_commands.json`）、
  `W/report_cdb/compile_commands.json`（从原 cdb 原样复制 solved 里判定不为空的 TU 的条目，X-05）。命令模板：
  `B -exec_mode=onlySa -cc_json=<cdb 目录> [-analysis_file=<文件>] -checker=rb.enhance.ipa.OobSummary "-extra_args=-analyzer-config …:Phase=… -analyzer-config …:WorkDir=W <--extra>"`。
  全量模式（不带 `-analysis_file`）的写法**待用户确认**（STATUS"等用户提供"）。report 的 cdb 要在 solve 之后才能生成：先跑 export、solve，再用同一命令重跑一次生成 report 那段。
* **status**：`fir/`、`solved/`（其中判定不为空的）、`decisions/` 的文件数，`logs/` 里最后一个阶段与退出码；说明下一步该跑什么。
* 每条命令先打印（`$ …`）再执行，标准输出 / 错误追加到 `W/logs/<阶段>.log`；非零退出 → 停止，打印"单独重跑这一段"的命令。

## 3. 对召回与误报的影响

无（编排）。

## 4. 实现位置

`oobsum/tools/oobsum_run.py`（只依赖 Python 3 标准库）。

## 5. 测试

* lit `driver.test`：`local` 跑 `Inputs/solve/` 的两个 TU → 汇总行（export 2、solve 判定 4、report 2 个 TU）；`status`；`bxen` 生成桩 cdb 与 report cdb、打印三段命令。

## 6. 调试

`W/logs/*.log`；`status`。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

**等用户提供**：bxen 全量模式（跑整个 cdb）的命令格式——当前模板按"去掉 `-analysis_file`"猜的，不对就把正确的格式告诉我改模板。之后用 TestCase 目录跑一遍三段。

## 9. 待定 / 以后

* `run.py csa` 的跨 TU 用例（"需跨TU" 两个入口）改走三阶段：SPEC-0055（已做）。
