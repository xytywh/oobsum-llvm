# 公司侧：在公司电脑上同步、编译、跑 OobSummary

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen / clang_check 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。
>
> **怎么用这个文件**：每次需要你在公司做事，Claude 都会重写 §0（带清单版本号，CLAUDE.md §5 第 8 条）——`git pull` 后只看 §0、照着做、把最后的模板填好贴回来。
> §1 讲这个规则是怎么跑的；§2 是各项操作的完整命令（§0 里的步骤都来自这里）；§3 是首次搭建（已完成）。

## 0. 这次要做的

**清单版本：2026-10-27-1**　（`git pull` 后先核对版本号；做完一步再做下一步；结果不符合"预期"就停下，把那一步的输出贴回来）

所有命令在**终端（bash）**里执行。先设三个变量——**只有这里要按你那边改**，后面的命令原样粘贴：

```bash
OOB_CDB=/usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all        # 单板 cdb 所在的目录（aarch64_be ILP32 那个；换架构只改这一行）
OOB_SUB=<要试的子系统目录：OOB_CDB 覆盖的源码里，挑一个几百个文件的目录>
OOB_REPORT=<clang_check 写的 report.json 的完整路径：在 gbl_config.json 的 log_path 指的目录下>
```

### 已经做完、确认没问题的（不用再做）

| 项 | 带回的结果 |
|---|---|
| 同步、编译、lit、`run.py csa`（2026-10-27） | lit 105/105；通过 108、回退 0 |
| TestCase（2026-10-27） | 4 条告警，`decisions=7 emitted=4` |
| 67 个用例对照（2026-10-27） | 通过 106、需跨TU 2、G2 0、回退 0、"变化" 5 行——与预期相同 |
| 几个架构的目标（2026-10-27） | 三个 cdb 都是 `ptr=4 long=4`：32 位 ARM 大端、`aarch64_be…ilp32`、`i386`。fork 支持 AArch64 ILP32，字节布局可信 |
| fork-facts（2026-10-26） | 结论记在 STATUS |
| I0 探针 | 不用做（§3.3） |

### 第 1 步　更新、编译（约 20 分钟；这次改了 C++，BXPlugin 要重新编）

```bash
cd /usr1/oobsum-llvm && git pull && git log --oneline -3
ninja -C cmake-build-release clang
cmake-build-release/bin/llvm-lit -s cmake-build-release/tools/clang/test/Analysis/oobsum
python3 oobsum/cases/run.py csa
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

预期：`git log` 里能看到带 `OOBSUM_CONFIG` 的提交；lit `Passed: 106`；`run.py csa` 末尾 `通过 108，通过+S 20，级别不同 2，超出预期 4`、`回退 0 条`；
land.py 列出更新了 `OobSummaryChecker.cpp`、`OobSummary/core/Policy.h`、`Policy.cpp`（**不是**"无事可做"）；make 不报错。

### 第 2 步　bxen 直接跑 TestCase：选项只靠环境变量传（2 分钟）

命令里**没有**任何 `-analyzer-config rb.enhance.ipa.OobSummary:…`（和 clang_check 给每个文件拼的命令一样），选项全靠前面的 `OOBSUM_CONFIG`（§1.4）：

```bash
rm -rf /tmp/oobsum-tc-env
OOBSUM_CONFIG="WorkDir=/tmp/oobsum-tc-env Dump=options+stats" /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
ls /tmp/oobsum-tc-env/decisions
```

预期：第一行 `[oobsum:options] Phase=inline WorkDir="/tmp/oobsum-tc-env" …`，行尾是 `OOBSUM_CONFIG="WorkDir=/tmp/oobsum-tc-env Dump=options+stats"`；
`[oobsum:stats] … decisions=7 emitted=4`；4 条告警（与原来相同）；`ls` 列出 1 个 `.jsonl`。

### 第 3 步　clang_check 跑 TestCase：确认环境变量能经 clang_check 传到每个 bxen 进程（5 分钟）

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py cdb --cc-json /usr1/BXPlugin/cmake-build-release-release --under /usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker --out /usr1/oobsum-cdb/tc
rm -rf /usr1/oobsum-work/tc-cc
OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/tc-cc" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j /usr1/oobsum-cdb/tc/compile_commands.json -r rb.enhance.ipa.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work/tc-cc --counts-only | head -2
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py report "$OOB_REPORT"
```

