# SPEC-0111 `run.py cdb` 与 `run.py csa --from-workdir`：公司用 bxen 跑 67 个用例、读现成的 WorkDir 对照（WP10 的 `check`）

状态：已实现
来源：WP10（07- §3）`check --cases`：把结果与用例头的 `@expect` 对照，验收用（06- I-30、§10）。`run.py csa` 只能自己调 cc1；公司是 bxen 跑，
要能拿 bxen 跑完的 WorkDir 直接对照，结论与本地 `run.py csa` 比（用户 2026-10-19：教我在公司怎么跑、怎么看、怎么反馈）。

## 2. 设计

* `run.py cdb [--cases 目录] [--out 目录] [--cc 编译器]`：给全部用例的每个源文件写一条 `compile_commands.json`
  （`directory` = 源文件所在目录，`command` = `<cc> -c <文件> -o /dev/null`）。公司把 `--cc` 换成自己的编译器 / 交叉编译器。
* `run.py csa --from-workdir <WD>`：不调 clang。
  * `WD` 是一个 WorkDir（有 `decisions/`），或者它的每个子目录各是一个 WorkDir（按子目录分开跑的；本地 `run.py csa --out` 的输出目录也是这样）。
  * 判定日志按文件头的 `file` 归到用例的源文件（按"相对用例根目录的路径"后缀匹配，公司的绝对路径与本地不同也认）；判定里的 `site`、调用链的路径同样换成相对路径。
    零条判定的 TU 也算跑到了。
  * 调用图从 `WorkDir/dump/<tu>/summary/<函数名>.json` 读（bxen 跑的时候带 `Dump=summary`；不带时"在入口能到达的函数里报出"这类认法用不上，记账类 TP 会显示漏报）。
  * 多文件用例按单 TU（inline）对照：跨 TU 的 TP 显示"需跨TU"（同 `--config Phase=…` 时）。对照、G2、与基线比回退都与 `run.py csa` 相同。
* 输出第一行 `== clang=WorkDir <WD> …`。

## 3. 对召回与误报的影响

只是验收工具。

## 4. 实现位置

`oobsum/cases/run.py`（`cmd_cdb`、`_workdirs`、`_load_workdir`、`_rel_path`、`_dump_summaries`、`cmd_csa` 的 `--from-workdir` 分支）；`oobsum/COMPANY.md` §3.1。

## 5. 测试

* lit `cases-csa.test`：直接跑完之后，`--from-workdir` 读同一个输出目录，合计与"回退 0 条"与直接跑的相同；`cdb` 写 6 个源文件。
* 本地模拟公司：把用例目录拷到别的路径、`run.py cdb`、逐个 TU 跑 cc1（`WorkDir` 同一个、`Dump=summary`）→ `--from-workdir`：通过 106、需跨TU 2（LV1-07、LV6-01）、
  级别不同 2、回退 0——与 `run.py csa`（三阶段跑跨 TU 用例，通过 108）只差这两个跨 TU 的入口。

## 6. 调试

`run.py csa --from-workdir` 的"运行错误"列出 WorkDir 里没有判定日志的源文件。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| I-30 `check --cases` | `oobsum.py check` | 由 `run.py csa --from-workdir` 实现（SPEC-0111） |

## 8. 公司侧验证

COMPANY.md §3.1：`run.py cdb` → bxen 跑用例（带 `WorkDir`、`Dump=summary`）→ `run.py csa --from-workdir`，预期通过 106、需跨TU 2、回退 0。
bxen 的 `-cc_json` 给目录还是文件、`-analysis_folder` 怎么写，按公司实际（COMPANY.md §4 的"待确认"）。

## 9. 待定 / 以后

跨 TU 的三阶段（export / solve / report）下的 WorkDir 也能直接读（solve 写的判定在 `decisions/`），未在公司验证。