* 第一条从 BXPlugin 的 cdb 里挑出 TestCase 那一个文件，写成一个小 cdb（预期打印"过滤后 1 条"）。`-e` 写你平时给 clang_check 的那个（bxen 所在的目录）。
* 预期：stats 第 2 行 `== 7 条判定，报出 4 条（1 个 TU）`；report 打印 `… rb.enhance.ipa.OobSummary 4 条：DEFINITE 2、POSSIBLE 1、SUSPECT 1`。
* **如果 report 里有 4 条、但 stats 说"没有判定日志"**：clang_check 没把环境变量传给 bxen——停下告诉我（那就要在 clang_check 里给这个规则的
  `-extra_args` 加选项，§1.4）。

### 第 4 步　看单板 cdb、做一个子系统的小 cdb（5 分钟）

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py cdb --cc-json $OOB_CDB
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py cdb --cc-json $OOB_CDB --under $OOB_SUB --out /usr1/oobsum-cdb/sub
```

预期：第一条打印全量的条目数、文件数、出现多次的文件数；第二条"过滤后 N 条"——N 几百为宜，太多就换一个小一点的目录。

### 第 5 步　子系统上跑 A：默认模式（单文件，§1.2）

```bash
rm -rf /usr1/oobsum-work/sub-a
time OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/sub-a" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j /usr1/oobsum-cdb/sub/compile_commands.json -r rb.enhance.ipa.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
cp "$OOB_REPORT" /usr1/oobsum-work/sub-a.report.json
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work/sub-a --counts-only
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py report /usr1/oobsum-work/sub-a.report.json
du -sh /usr1/oobsum-work/sub-a
```

预期：stats 第一行 `目标：aarch64_be-unknown-linux-gnuilp32 ptr=4 long=4 big-endian（K 个 TU）`，K 接近第 4 步的 N（少了的是失败 / 超时）；
report 的条数与 stats 里"报出"的条数相同或略少（clang_check 去重）。`cp` 那一步是因为下一次 clang_check 会覆盖 report.json。

抽查：在 `sub-a.report.json` 里 DEFINITE、POSSIBLE 各挑 5–10 条，你判断是不是真问题。只带回条数；误报用你自己的话描述是什么写法。

### 第 6 步　子系统上跑 B：跨文件（export → solve → report，§1.2）

```bash
rm -rf /usr1/oobsum-work/sub-b
time OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/sub-b Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j /usr1/oobsum-cdb/sub/compile_commands.json -r rb.enhance.ipa.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py status --workdir /usr1/oobsum-work/sub-b
/usr/bin/time -v python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir /usr1/oobsum-work/sub-b --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" --run
time OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/sub-b Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j /usr1/oobsum-cdb/sub/compile_commands.json -r rb.enhance.ipa.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
cp "$OOB_REPORT" /usr1/oobsum-work/sub-b.report.json
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work/sub-b --counts-only
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py report /usr1/oobsum-work/sub-b.report.json
du -sh /usr1/oobsum-work/sub-b
```

预期：
* export 那遍不出告警；`status` 里 `fir/` 的个数接近第 4 步的 N。
* solve 打印 `solve 完成：solved/ N 个 TU，判定 … 条`；记下 `time -v` 输出里的 `Elapsed (wall clock) time` 和 `Maximum resident set size`。
* report 那遍出告警；和 A 比，stats 的"报出的 SUSPECT：按原因"里 `not_in_tu` 应当少很多（跨文件的调用都代换上了）。

### 第 7 步　带回来（照这个格式填，贴进对话）

```text
清单版本：2026-10-27-1
1. git log --oneline -1：
2. lit / run.py csa：Passed __；通过 __、回退 __；land.py 更新了 __ 个文件
3. 第 2 步：options 行尾有没有 OOBSUM_CONFIG="…"：__；decisions=__ emitted=__；告警 __ 条
4. 第 3 步：stats 第 2 行：__；report 那一行：__
5. 第 4 步：全量 __ 条、文件 __ 个、重复 __ 个；子系统 __ 条
6. A：time 的 real __；stats --counts-only 全文；report 那两行；du __
7. B：export 的 real __；solve 的 Elapsed __、Maximum resident set size __；report 的 real __；stats --counts-only 全文；report 那两行；du __
8. 抽查：DEFINITE 看了 __ 条、真问题 __ 条；POSSIBLE 看了 __ 条、真问题 __ 条；误报是什么写法（自己的话）
9. 有没有崩溃 / 报错：哪一步、退出码、崩溃栈里带 OobSummary 的几行
```

全量（整个 cdb）和选项对照（§2.7）等这些数回来、我看过再做。

## 1. 这个规则是怎么跑的

### 1.1 摘要是什么

每个函数**只分析一次**，得到它的摘要：它对实参的要求（例如"写 `p[0 … n]`，要求 `n < p 指向的对象大小`"）和它对外的效果（写了哪个成员、返回值的范围……）。
在每个调用点把**实参代进**被调者的摘要：能判的就地判（越界 → DEFINITE / POSSIBLE，证明了 → SAFE）；判不了的，这条要求换成调用者的形参，变成调用者的要求，
继续往上传——直到碰到给出缓冲区和长度的那一层。所以 3 层、5 层、20 层的链都一样处理（本地测过 20 层），告警报在"界和长度第一次同时已知"的那个调用点，
note 里列出整条调用链。

每条判定都带标签：DEFINITE（明确越界）/ POSSIBLE（在代码给出的界下可能越界）/ SUSPECT（证明不了安全，带原因）/ SAFE（证明了安全，默认不报）。
1–2 层就能判的 DEFINITE / POSSIBLE 默认不报（交给现有规则，`InlineDepth=2`）；SUSPECT 任何层数都报（宁可多报）。

### 1.2 两种跑法

| | A 默认模式（`Phase=inline`） | B 跨文件模式（`export` → `solve` → `report`） |
|---|---|---|
| 怎么跑 | 一个文件一个进程，一条命令——和其他单文件规则一样，clang_check 跑一遍 | clang_check 跑两遍（export、report），中间一条 solve（单进程） |
| 跨函数 | 同一个文件里任意层数 | 任意层数 |
| 跨文件 | 被调者在别的文件里：这次调用记 SUSPECT `not_in_tu`；没有调用者的对外函数当入口，它的要求证明不了就记 SUSPECT `entry_unproven` | solve 把所有文件的函数放进一张调用图，被调者先算，跨文件的调用点照样代换实参 |
| 告警 | 每个进程直接打印 | report 那一遍每个进程打印自己文件里的 |
| 适合 | 先看一遍：会不会崩、多快、大概多少 | 正式结果 |

B 的三遍：**export** 每个文件把自己的函数翻成中间表示写进 `WorkDir/fir/`（不分析、不报，比 A 快）；**solve** 一个进程读全部 `fir/`，按全局调用图算摘要、
在所有调用点判定，结果按文件写进 `WorkDir/solved/`；**report** 每个文件读自己的 `solved/<文件>.json`，在源码位置上报告警（只解析源码、不分析）。

### 1.3 和 CSA 原来的跨文件（CTU）有什么不一样

* CSA：路径敏感的符号执行，从每个顶层函数往下走；遇到别的文件的函数，就把那个文件的 AST 导入进来接着走。路径数随分支和调用层数指数增长，
  靠 `max-nodes`、inline 深度截断——深的链会被截掉，这也是现有规则只覆盖 1–2 层的原因。
* 本规则：**不做路径敏感执行**（只挂 TU 级的 `check::ASTDecl<TranslationUnitDecl>`，不跑 ExprEngine）；每个函数在 CFG 上做一次抽象解释（循环用加宽收敛）得到摘要；
  跨文件时**不导入 AST**，只交换每个文件导出的中间表示；solve 按调用图自底向上算、在调用点代换。代价大致和代码量成正比，链多长都行。

### 1.4 选项怎么传：clang_check 不用改

clang_check 给每个文件拼的命令里只有 `-rbchecker-config …`、`-analyzer-max-loop 3`，没有本规则的选项——本规则就全用默认值：A 默认模式、不写 WorkDir。
**这样也能跑、照样出告警**，只是没有判定日志（`stats` 看不了），也跑不了 B。

要写判定日志或者跑 B，在 clang_check 的命令**前面**加环境变量 `OOBSUM_CONFIG`（SPEC-0120），clang_check 起的每个 bxen 进程都会继承它：

```bash
OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/xxx Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py …
```

* 写法：`键=值`，空格（或 `;`）分隔；键就是本规则的选项名（`Phase`、`WorkDir`、`Dump`、`CharAlias`…，见 §2.7 与 `clang/test/Analysis/oobsum/README.md`）。
* 优先级：环境变量 > `-analyzer-config` > 默认值。写错键名或取值，和命令行写错一样报选项错误（每个文件都会报，一眼能看到）。
* `Dump=options` 打印的那行末尾会显示 `OOBSUM_CONFIG="…"` 的原文，确认它生效了。
* 写在命令前面只对这一条命令有效；**不要 `export`**（否则后面所有 bxen 都带着它）。我们自己的脚本（`run.py`、`oobsum_run.py`…）会忽略它。
* 想改 clang_check 也行：在这个规则的 `-extra_args` 里加 `-analyzer-config rb.enhance.ipa.OobSummary:WorkDir=…` 等，效果相同。

### 1.5 告警长什么样、report.json

和其他 CSA 规则的格式一样，clang_check 照常收进 report.json：

```text
<文件>:<行>:<列>: warning: [OOB-<标签>[:<原因>]] <英文正文> [rb.enhance.ipa.OobSummary]
<文件>:<行>:<列>: note: Call chain: A (a.c:10) -> B (b.c:20) -> C (c.c:30)
```

标签在方括号里（`[OOB-DEFINITE]`、`[OOB-SUSPECT:not_in_tu]`），可以按它筛。调用链在 note 行里；clang_check 不收 note 的话报告里只有第一行，
要看链和中间的摘要就用 §2.8。`oobsum_run.py report <report.json>` 按标签 / 原因数本规则的告警（只有数字，可以带回）。

## 2. 各项操作（参考；§0 的步骤都来自这里）

### 2.1 更新、同步、编译

```bash
cd /usr1/oobsum-llvm && git pull && git log --oneline -3
ninja -C cmake-build-release clang                                                         # 本地那份 clang（lit 和 run.py 用）
cmake-build-release/bin/llvm-lit -s cmake-build-release/tools/clang/test/Analysis/oobsum
python3 oobsum/cases/run.py csa
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin          # 先看：要拷 / 删的文件与登记的 diff
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply  # 再落地
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

`land.py` 做的是 `csa-checker-dev` 的五处落点，并且是**同步**（改过的覆盖、多出来的旧文件删掉、登记整段替换；重复执行不会重复加）：

| 本仓库 | BXPlugin |
|---|---|
| `clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp`、`OobSummary/**` | `xir/llvm_ast/StaticAnalyzer/Checkers/` 同名路径（`OobSummary/` 里多出来的旧文件会删掉） |
| 上面的 `.cpp` | `xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` 的 `add_library()` 末尾追加；`target_include_directories` 补 `${CMAKE_CURRENT_SOURCE_DIR}`（SPEC-0114） |
| `Checkers.td` 里的 `def OobSummaryChecker` | `xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` 的 `RosaRBEnhanceIpa` 块最后（已有则整段替换） |
| `oobsum/testcase/OobSummaryChecker/` | `xir/llvm_ast/TestCase/OobSummaryChecker/`，并在 `TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(OobSummaryChecker)` |

找不到预期结构（比如包块结尾不是 `} // end "rb.enhance.ipa"`）会停下来，不猜；把报错贴回来改脚本。
改了 `Checkers.td` 后如果新选项不生效，多半是 `Checkers.inc` 没重新生成：做一次完整编译（`csa-checker-dev` 的 TableGen 注意事项）。

### 2.2 TestCase

```bash
rm -rf /tmp/oobsum-tc && mkdir -p /tmp/oobsum-tc
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/oobsum-tc -analyzer-config rb.enhance.ipa.OobSummary:Dump=options+stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /tmp/oobsum-tc
```

预期：4 条告警——调试内建函数的 `[OOB-DEFINITE]`、`[OOB-POSSIBLE]`、`[OOB-SUSPECT:entry_unproven]`，加 `Tc_Engine_TP` 处的
`[OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain`（带调用链 note）；`[oobsum:stats] … decisions=7 emitted=4`；
`decisions.py` 显示 7 条判定，其中 3 条没报（2 条 `safe`、1 条 `inline_depth`）。每个版本的预期写在 `TC_OobSummaryChecker.c` 的文件头。

### 2.3 67 个分级用例：bxen 跑一遍，和本地的 `run.py csa` 对照（SPEC-0111）

```bash
cd /usr1/oobsum-llvm
python3 oobsum/cases/run.py cdb --out /usr1/oobsum-cases-cdb --cc /usr/local/bin/cc
rm -rf /usr1/oobsum-cases-wd && mkdir -p /usr1/oobsum-cases-wd
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/oobsum-cases-cdb/ -analysis_folder=/usr1/oobsum-llvm/oobsum/cases -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-cases-wd -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 oobsum/cases/run.py csa --from-workdir /usr1/oobsum-cases-wd
```

预期：`合计 134 个入口：通过 106，通过+S 20，级别不同 2，超出预期 4，需跨TU 2`；G2 那行 `0 个`；`回退 0 条`；"变化"5 行——LV1-07、LV6-01 各两行
（单文件跑，要跨文件）、LV9-02 一行（64 位上多 2 条 SUSPECT）。

* 第一条给用例生成 cdb：`--cc` 用和 TestCase 同一个编译器；bxen 不会自己找 gcc 的系统头文件（`stddef.h` 等），`run.py cdb` 用 `<cc> -E -x c - -v`
  问出这些目录、按顺序写成 `-isystem`（SPEC-0115）。还缺参数用 `--flags="…"` 追加（要带等号，因为值以 `-` 开头）。
* `Dump=summary` 必须带（对照要用调用图）。
* rb.enhance.ipa 规则：`-extra_args` 里**不加** `-analyzer-config ipa=none`；`-analyzer-config` 的列表值用 `+` 连接（`Dump=options+stats`），逗号会被当成分隔符。

### 2.4 看分析时用的目标（SPEC-0117）

每次运行都会在 `WorkDir/fir/<tu>.json` 里记下 bxen 分析这个文件时用的目标（不用加 `Dump`），`stats` 的第一行就是它：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir> | head -1
# 目标：<三元组> ptr=<指针字节数> long=<long 字节数> <big-endian 大端 | little-endian 小端>（K 个 TU）
```

只想看某个架构：用 §2.8 单独跑那个架构 cdb 里的任意一个文件（`-cc_json=<那个 cdb 的目录>/`），再对它的 WorkDir 执行上面这条。
已测（2026-10-27）：三个 cdb 分别是 `armebv4t-unknown-linux-gnueabi`、`aarch64_be-unknown-linux-gnuilp32`、`i386-unknown-linux-gnu`，都是 `ptr=4 long=4`。

* **布局本来就会影响结果，这是对的**：越界按字节算，同一个结构体在 32 位上 8 字节、在 64 位上 16 字节，`memcpy_s(&st, 8, …)` 在一个架构上安全、在另一个上越界。
  要保证的是"每个 cdb 都按它自己真实的目标算"，不是"几个架构结果一样"；几个架构都要看就各跑各的、各用一个 WorkDir，以产品实际运行的架构为准。
* 看到 `x86_64 … ptr=8` 而那个 cdb 是单板产品：bxen 按宿主算了，含指针 / `long` 的结构体会算大——把这一行带回来。

### 2.5 用 clang_check 跑（cdb 每个文件一个 bxen 进程、线程池并行，SPEC-0118 / 0120）

选项靠命令前的 `OOBSUM_CONFIG`（§1.4）。**每一轮用一个新的空 WorkDir**（旧文件会混进 solve 和统计）；每次 clang_check 跑完把 report.json 复制一份（下一次会覆盖）。

```bash
# A 默认模式
OOBSUM_CONFIG="WorkDir=<WorkDir>" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j <cdb 目录>/compile_commands.json -r rb.enhance.ipa.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
# B 跨文件：export 一遍 → solve → report 一遍（同一个 WorkDir、同一个 cdb）
OOBSUM_CONFIG="WorkDir=<WorkDir> Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py …（同上）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir <WorkDir> --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" --run
OOBSUM_CONFIG="WorkDir=<WorkDir> Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py …（同上）
```

* solve 不加 `--run` 只打印它要执行的那条 bxen 命令。`oobsum_run.py status --workdir <WorkDir>` 看 `fir/`、`solved/`、`decisions/` 各有多少。
* A 跑过的 WorkDir 里已经有 `fir/`（设了 WorkDir 就会写），可以跳过 export 直接 solve——但 report 会覆盖 `decisions/`，要留 A 的结果就先复制一份。
* solve 在 1 万个文件上还没实测（目标 ≤ 1 小时、≤ 64 GB）：先在子系统上量，再全量。
* 磁盘：WorkDir 大约是源码大小的 10 倍（`fir/` 缩进写占大头）。全量不要加 `Dump=summary`（`dump/` 还要再大 2 倍）。
* cdb 里同一个文件出现多次（不同宏各编一次）：告警不受影响，判定日志 / FIR 只留最后写的那份；`oobsum_run.py cdb --cc-json <cdb 目录>` 数有多少。
* 进程被超时杀掉：只会留下 `*.tmp`，不会有写了一半的 `.json`。
* 不用 clang_check 时：`oobsum_run.py bxen --workdir … --bxen … --cc-json <目录> --analysis-folder <目录> --extra "…"` 打印一条 bxen 跑几个目录的三段命令（试跑用）。

### 2.6 看结果

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir> --counts-only   # 目标、标签 × 层数、SUSPECT 按原因、没报的按原因（只有计数，可以带回）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir>                 # 再加报得最多的函数 / 文件（业务信息，只给自己看）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py report <report.json>                      # clang_check 的报告里本规则的告警按标签 / 原因计数
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py <WorkDir> --reason not_in_tu               # 某个原因的判定逐条列出（默认只列报了的）
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py <WorkDir> --emitted --jsonl review.jsonl   # 报了的判定导出给大模型复核
```

### 2.7 选项对照（D25：取舍都做成了选项，按实测选）

同一个子系统、**每种取值一个 WorkDir**，在 `OOBSUM_CONFIG` 里多加一个键，比 `stats --counts-only`：

| 跑哪一遍 | `OOBSUM_CONFIG` 里加的 | 看什么 |
|---|---|---|
| 基线（默认） | （不加） | 计数表作对照 |
| char 写别名 | `CharAlias=true`、`CharAlias=verify` | SUSPECT 条数；`verify` 下 `alias_unverified` 的条数，抽几条看是否真有别名（SPEC-0103 / 0106） |
| 内层界 | `Subobject=label`、`Subobject=off` | `subobject_overflow` 条数；`label` 下内层的 DEFINITE / POSSIBLE（SPEC-0104 / 0105） |
| 不回绕假设 | `AssumeNoWrap=true` | SUSPECT 少了多少；抽几条打开后不再报的，看检查是否确实不会回绕（SPEC-0108） |
| 1–2 层也报 | `InlineDepth=0` | 和现有规则重叠的 DEFINITE / POSSIBLE 有多少 |
| 只报某些 SUSPECT | `SuspectReasons=not_in_tu+entry_unproven` | 只留这几个原因的 SUSPECT（其余照样写进判定日志） |

业务的分配器、拷贝封装、纯函数（07- 输入 I5 / I8 / I9）写进模型文件：复制 `oobsum/config/oob_models.example.yaml` 改，`OOBSUM_CONFIG` 里加 `ModelFile=<路径>`；
`Dump=models` 看实际生效的表（SPEC-0033）。模型文件留在公司，不带回。

### 2.8 单独跑一个文件、看一条判定的来龙去脉

```bash
rm -rf /usr1/oobsum-work/one && mkdir -p /usr1/oobsum-work/one
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=<cdb 目录>/ -analysis_file=<那个 .c> -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work/one -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+apply -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=<函数名正则> -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py explain --workdir /usr1/oobsum-work/one <函数名或指纹>
```

`explain` 列出这条判定的调用链和链上每个函数的摘要（`Dump=summary` 跑的才有）。跨文件的判定：对 B 的 WorkDir 执行 `explain`（solve 时的摘要在 solve 那一步的输出里）。

### 2.9 撤掉

```bash
python3 /usr1/oobsum-llvm/oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --remove --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

## 3. 首次搭建（已完成；换机器时才用）

### 3.1 只取需要的部分（不用拉整个 llvm-project 源码）

```bash
cd /usr1
git clone --filter=blob:none --no-checkout --branch oobsum/dev <你的私有仓库地址> oobsum-llvm
cd oobsum-llvm
git sparse-checkout set --no-cone /CLAUDE.md /oobsum/ /clang/include/clang/StaticAnalyzer/Checkers/Checkers.td /clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp /clang/lib/StaticAnalyzer/Checkers/OobSummary/ /clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt /clang/test/Analysis/oobsum/
git checkout oobsum/dev
```

部分克隆只下载检出文件的内容（几 MB），提交历史照常可看（`git log -p -- oobsum/specs/` 看规格的演进）。要在公司跑 lit 和 `run.py csa`，还要按
`oobsum/devenv/30-build-llvm.sh` 编一份本仓库的 clang（全量检出）。

### 3.2 fork-facts（2026-10-26 已做）

`bash oobsum/devenv/fork-facts.sh > fork-facts.txt`：公司 clang 的版本、llvm-project-14 是否上游 14.0.0、构建开关、xir 内嵌 CSA 与上游的差异计数
（只读，不输出业务代码；贴回之前自己过一眼）。

### 3.3 I0 探针（不用做）

写正式代码之前的摸底（`oobsum/probes/i0-integration/README.md`）。它要确认的事已由 TestCase、fork-facts、§2.4 确认（SPEC-0117）；想要全量的规模数据时，
直接用 §2.5 跑正式规则 + `stats` 即可。

## 4. 带回来什么

* 只要计数和结论：§0 最后的模板、`stats --counts-only`、`oobsum_run.py report`、耗时、我们自己代码的编译报错 / 崩溃栈。
* **不带**业务代码、函数名、文件名、告警原文；要说明某类误报 / 漏报，用自己的话描述写法，或写一个不含业务代码的最小例子。
* 在 WSL 里贴进对话（开头写清单版本号，`oobsum/PROMPTS.md` 的 C 段），Claude 据此修改、再重写 §0。
