# 公司侧：在公司电脑上同步、编译、跑 OobSummary

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen / clang_check 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。
>
> **怎么用这个文件**：每次需要你在公司做事，Claude 都会重写 §0（带清单版本号，CLAUDE.md §5 第 8 条）——`git pull` 后只看 §0、照着做、把最后的模板填好贴回来。
> §1 讲这个规则是怎么跑的、告警怎么看；§2 是各项操作的完整命令（§0 里的步骤都来自这里）；§3 是首次搭建（已完成）。

## 0. 下一次在公司按顺序做

**清单版本：2026-11-10-3**

本节是一轮完整验证的执行流程。每步按“目的 → 命令 → 正确预期 → 异常时保留与反馈”执行；遇到异常停在该步，不继续后续分析。
历史数据只作比较，本轮从第1步开始，用新的WorkDir保存本轮结果。重复运行核对时，每轮保留完整WorkDir与报告副本、提交号及选项概括，不能只留最终告警总数。第9步尾数组对照另有一次solve/report；第5步A/B/C只选一种，不要三种都跑。

### 路径与日志：开始前设置一次

路径只改这里；OOB_REPO改成公司实际克隆位置。OOB_RUN_TAG每一轮换一个未用过的值，OOB_WD是含fir/solved/dump的根目录，OOB_REPORT必须指向报告JSON文件。
函数名不在这里设置，到第5步选择少量保存或第6步查看时再填。

```bash
OOB_REPO=/usr1/oobsum-llvm
OOB_ROOT=/usr1/BXPlugin
OOB_BUILD=/usr1/BXPlugin/cmake-build-release-release
OOB_CDB=/usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all
OOB_GBL_OLD=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
OOB_GBL=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/gbl_config.json
OOB_REPORT=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/log/allfiles-buffer_overflow_report.json
OOB_INI=/usr1/clang_check/src/cfg/ini/config.ini
OOB_FUNCPTR=/usr1/clang_check/src/cfg/ini/config_funcptr.ini
OOB_RUN_TAG=20261110-r3
OOB_WD=/usr1/oobsum-work/full-b-$OOB_RUN_TAG
OOB_LOG=$OOB_WD/validation
OOB_TC_WD=$OOB_WD/testcase
OOB_REPORT_COPY=$OOB_WD/report.json
OOB_COMPARE=$OOB_WD.flex-on
OOB_COMPARE_LEFT=/usr1/oobsum-work/full-b-run-a
OOB_COMPARE_RIGHT=/usr1/oobsum-work/full-b-run-b
set -o pipefail
if [ -e "$OOB_WD" ] || [ -e "$OOB_COMPARE" ]; then
    printf 'WorkDir已存在，请更换OOB_RUN_TAG；保留旧目录。
'
    false
else
    mkdir -p "$OOB_LOG" && cd "$OOB_REPO"
fi
```

**正确预期：** 创建一个新目录及validation/；此前业务结果保留。目录已存在或无写权限时，先修正路径/轮次，不执行下一步。
构建、分析和统计命令块用tee实时显示并保存输出，最后写同名.exit：command_exit是该块命令退出码，log_exit是tee退出码，**两者都应为0**。
块内用`&&`串联，前一个命令失败就不继续该块。任一退出码非0，或下面的内容预期不满足，都应停下；不要只看进程exit=0就认为覆盖完整。
报告和汇总也可能较大；所有原始日志、FIR、solved、摘要、decisions和报告JSON保留在公司，不直接打包带回。
能带回的是版本/退出码/计数、目标ABI、progress与summary/status纯计数、自有测试的错误、本规则栈的非业务部分；业务名字、路径、源码和告警正文留在公司。

### 已经做完、确认没问题的基准

| 项目 | 已确认结果（不替代本轮预期） |
|---|---|
| 公司完整lit | 2026-11-05公司确认更新后通过；当前套件预期119项 |
| C++健壮性验证 | Release/Debug均exit=0，无断言；242条判定/222报出 |
| IPA对照 | 自有TestCase加/不加ipa=none结果一致；本轮统一显式ipa=none |
| 历史全板cdb | 10686条、10262个文件、261个文件多次出现；C6689、C++3573 |
| 历史export/solve/report | 10245个FIR；solve19m49.53s/29.4 GiB；报告629741条；各数值随输入与版本变化 |
| 最近一次solve与摘要 | FIR/solved10257；2231627条判定、9884个TU非空；25m07.76s/45.2 GiB；两函数local/derived可读 |

### 第1步：更新仓库、构建并验证自有用例

**目的：** 确认使用同一提交，先通过仓库自有回归，再分析业务。

```bash
{
git -C "$OOB_REPO" branch --show-current &&
test "$(git -C "$OOB_REPO" branch --show-current)" = oobsum/dev &&
git -C "$OOB_REPO" pull --ff-only &&
git -C "$OOB_REPO" log --oneline -1 &&
ninja -C "$OOB_REPO/cmake-build-release" clang &&
"$OOB_REPO/cmake-build-release/bin/llvm-lit" -sv "$OOB_REPO/cmake-build-release/tools/clang/test/Analysis/oobsum" &&
python3 "$OOB_REPO/oobsum/cases/summaries/replay.py" &&
python3 "$OOB_REPO/oobsum/cases/run.py" csa &&
python3 "$OOB_REPO/oobsum/devenv/land.py" --drop "$OOB_REPO/oobsum/landing/OobSummaryChecker.manifest" --check-tree
} 2>&1 | tee "$OOB_LOG/01-local-checks.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/01-local-checks.exit"
```

**正确预期：** 分支oobsum/dev；构建成功；lit通过119项；replay为0处不一致；csa回退0、TN强告警0；check-tree一致。
**异常时：** 保留01-local-checks.log/.exit及对应lit Output目录。反馈提交号、失败测试名和完整RUN/expected/input或Python断言段；这些是自有用例，可原样带回。分支不符、拉取冲突或构建错误也停在此步处理。

### 第2步：同步到BXPlugin、编译并核对规则配置

**目的：** 让公司bxen使用本次提交的checker，并让clang_check使用core规则名和正确的报告路径。

```bash
{
python3 "$OOB_REPO/oobsum/devenv/land.py" --drop "$OOB_REPO/oobsum/landing/OobSummaryChecker.manifest" --into bxplugin --root "$OOB_ROOT" --apply &&
make -C "$OOB_BUILD" -j16 &&
mkdir -p "$(dirname "$OOB_GBL")" &&
if [ ! -f "$OOB_GBL" ]; then cp "$OOB_GBL_OLD" "$OOB_GBL"; fi &&
python3 -c 'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text(p.read_text().replace("rb.enhance.ipa.OobSummary", "rb.enhance.core.OobSummary"))' "$OOB_GBL"
} 2>&1 | tee "$OOB_LOG/02-bxplugin.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/02-bxplugin.exit"
```

**正确预期：** land登记一致，编译退出0；只有一个OobSummary checker def，位于core包。检查公司配置：规则名为rb.enhance.core.OobSummary、按core/cmn执行，不放入IPA_CMN_RULE_CHECKER；gbl_config的log_path与OOB_REPORT一致。保留其它规则配置。
**异常时：** 保留02-bxplugin.log/.exit、同步清单及编译报错；带回本规则的缺文件/重复def/unknown checker等错误。公司配置只在公司检查，不带回整份配置。路径或配置未对齐时先处理，不进入业务分析。

### 第3步：运行自有TestCase，核对默认选项和告警格式

**目的：** 用短用例确认bxen运行参数、日志写入和默认报告行为。

```bash
{
OOBSUM_CONFIG="WorkDir=$OOB_TC_WD Dump=options+stats" "$OOB_BUILD/bxen" -exec_mode=onlySa -cc_json=$OOB_BUILD -analysis_file=$OOB_ROOT/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-config ipa=none -analyzer-max-loop 3 -rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR" &&
python3 "$OOB_REPO/oobsum/tools/decisions.py" "$OOB_TC_WD"
} 2>&1 | tee "$OOB_LOG/03-testcase.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/03-testcase.exit"
```

**正确预期：** InlineDepth=0、DiagnosticStyle=sink、FlexibleArrayMax=4、ReportFlexibleArray=false；5条warning，decisions=7 emitted=5。引擎告警在Tc_Leaf的实际下标访问行，主行有[access=array-index]、元素大小和完整Call chain；decisions日志存在。
**异常时：** 保留03-testcase.log/.exit、testcase/decisions和dump。带回这整个自有TestCase的输出及本规则栈；选项、数量或位置不符时停下，先不要跑全板。

### 第4步：核对cdb并export业务FIR

**目的：** 确定输入范围，再将每个成功TU降级成中间表示。export只写fir/，不进行跨文件求解、不生成业务告警或decisions。

```bash
{
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" cdb --cc-json "$OOB_CDB"
} 2>&1 | tee "$OOB_LOG/04-cdb.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/04-cdb.exit"
```

**cdb正确预期：** 条目数/唯一文件数/语言分布符合本轮范围。重复条目不等于重复文件；同一主文件目前只保留一个FIR。
若“去掉输出选项后编译命令仍不同”非0，先在公司确认：仅无关选项差异可记录概括；若宏/ABI确实不同，先反馈变体计数与抽象差异，确定如何分批后再继续，不能把多变体当作全部已覆盖。

```bash
{
/usr/bin/time -v env OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL" &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
} 2>&1 | tee "$OOB_LOG/04-export.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/04-export.exit"
```

**export正确预期：** fir/逐渐增加，每个成功TU一个JSON，内含target、funcs、类型与指令。新目录下solved/decisions尚无业务结果。有效输入应全部成功，FIR数量应与有效唯一文件数相符；不能把success/failed条目数直接当唯一文件数。
主业务目标应为预期的aarch64_be ILP32（ptr=4、long=4、大端）；已确认的其它目标可以存在，solve会分组。可直接打开一个FIR检查target；FIR不是函数摘要。
**输入完整性预期：** 同一Tu重复产生相同FIR允许幂等；若内容不同，export报错误，原canonical不被覆盖，两个内容版本保存在fir-conflicts/Tu/，并阻止后续solve/report。inputs-pending表示FIR已重新导出，尚需成功solve；不表示已完成判断。
该检查只覆盖同Tu冲突，不是多个产品/配置的自动解析。不同FIR也可能来自源码/选项或降级输出差异，不能只凭内容哈希就认定公司改了宏。
若出现冲突，保留整个WorkDir及fir-conflicts，先在公司核对对应cdb条目属于哪个真实构建目标；不同程序/配置应分别使用匹配的cdb和新WorkDir。不能删除冲突目录或固定挑一个版本以求通过。
所有导出进程必须统一版本，export全部成功且结束后再solve；不支持不同阶段重叠或新旧版本混写。补丁不能找回旧运行已被覆盖的版本，验证保护需从新WorkDir重新export。文件系统不支持同目录硬链接时明确失败，不退回覆盖写法。
**异常时：** 保留04-cdb、04-export日志/.exit、clang_check原始任务日志及本轮fir/。若failed非0、缺TU、目标意外或异常退出，先停下；反馈条目/唯一文件/FIR/失败数、目标分布与错误类别概括。具体业务文件、命令和编译错误正文留在公司核查；不能把程序exit=0当成全部文件成功。

### 第5步：完整solve，并选择摘要保存范围

**目的：** 读全部FIR，按调用图算函数摘要和跨调用判断，生成solved/。stub.c只触发一次checker回调，不是业务分析对象。
摘要保存选择不缩小分析范围；A/B/C都完整求解。只执行下面case的一种分支。

| 方式 | 保存什么 | 后续查看 |
|---|---|---|
| A | 不额外存摘要，仍有solved判断 | 第6步记“未保存”并跳过；无法从solved反推完整摘要 |
| B | 只存指定函数local/derived | 可看这些函数；其它未保存函数需在未来solve留存 |
| C | 存全部可用函数local/derived | 此后任意已索引函数只读查询，无需重算 |

本轮要验证任意函数的摘要读取可选C。每函数两份JSON，按26.8万函数约53.6万份正文，增加磁盘/inode/写盘时间；需要节约空间可选B或A。
C使用summary-files，只落盘，不把所有函数JSON再刷进终端/日志。B保留原summary方式，少量JSON会显示在日志。

```bash
OOB_SUMMARY_MODE=C
```

**仅选B时再设置下面两个名字**，换成自己FIR中准确的funcs[].name。Top_A/Fill仅为本仓库自有用例的写法示范，不能照抄去选择业务函数。

```bash
OOB_SUMMARY_NAME='Top_A'
OOB_SUMMARY_NAME2='Fill'
```

单引号可以保留，它不属于值；不要添加占位用的尖括号或反引号。引用变量时保留双引号。无需export这两个shell变量。
已知对应FIR时可直接用编辑器看funcs[].name/key和顶层target；摘要/索引中名字字段才叫fn。盘点脚本是可选的：

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --list --contains "$OOB_SUMMARY_NAME" --limit 50
```

--contains/--limit只筛选展示，仍解析全部FIR，数据多时较慢。B默认也预检全部FIR；已确认名字时可在B命令加--skip-summary-precheck省去这遍Python解析，代价是错名得等solve结束才发现。C无需预检名字。
以下使用内置模型和默认分析选项；如配置ModelFile/MaxBlocks等自定义项，须让本轮相关solve/report和第9步对照使用同一组值，并在公司记录。

```bash
{
case "$OOB_SUMMARY_MODE" in
A) /usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR -analyzer-config rb.enhance.core.OobSummary:Dump=progress" --run ;;
B) /usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --summary-function "$OOB_SUMMARY_NAME" --summary-function "$OOB_SUMMARY_NAME2" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR" --run ;;
C) /usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --summary-all --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR" --run ;;
*) printf 'OOB_SUMMARY_MODE必须为A/B/C\n'; false ;;
esac
} 2>&1 | tee "$OOB_LOG/05-solve.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/05-solve.exit"
```

**正确预期：** command_exit/log_exit均0；最终progress为done；solved文件数等于本轮FIR数（含判断为空的TU），终端给出判断总数和非空TU数。
正常solve还应清除inputs-pending；若检测到fir-conflicts或遗留*.tmp，会写failed并非0退出，不生成新的分析结果。Dump=inputs可输出输入状态，持久化在dump/inputs.json。report只做常数次标记检查，不为每个TU重扫全部FIR。
B/C应确认摘要保存成功：write_failures=0、index_written=true，索引非空；B有预选函数的两种视图，C为available=matched=saved。A不产生新摘要索引是正常选择。

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD" > "$OOB_LOG/05-status.txt"
cat "$OOB_LOG/05-status.txt"
cp "$OOB_WD/dump/solve/progress.json" "$OOB_LOG/05-progress.json"
if [ "$OOB_SUMMARY_MODE" != A ]; then
    cp "$OOB_WD/dump/solve/summary/status.json" "$OOB_LOG/05-summary-status.json"
    cat "$OOB_LOG/05-summary-status.json"
fi
```

状态统计会读solved，结束后执行一次即可；过程中用下面的轻量观察，不要反复解析全量结果。

| 阶段 | 正在做什么 | 主要内存/磁盘产物 |
|---|---|---|
| read | 解析全部FIR，按目标ABI分组，准备函数/类型/全局输入 | 主要读fir/，尚无新solved |
| analyze | 依调用图先被调者后调用者，解释函数CFG；调用处利用已知被调效果，构造local事实 | 内存中的函数摘要、Req/Eff/calls/opens |
| propagate | 把被调者Req按调用实参代换，向上传递未解决要求，生成各上下文的结果 | 内存中的derived Req/reps/opens和证据链 |
| decisions | 将传播结果与未证明项目转换为DEFINITE/POSSIBLE/SUSPECT/SAFE及原因、位置、链 | 内存中的完整判断记录，report再决定报出策略 |
| summary（开启保存时，新版可见） | 按筛选保存local/derived；不是重新求解 | dump/solve/summary/的正文，最后汇总index/status |
| write | 按访问归属TU写预计算判断 | solved/每TU一个JSON，没有判断也写空列表 |
| done | 各目标已处理且solved写入结束 | progress最终快照；还需看摘要status，判断调试写盘是否成功 |

多目标依次运行各自的analyze/propagate/decisions/summary/write，前缀如group1/2；read只在开头，index/status在最后汇总。
保存是bxen checker在solve进程中做的，实际在各组求解完后；不是summary查询脚本做的，也不要求人一直盯着终端。进程退出后JSON仍在，可以随时打开。

另开终端时只复制相同的路径变量赋值，不重复新目录检查，也不更改本轮OOB_RUN_TAG；再观察：

```bash
cat "$OOB_WD/dump/solve/progress.json"
tail -20 "$OOB_WD/logs/solve.log"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
OOB_SOLVE_PID=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["pid"])' "$OOB_WD/dump/solve/progress.json")
ps -p "$OOB_SOLVE_PID" -o pid,etime,pcpu,rss,stat
```

阶段内completed/total不是总耗时百分比；最慢函数内部计数可能暂不动，结合CPU/RSS看。RSS和/usr/bin/time的Maximum resident set size均为KiB。
历史全量solve约25分钟、峰值约45.2 GiB，仅作比较基准，不是本轮承诺值；保存全部摘要另有写盘成本。
若超过2小时、RSS超过100 GiB、发生崩溃或写盘错误，停下保留完整日志、progress快照、阶段、退出码和资源数字，按本节末尾模板反馈。

**异常时：** 保留05-solve.log/.exit、原始logs/solve.log、progress/status快照、fir/和已写出的solved/summary。反馈阶段、退出码、FIR/solved数、判断数、摘要保存计数、Elapsed和RSS；涉及业务名字/路径的完整日志只留在公司。不要清目录或反复重跑覆盖证据，同一WorkDir不并发两个solve。

### 第6步：查看摘要，核对两条问题的推理过程

**目的：** 读取第5步已保存的函数摘要，验证能从参数/Req/calls看到传递关系。选A则本步记“未保存”并跳过；B/C再执行读取。

**这里没有生成或保存动作。** summary --function只读index，按函数名/键定位一个文件，再读取local或derived正文打印；不扫描fir/，不调用bxen，不重新solve。
summary --list则是从FIR盘点可用函数，已索引摘要数是附带统计；它与查看一份保存的正文是两种动作，没有必要每次先盘点。
保存位置：$OOB_WD/dump/solve/summary/，实际根目录由开头设置的OOB_WD决定。

| 文件 | 内容与用途 |
|---|---|
| index.json | 数组中每项有fn、key、target、tu、stem，用fn/key找到对应正文；摘要粒度是“目标内的函数” |
| stem.local.json | 该函数解释所得的实际local事实，作为传播输入；有参数、Req、Eff、调用点及当时实参/区间、未证明项目 |
| stem.derived.json | 保留local的参数/调用点/效果等，替换为传播后Req与reps、补入口opens，展示调用者上下文中的传递结果 |
| status.json | available/matched/saved/write_failures等计数；只反映保存状态，不是告警统计 |

stem是“目标+函数键”的哈希文件名；与判断fp不是同一种东西。打开index找到想看的fn，复制同条的stem，再在文件管理器/编辑器中打开那两个JSON即可。
手工打开与下面的脚本查询任选一种即可。普通文本编辑器就能查看；下面json.tool同样只是格式化读取。OOB_SUMMARY_STEM的中文字符串是填写位置，执行前换成index中的实际stem。

```bash
python3 -m json.tool "$OOB_WD/dump/solve/summary/index.json"
OOB_SUMMARY_STEM='从index中复制的实际stem'
python3 -m json.tool "$OOB_WD/dump/solve/summary/$OOB_SUMMARY_STEM.local.json"
python3 -m json.tool "$OOB_WD/dump/solve/summary/$OOB_SUMMARY_STEM.derived.json"
```

或者用下面的脚本按函数名读取，不用自己拼哈希文件名。B沿用第5步已设置的真实变量；C在这里填写要看的名字。下面Top_A/Fill仅为自有用例的赋值示范，执行前换成实际名字，不加尖括号/反引号。

```bash
OOB_SUMMARY_NAME='Top_A'
OOB_SUMMARY_NAME2='Fill'
```

```bash
{
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view local &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view derived &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME2" --view local &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME2" --view derived
} 2>&1 | tee "$OOB_LOG/06-summary-query.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/06-summary-query.exit"
```

tee仅在validation/留一份阅读输出副本；原始摘要仍是solve写在dump/solve/summary/的文件，查询不生成或重算摘要。该阅读日志含业务信息，只留在公司。
预期返回JSON且退出0；同名static/跨目标函数有多项时工具不会任选，改用index里的key给--function，并用--target给完整目标字符串。
索引没有该函数时只会提示缺失，不会自动启动求解；先确认实际名字和目标，再判断上次是否保存。等本次done且摘要保存成功后再按当前index查看，写盘过程中可能还有上次产物。
后续不开启保存的solve不会刷新旧摘要；输入/模型/选项变化时应使用新的WorkDir，或明确重新保存，避免把旧索引当本次结果。
若上次选C且保存成功，换任意已索引函数只需改变量再读；若上次只选两个，新的第三个未保存，就必须在下一次solve选它或选C。
solved含的是判断，不含所有函数的完整local/derived；FIR含的是指令，也不能直接当摘要；无法凭现有solved免费恢复未落盘的完整摘要。
已经保存的函数可以直接读取，无需重跑solve。Dump=summary且不设Filter也能存全部，但会把全部local JSON打印到日志；方式C避免这份重复输出。

**local/derived的区别，用自有Top_A → Fill说明：** Fill写入形参长度n要求的字节；Top_A给出16字节数组与长度17。
local的Top_A记录“调用Fill，传入对象大小16与n=17”等实参事实；传播会把Fill的访问要求代入这些实参，derived的Top_A因而可以含一条跨调用的POSSIBLE及证据链（具体级别以本用例的界来源为准）。
local并非只扫描本函数语法：analyze时已经应用已知被调者的效果；derived的重点是上传要求和在当前上下文已定级的结果。
两个都是同一函数的视图；derived没有reps不等于安全，要求可能继续上传，opens也可能仍未证明；是否报警最终看report策略与decisions日志。
先在local中看leaf的Req公式/守卫和上层calls实参，再看derived中对象大小、末端与调用链，便于核对推理是否对应源码。

**正确预期：** 两个视图都能读到合法JSON，所选fn/key/target对应预期函数；C可另选一个未预先点名的函数直接读取。查看过程中不启动bxen或求解。
**异常时：** 保留原index/status、对应local/derived文件和查询错误；同名函数先用key/target消除歧义。反馈保存方式、available/matched/saved/write_failures、是否缺文件/歧义/读入错误，以及视图是否可读；摘要正文与名字留在公司。B未选择的函数/A未保存的摘要属于保存范围，不能当成丢失。

### 第7步：report并核对汇总

**目的：** clang_check重新解析对应源码，将solved判断定位并报告，写decisions/*.jsonl；不重新执行全程序solve。
先记录开始时间，保证复制的全板JSON是本轮新报告。报告源由OOB_GBL的log_path决定，OOB_REPORT应为该文件。

```bash
{
touch "$OOB_LOG/07-report.started" &&
/usr/bin/time -v env OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL" &&
test -f "$OOB_REPORT" && test "$OOB_REPORT" -nt "$OOB_LOG/07-report.started" &&
cp "$OOB_REPORT" "$OOB_REPORT_COPY"
} 2>&1 | tee "$OOB_LOG/07-report.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/07-report.exit"
```

```bash
{
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" stats --workdir "$OOB_WD" --counts-only --top 50 &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" report "$OOB_REPORT_COPY"
} 2>&1 | tee "$OOB_LOG/07-stats.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/07-stats.exit"
```

**正确预期：** report命令退出0、有效输入全部成功；生成本轮报告JSON与decisions日志，工具能读出标签/原因统计，没有缺solved或多warning粘连提示。
solved是未应用报告策略的判断；decisions含报出/未报出及原因；统计中的“报出判断”可大于同sink行合并后的“物理告警”，report.json还可能有收集去重，不能将这些计数直接当成同一口径。
**异常时：** 保留07-report/07-stats日志/.exit、clang_check任务日志、solved/decisions和本轮报告JSON。复制检查不通过时先核对OOB_REPORT/log_path，不能用cp -r复制整个log目录冒充JSON。
inputs-pending或fir-conflicts存在时report会报错误，不读取旧solved；report阶段的inputs状态为快速检查，details_scanned=false、具体数量为-1表示未扫描，应读取solve阶段日志和本机冲突目录核对。
任务失败、缺TU、缺solved或计数差异无法用覆盖/合并/去重解释时停下，反馈成功/失败条目数、各口径计数与差值、stats --counts-only及report统计。原始业务报告留在公司，不贴告警正文。

### 第8步：检查告警格式、抽查并统计场景

**目的：** 验证告警可读性、分类及证据；在公司打开至少两条DEFINITE/POSSIBLE及主要SUSPECT原因的样本。

**正确预期：** warning定位实际下标/成员/解引用/内存访问的sink行；主行有Access、元素类型/大小、容量、范围或exclusive end、Reason、Context、完整Call chain。
同一sink行合并成一个warning，不同上下文/理由由Also展开；原判断通过report_group仍能找全。已知布局能换算元素容量；例如20字节结构体数组3项容量60，索引3处4字节字段的exclusive end=64，即越4字节。
分配位置的附加note只在来源文件唯一可定位时出现；同basename存在多个候选时保留主warning和来源文本，不猜某个文件。Dump=diagnostics可在dump/TU/allocation-notes.jsonl查看unique/ambiguous/missing等定位状态，文件可能含业务名字，仅留在公司。
SUSPECT只表示未证明安全，不能当成已证实越界；type_range_only的人类含义是缺少可用的代码约束。核对具体R/W对象、索引/长度来源、守卫及调用实参，别只按函数名或取地址写法判断真伪。

**可选：从某条判断查询调用链。** 本段依赖第7步已生成的decisions日志，不是查看摘要或统计场景的前置步骤。

**explain的指纹到底填什么：** 它是decisions日志记录里的fp字段，一串十六进制检索标识，不是函数名、key、stem，也不是随意填的“1”。
例如自有TestCase曾输出fn=F:Tp_Labels与fp=a5e49529d9fcedab：前者描述函数键，后者才是那条判断的指纹。这个示例fp不能照抄去查你的业务WorkDir。
如需从一条判断出发，直接打开对应decisions/TU.jsonl，复制那条记录的fp；也可以用decisions.py列出，但它会读全量日志并列很多条，数据大时不必使用。

```bash
python3 "$OOB_REPO/oobsum/tools/decisions.py" "$OOB_WD" --label definite --emitted
OOB_DECISION_FP='a5e49529d9fcedab'  # 仅为自有TestCase示例；实际替换为当前WorkDir那条记录的fp值
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" explain --workdir "$OOB_WD" "$OOB_DECISION_FP"
```

这一段只在公司查看判断/调用链，输出不带回。explain同样是只读，但目前需扫描decisions日志，全量时也可能慢；已有具体判断JSON时可直接看chain/msg等字段。
explain也接受函数名，含义是“查询与这个函数相关的判断/链”，并非“生成这个函数摘要”。已经知道名字、只要摘要时，直接summary --function更合适，根本不需要先找fp。
fp是判断检索标识，可能匹配多个上下文；warning里的group也是另一种查询入口。未保存的链上摘要会明确提示，而非悄悄补算。


```bash
{
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --workdir "$OOB_WD" &&
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --workdir "$OOB_WD" --multi &&
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --report "$OOB_REPORT_COPY"
} 2>&1 | tee "$OOB_LOG/08-scenes.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/08-scenes.exit"
```

**场景统计正确预期：** 默认按物理告警统计；主类合计等于该口径总数，--multi多标签合计可以更大，缺证据列UNCLASSIFIED。它统计已观察特征，不仅按调用层数分类，也不代表某等级所有能力均已支持；Lv10业务契约需人工判断。
**异常时：** 在公司保留相关判断fp/group、local/derived、source/sink与报告映射，必要时对照FIR；反馈只用抽象写法和数字：访问类别、读/写、元素大小、容量、范围、标签、保护条件类别及误报/真问题/看不出计数。位置、元素换算、分类或引用关系明显不符就停下反馈，不把业务函数/文件/源码/正文带回。

### 第9步：尾数组报告开关对照

**目的：** 复用同一FIR，只改变ReportFlexibleArray，核对默认过滤是否符合约定。为避免覆盖基线，用独立OOB_COMPARE；本步需要另一次solve/report，不用重新export。
默认FlexibleArrayMax=4：结构体最后一个静态数组元素数<=4以及[]为候选；设0只保留[]/[0]。这是一项报告策略，不代表SAFE；正常内层数组/非末尾成员/union仍检查。

```bash
{
test ! -e "$OOB_COMPARE" &&
mkdir -p "$OOB_COMPARE" &&
cp -a "$OOB_WD/fir" "$OOB_COMPARE/" &&
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_COMPARE" --bxen "$OOB_BUILD/bxen" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR -analyzer-config rb.enhance.core.OobSummary:Dump=progress -analyzer-config rb.enhance.core.OobSummary:ReportFlexibleArray=true" --run
} 2>&1 | tee "$OOB_LOG/09-flex-solve.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/09-flex-solve.exit"
```

确认本步solve完成、solved数与FIR数相符，再执行对照report。基线报告已在OOB_REPORT_COPY保存；公司共享log路径将产生对照报告。

```bash
{
touch "$OOB_LOG/09-report.started" &&
/usr/bin/time -v env OOBSUM_CONFIG="WorkDir=$OOB_COMPARE Phase=report ReportFlexibleArray=true" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL" &&
test -f "$OOB_REPORT" && test "$OOB_REPORT" -nt "$OOB_LOG/09-report.started" &&
cp "$OOB_REPORT" "$OOB_COMPARE/report.json" &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" stats --workdir "$OOB_COMPARE" --counts-only --top 50 &&
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" report "$OOB_COMPARE/report.json"
} 2>&1 | tee "$OOB_LOG/09-flex-report.log"
OOB_PIPE_STATUS=("${PIPESTATUS[@]}")
printf 'command_exit=%s log_exit=%s\n' "${OOB_PIPE_STATUS[0]}" "${OOB_PIPE_STATUS[1]}" | tee "$OOB_LOG/09-flex-report.exit"
```

**正确预期：** 两轮有效TU相同，原判断标签/数量相同；默认候选仍保留原标签并以flexible_array说明未报，打开后不再被这个原因过滤。仍可能受duplicate_access等其它策略影响，不要求每条候选都变成独立warning。普通固定内层访问保持检查。
在公司核对候选确实符合末尾数组的分配约定；已知实际分配很小的真问题也可能被该约定过滤，不能据此宣布安全。
**异常时：** 保留两套WorkDir、报告副本、09日志/.exit、两份stats --counts-only；反馈总数/标签/各过滤原因计数及抽象差异。原始标签改变、普通非候选访问意外消失或任务失败时停下，不覆盖基线重跑。

### 第10步：同配置多轮一致性对比

**目的：** 复用公司已保留的两轮结果，查差异最早出现于FIR、solved、decisions还是最终报告。先比较已有数据，不为此立即重复跑两次全量；若旧数据被同目录覆盖，则无法反推旧轮，只能重新保留两轮独立快照。
选取同一机器/源码绝对路径、同一版本/cdb/模型/选项的两轮；不要把第9步flex-on和默认值的有意差异当作不稳定。工具不能自动证明这些前提相同。
在开头路径区填写两轮OOB_COMPARE_LEFT/RIGHT；默认报告为各目录report.json，旧报告保存在别处时加--report-left/--report-right的实际文件路径。只有一轮或旧数据被覆盖时，本步先记“缺对照未执行”，不要拿不存在的示例目录运行。

```bash
python3 "$OOB_REPO/oobsum/tools/compare_runs.py" "$OOB_COMPARE_LEFT" "$OOB_COMPARE_RIGHT" --json > "$OOB_LOG/10-comparison.json" 2> "$OOB_LOG/10-comparison.progress"
OOB_COMPARISON_EXIT=$?
printf 'compare_exit=%s\n' "$OOB_COMPARISON_EXIT" | tee "$OOB_LOG/10-comparison.exit"
cat "$OOB_LOG/10-comparison.json"
```

**正确预期：** 完整同输入结果退出0，complete/equal均true，各目录added/removed/changed均0，report的same_warning_records=true。
退出1表示发现变化，退出2表示缺阶段或未完成/冲突输入；两者都需保留证据并反馈，不是成功。进度只输出阶段名，默认结果只含计数，可带回。
只完成export的两轮，可在同一命令加--through export；只到solve则用--through solve。这样不必为了定位FIR差异继续做25分钟求解。
报告按本规则记录的多重集合比较，忽略数组排列、保留重复次数；only_md5_differs=true表示位置/正文等字段相同、只md5变化，不是主告警字段变化。without_md5保留其它所有字段，不能据此认定漏洞真伪。
FIR等按字节摘要比较，不解析整个大FIR；差异也可能是格式/版本/编号变化，不能只凭哈希判定源码逻辑变化。FIR相同而solved变要核对模型/选项/版本，decisions变核对报告策略，仅report变再查收集器/文本。
如需在公司定位具体记录，可加--details-json "$OOB_LOG/10-local-details.json"；其中含本地文件列表/报告记录索引与md5，留在公司，不带回。大报告仍需解析，会占用额外时间和内存。
**异常时：** 保留两个原WorkDir/报告、10-comparison结果与进度；反馈退出码、first_change、各阶段计数、guard状态及是否only_md5_differs。缺少阶段不能把两个空集合说成一致；原告警正文及详细索引留在公司。

### 第11步：保存本轮结果并反馈

**正确预期：** OOB_LOG有各阶段.log/.exit、progress及摘要计数快照；基线/对照报告分别保留；能够把告警映射到判断和摘要。
如选C且要比较存储成本，可在公司执行：

```bash
du -sh "$OOB_WD/fir" "$OOB_WD/solved" "$OOB_WD/decisions" "$OOB_WD/dump"
find "$OOB_WD/dump/solve/summary" -maxdepth 1 -type f | wc -l
```

扫描摘要目录同样需要时间，结束后做一次即可，文件数不是求解进度。
任一步异常，不必等整轮完成：保留该步日志/.exit与关联产物，填下面模板已完成的部分，未执行处写“未执行”，立即反馈。只有自有测试的错误可原样贴；业务阶段日志/数据仅在公司留存，带回计数和脱敏概括。

```text
清单版本：2026-11-10-3
停止/完成步骤 __；提交号 __；各步骤command_exit/log_exit __
稳定性输入概括：每轮是否新WorkDir __；cdb重复文件/不同编译命令文件数 __；不同宏/ABI是否代表不同产品配置 __；源码/选项/模型是否固定 __
重复差异：总数或仅顺序/附加note/主warning变化 __；各轮WorkDir与报告是否已在公司留存 __
1. 构建 __；lit通过/失败 __；replay不一致 __；csa回退 __ / TN强告警 __；check-tree __
2. land/公司编译 __；规则名/目标报告路径核对 __
3. 自有TestCase：InlineDepth __；DiagnosticStyle __；decisions __ emitted __；warning __；sink/调用链 __
4. cdb条目/唯一文件/重复文件/C/C++ __；不同编译命令文件数 __；差异类别概括 __
   export成功/失败 __；FIR __；缺TU数 __；目标组及数量 __
   FIR冲突TU数 __；遗留候选数 __；inputs状态 __；是否统一版本/阶段串行 __；冲突分类概括 __
5. solve方式 A/B/C __；Elapsed __；峰值RSS KiB __；solved __；判断 __；非空TU __；最终阶段 __
   摘要available/matched/saved/write_failures __；index_written __；保存阶段可见 __
6. local/derived可读 __；C时任意另选函数可读 __（A写未保存）；抽象推理核对 __
7. report成功/失败 __；日志判断数/报出判断数/物理warning数/report条数 __；无法解释差值 __
   附基线stats --counts-only与report统计（只有计数）
8. 告警格式核对 __；场景默认/multi计数 __；各原因抽查 真问题/误报/看不出 __；抽象写法概括 __
9. flex-on solve/report退出码 __；附对照stats --counts-only和report统计；与基线差异 __
10. 一致性比较退出码 __；complete/equal/first_change __；附默认计数JSON（不附details）
11. 摘要保存体积/文件数（若测量）__
异常：步骤 __；错误类别 __；退出码 __；progress/摘要状态纯计数 __；仅本规则的非业务栈 __
自有测试异常：附完整失败段；业务内容：不附源码/文件名/函数名/告警正文/原始JSON
```

## 1. 这个规则是怎么跑的

### 1.1 摘要是什么

每个函数**只分析一次**，得到它的摘要：它对实参的要求（例如"写 `p[0 … n]`，要求 `n < p 指向的对象大小`"）和它对外的效果（写了哪个成员、返回值的范围……）。
在每个调用点把**实参代进**被调者的摘要：能判的就地判（越界 → DEFINITE / POSSIBLE，证明了 → SAFE）；判不了的，这条要求换成调用者的形参，变成调用者的要求，
继续往上传——直到碰到给出缓冲区和长度的那一层。所以 3 层、5 层、20 层的链都一样处理（本地测过 20 层），默认告警定位到实际访问的sink行（SPEC-0126），证明上下文和整条调用链在消息中列出。

每条判定都带标签：DEFINITE（明确越界）/ POSSIBLE（在代码给出的界下可能越界）/ SUSPECT（证明不了安全，带原因，§1.6）/ SAFE（证明了安全，默认不报）。
所有层数的 DEFINITE / POSSIBLE 默认都报（SPEC-0122，`InlineDepth=0`）；SUSPECT 也不按层数过滤。

### 1.2 两种跑法

| | A 默认模式（`Phase=inline`） | B 跨文件模式（`export` → `solve` → `report`） |
|---|---|---|
| 怎么跑 | 一个文件一个进程，一条命令——和其他单文件规则一样，clang_check 跑一遍 | clang_check 跑两遍（export、report），中间一条 solve（单进程） |
| 跨函数 | 同一个文件里任意层数 | 任意层数 |
| 跨文件 | 被调者在别的文件里：这次调用记 SUSPECT `not_in_tu`；没有调用者的对外函数当入口，它的要求证明不了就记 SUSPECT `entry_unproven` | solve 把所有文件的函数放进一张调用图，被调者先算，跨文件的调用点照样代换实参 |
| 告警 | 每个进程直接打印 | report 那一遍每个进程打印自己文件里的 |
| 适合 | 先看一遍：会不会崩、多快、大概多少 | **正式结果** |

B 的三遍：**export** 每个文件把自己的函数翻成中间表示写进 `WorkDir/fir/`（不分析、不报，比 A 快）；**solve** 一个进程读全部 `fir/`，按全局调用图算摘要、
在所有调用点判定，结果按文件写进 `WorkDir/solved/`；**report** 每个文件读自己的 `solved/<文件>.json`，在源码位置上报告警（只解析源码、不分析）。

**为什么 B 要跑整个 cdb**：只跑一个子系统时，子系统外面的被调者都"没定义"（`unknown_callee`），子系统里被外面调用的函数都当成入口（`entry_unproven`、`typed_field`）——
这些 SUSPECT 大部分是范围造成的，跑全量会消失。2026-10-27 子系统 B 的 3519 条里，`unknown_callee` 844 条就是这一类。

**A 与 B 的条数为什么差这么多**：A 每个文件各算一遍头文件里的 inline 函数，同一处访问在每个包含它的文件里各记一条（判定日志 20684 条），
clang_check 按"文件 + 行 + 正文"去重后剩 3644；B 的 solve 对同一个头文件函数只算一份（3519 → report.json 3169）。

### 1.3 和 CSA 原来的跨文件（CTU）有什么不一样

* CSA：路径敏感的符号执行，从每个顶层函数往下走；遇到别的文件的函数，就把那个文件的 AST 导入进来接着走。路径数随分支和调用层数指数增长，
  靠 `max-nodes`、inline 深度截断——深的链会被截掉，这也是现有规则只覆盖 1–2 层的原因。
* 本规则：**不做路径敏感执行**（只挂 TU 级的 `check::ASTDecl<TranslationUnitDecl>`，不跑 ExprEngine）；每个函数在 CFG 上做一次抽象解释（循环用加宽收敛）得到摘要；
  跨文件时**不导入 AST**，只交换每个文件导出的中间表示；solve按调用图自底向上算、在调用点代换。没有固定深度截断，但Req数量、CFG与表达式复杂度、递归/别名精度和资源仍有限制；不能承诺所有输入线性耗时，详见§1.8。

### 1.4 选项怎么传：clang_check 不用改

clang_check 给每个文件拼的命令里只有 `-rbchecker-config …`、`-analyzer-max-loop 3`，没有本规则的选项——本规则就全用默认值：A 默认模式、不写 WorkDir。
**这样也能跑、照样出告警**，只是没有判定日志（`stats` 看不了），也跑不了 B。

要写判定日志或者跑 B，在 clang_check 的命令**前面**加环境变量 `OOBSUM_CONFIG`（SPEC-0120），clang_check 起的每个 bxen 进程都会继承它
（2026-10-27 已在公司验证：WorkDir 只写在环境变量里，判定日志写到了那里）：

```bash
OOBSUM_CONFIG="WorkDir=/usr1/oobsum-work/xxx Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py …
```

* 写法：`键=值`，空格（或 `;`）分隔；键就是本规则的选项名（`Phase`、`WorkDir`、`Dump`、`CharAlias`…，见 §2.7）。
* 优先级：环境变量 > `-analyzer-config` > 默认值。写错键名或取值，和命令行写错一样报选项错误（每个文件都会报，一眼能看到）。
* `Dump=options` 打印的那行末尾会显示 `OOBSUM_CONFIG="…"` 的原文，确认它生效了。
* 写在命令前面只对这一条命令有效；**不要 `export`**（否则后面所有 bxen 都带着它）。我们自己的脚本（`run.py`、`oobsum_run.py`…）会忽略它。
* CI 里要在 clang_check 里面设置：见 §2.10。

### 1.5 告警长什么样、report.json

和其他 CSA 规则的格式一样，clang_check 照常收进 report.json：

```text
<文件>:<行>:<列>: warning: [OOB-<标签>[:<原因>]] <英文正文> [rb.enhance.core.OobSummary]
<文件>:<行>:<列>: note: Call chain: A (a.c:10) -> B (b.c:20) -> C (c.c:30)
```

标签在方括号里（`[OOB-DEFINITE]`、`[OOB-SUSPECT:not_in_tu]`），可以按它筛。默认sink格式还将完整调用链写在warning主行，note是附加展开；要看已保存摘要和判断上下文可直接读JSON，或用§0第6步的只读查询。`oobsum_run.py report <report.json>` 按标签 / 原因数本规则的告警（只有数字，可以带回）。

### 1.6 SUSPECT 的原因：是什么意思、怎么判断

SUSPECT = 证明不了安全，**不等于有问题**；原因说明卡在哪一步。看的时候先看原因，再看代码里那个指针 / 长度从哪来。

| 原因 | 意思 | 典型写法 | 怎么判断 | 跑全量 B 会少吗 / 以后怎么少 |
|---|---|---|---|---|
| `typed_field` | 入口函数（分析范围里没有调用者）的指针形参，访问在它声明的类型大小之内（`pstMsg->usLen`），但证明不了调用方传进来的对象真有那么大 | `void Msg_Proc(MSG_S *pstMsg) { … pstMsg->usLen … }` | 看真正的调用方：传的是不是这种结构体（还是一块更小的缓冲区强转过来的）。正常用法就是误报 | 会：调用方在范围里的都代入实参而消失，只剩真正的入口（任务入口、回调、消息处理）。以后可加选项"入口形参按声明类型信任"（要你定） |
| `entry_unproven` | 入口函数里依赖形参的访问证明不了（`p[i]`，`i` 是形参） | `void Copy(UINT8 *p, UINT32 i) { p[i] = 0; }` 没有调用者 | 看平台 / 调用方是否保证了长度、下标 | 会：同上 |
| `untracked_pointer` | 指针来自数组元素、超过两层的成员，或 C++ 的 `this`，不跟踪它指向多大 | `pstCtx->pstCfg->pucBuf[i]`、`astTbl[k].pData[n]`；C++ 成员函数里的 `m_buf[i]` | 看那个指针指向的对象多大、下标 / 长度有没有检查 | 不会。以后：成员级的界（指针成员和长度成员配对）、`this` 建模（SPEC-0121 §9） |
| `unknown_callee` | B：被调者在所有分析的文件里都没有定义，实参里有指针 | 调用库函数、汇编实现、cdb 之外的模块 | 看被调者会不会往那个指针写、写多少 | 会：全量时只剩真正的外部函数；常用的外部函数可以写进模型文件（§2.7 ModelFile） |
| `not_in_tu` | A：被调者不在本文件里 | 同上 | 跑 B | B 里没有这个原因 |
| `type_range_only` | 长度 / 下标只知道类型范围，没有检查约束它（来自报文字段、全局、调用结果） | `memcpy_s(dst, len, src, pstMsg->usLen)` 前面没有检查 `usLen` | **最值得看**：外部来的长度没校验就用，常是真问题 | 不会（除非检查在别的函数里、跨文件后能代进来） |
| `fptr` | 经函数指针调用、解析不出目标，实参里有指针 | 回调表、注册的处理函数 | 看可能的目标会不会越界写 | 以后：函数指针表的解析 |
| `cxx_call` | C++ 成员函数 / 构造函数 / 虚函数的调用，实参里有指针（`this` 没建模，SPEC-0121） | `obj.Fill(buf, len)` | 同 `fptr` | 以后：`this` 建模 |
| `recursion` | 递归环上的调用，更深处的访问没检查 | 互相递归的解析函数 | 看递归深度与下标 | 以后：环上求不动点 |
| `subobject_overflow` | 可能越出成员数组 / 多维数组的一行，但没越出整个对象 | `st.aucName[i]`，`i` 可能 ≥ 成员长度 | 看是不是故意跨成员写（有的代码这么用） | 选项 `Subobject=off / label`（§2.7） |
| `pointer_stepping` | 指针在循环里步进 | `while (n--) { *p++ = 0; }` | 看循环次数与对象大小 | — |
| `alloc_size_unknown` | 分配函数的大小不知道 | 内部封装的分配器 | 写进模型文件（§2.7 ModelFile） | — |

### 1.7 C++ 文件怎么处理（SPEC-0121）

`.cpp` 照常分析：C 的写法与 C 文件完全相同；引用按指针算（引用形参、经引用写回调用方都精确）；重载、模板实例化各算各的。
`this` 还没建模：成员函数里经 `this` 的访问记 `untracked_pointer`，成员函数 / 构造函数的调用记 `cxx_call`——**都报 SUSPECT，不会因此推出 SAFE**。C++ 类写得多的话这部分会比较吵，
看第 4 步的 C++ 文件数再定要不要做 `this` 建模。

### 1.8 摘要、复杂度与复核解释

详见 [10-analysis-and-diagnostics.md](10-analysis-and-diagnostics.md)：
摘要为什么有其它函数与 reps / chain；真实推理如何使用 local 事实；reported 与 emitted 的区别；
相对于 CSA 路径执行的成本结构、没有统一加速倍数与实际限制；9秒/257MiB、旧批次20分钟/29.4GiB与新批次25:07.76/45.2GiB的测量口径；
exclusive end 和结构体元素容量换算；各类 SUSPECT 的证据边界；四类 YAML 模型与不能随便声明 pure 的原因。

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
| `Checkers.td` 里的 `def OobSummaryChecker` | `xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` 的 `RosaRBEnhanceCore` 块最后（已有旧父包则移动，内容同步替换） |
| `oobsum/testcase/OobSummaryChecker/` | `xir/llvm_ast/TestCase/OobSummaryChecker/`，并在 `TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(OobSummaryChecker)` |

找不到预期结构（比如包块结尾不是 `} // end "rb.enhance.core"`）会停下来，不猜；把报错贴回来改脚本。
改了 `Checkers.td` 后如果新选项不生效，多半是 `Checkers.inc` 没重新生成：做一次完整编译（`csa-checker-dev` 的 TableGen 注意事项）。

### 2.2 TestCase

```bash
rm -rf /tmp/oobsum-tc && mkdir -p /tmp/oobsum-tc
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-config ipa=none -analyzer-max-loop 3 -analyzer-config rb.enhance.core.OobSummary:WorkDir=/tmp/oobsum-tc -analyzer-config rb.enhance.core.OobSummary:Dump=options+stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /tmp/oobsum-tc
```

预期：5条告警，包括四条调试内建函数告警和一条引擎DEFINITE；引擎告警定位Tc_Leaf的实际访问行，主行列出Access、元素信息和完整Call chain；`[oobsum:stats] … decisions=7 emitted=5`；
`decisions.py` 显示 7 条判定，其中 2 条没报（都是 `safe`；默认不再按层数过滤）。每个版本的预期写在 `TC_OobSummaryChecker.c` 的文件头。

### 2.3 67 个分级用例：bxen 跑一遍，和本地的 `run.py csa` 对照（SPEC-0111）

```bash
cd /usr1/oobsum-llvm
python3 oobsum/cases/run.py cdb --out /usr1/oobsum-cases-cdb --cc /usr/local/bin/cc
rm -rf /usr1/oobsum-cases-wd && mkdir -p /usr1/oobsum-cases-wd
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/oobsum-cases-cdb/ -analysis_folder=/usr1/oobsum-llvm/oobsum/cases -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-config ipa=none -analyzer-max-loop 3 -analyzer-config rb.enhance.core.OobSummary:WorkDir=/usr1/oobsum-cases-wd -analyzer-config rb.enhance.core.OobSummary:Dump=summary -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 oobsum/cases/run.py csa --from-workdir /usr1/oobsum-cases-wd
```

预期：`合计 134 个入口：通过 106，通过+S 20，级别不同 2，超出预期 4，需跨TU 2`；G2 那行 `0 个`；`回退 0 条`；"变化"5 行——LV1-07、LV6-01 各两行
（单文件跑，要跨文件）、LV9-02 一行（64 位上多 2 条 SUSPECT）。

* 第一条给用例生成 cdb：`--cc` 用和 TestCase 同一个编译器；bxen 不会自己找 gcc 的系统头文件（`stddef.h` 等），`run.py cdb` 用 `<cc> -E -x c - -v`
  问出这些目录、按顺序写成 `-isystem`（SPEC-0115）。还缺参数用 `--flags="…"` 追加（要带等号，因为值以 `-` 开头）。
* `Dump=summary` 必须带（对照要用调用图）。
* `-analyzer-config` 的列表值用 `+` 连接（`Dump=options+stats`），逗号会被当成分隔符。

### 2.4 看分析时用的目标（SPEC-0117）

每次运行都会在 `WorkDir/fir/<tu>.json` 里记下 bxen 分析这个文件时用的目标（不用加 `Dump`），`stats` 的第一行就是它：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir> | head -1
# 目标：<三元组> ptr=<指针字节数> long=<long 字节数> <big-endian 大端 | little-endian 小端>（K 个 TU）
```

已测（2026-10-27）：三个 cdb 分别是 `armebv4t-unknown-linux-gnueabi`、`aarch64_be-unknown-linux-gnuilp32`、`i386-unknown-linux-gnu`，都是 `ptr=4 long=4`。

* **布局本来就会影响结果，这是对的**：越界按字节算，同一个结构体在 32 位上 8 字节、在 64 位上 16 字节，`memcpy_s(&st, 8, …)` 在一个架构上安全、在另一个上越界。
  要保证的是"每个 cdb 都按它自己真实的目标算"，不是"几个架构结果一样"；几个架构都要看就各跑各的、各用一个 WorkDir，以产品实际运行的架构为准。
* 看到 `x86_64 … ptr=8` 而那个 cdb 是单板产品：bxen 按宿主算了，含指针 / `long` 的结构体会算大——把这一行带回来。

### 2.5 用 clang_check 跑（cdb 每个文件一个 bxen 进程、线程池并行，SPEC-0118 / 0120）

选项靠命令前的 `OOBSUM_CONFIG`（§1.4）。**每一轮用一个新的空 WorkDir**（旧文件会混进 solve 和统计）；每次 clang_check 跑完把 report.json 复制一份（下一次会覆盖）。

```bash
# A 默认模式
OOBSUM_CONFIG="WorkDir=<WorkDir>" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e /usr1/BXPlugin/cmake-build-release-release/ -j <cdb 目录>/compile_commands.json -r rb.enhance.core.OobSummary -c /usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/gbl_config.json
# B 跨文件：export 一遍 → solve → report 一遍（同一个 WorkDir、同一个 cdb）
OOBSUM_CONFIG="WorkDir=<WorkDir> Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py …（同上）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir <WorkDir> --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" --run
OOBSUM_CONFIG="WorkDir=<WorkDir> Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py …（同上）
```

* 检查 `/usr1/clang_check/src/cfg/project_cfg.py`：本规则旧名应从 `IPA_CMN_RULE_CHECKER` 移除；新 core 名也不要加入该 IPA 清单。
  本规则不走 CSA 的 inline，`ipa=none` 加不加判定相同；SPEC-0122 起迁入 core，应从 IPA_CMN_RULE_CHECKER 移除，使用 core / cmn 的默认 ipa=none。
* solve 不加 `--run` 只打印它要执行的那条 bxen 命令。`oobsum_run.py status --workdir <WorkDir>` 看 `fir/`、`solved/`、`decisions/` 各有多少。
* A 跑过的 WorkDir 里已经有 `fir/`（设了 WorkDir 就会写），可以跳过 export 直接 solve——但 report 会覆盖 `decisions/`，要留 A 的结果就先复制一份。
* 磁盘：FIR/solved/摘要/日志分别计量，不能固定按源码倍数估算。只要少量摘要用--summary-function，想日后任意查看用--summary-all；它只落盘不逐函数刷屏，但每函数两份文件会增加体积/inode/写盘时间，§0第5步列出取舍。
* cdb 里同一个文件出现多次（不同宏各编一次）：告警不受影响，判定日志 / FIR 只留最后写的那份；`oobsum_run.py cdb --cc-json <cdb 目录>` 数有多少。
* 进程被超时杀掉：只会留下 `*.tmp`，不会有写了一半的 `.json`。
* 不用 clang_check 时：`oobsum_run.py bxen --workdir … --bxen … --cc-json <目录> --analysis-folder <目录> --extra "…"` 打印一条 bxen 跑几个目录的三段命令（试跑用）。

### 2.6 看结果

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir> --counts-only   # 目标、标签 × 层数、SUSPECT 按原因、没报的按原因（只有计数，可以带回）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <WorkDir>                 # 再加报得最多的函数 / 文件（业务信息，只给自己看）
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py report <report.json>                      # clang_check 的报告里本规则的告警按标签 / 原因计数
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py <WorkDir> --label suspect --reason <原因>   # 某个原因的判定逐条列出（默认只列报了的；只给自己看）
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py <WorkDir> --emitted --jsonl review.jsonl   # 报了的判定导出给大模型复核
```

### 2.7 选项对照（D25：取舍都做成了选项，按实测选）

同一批文件、**每种取值一个 WorkDir**，在 `OOBSUM_CONFIG` 里多加一个键，比 `stats --counts-only`：

| 跑哪一遍 | `OOBSUM_CONFIG` 里加的 | 看什么 |
|---|---|---|
| 基线（默认） | （不加） | 计数表作对照 |
| char 写别名 | `CharAlias=true`、`CharAlias=verify` | SUSPECT 条数；`verify` 下 `alias_unverified` 的条数，抽几条看是否真有别名（SPEC-0103 / 0106） |
| 内层界 | `Subobject=label`、`Subobject=off` | `subobject_overflow` 条数；`label` 下内层的 DEFINITE / POSSIBLE（SPEC-0104 / 0105） |
| 不回绕假设 | `AssumeNoWrap=true` | SUSPECT 少了多少；抽几条打开后不再报的，看检查是否确实不会回绕（SPEC-0108） |
| 浅层过滤对照 | `InlineDepth=2`（默认 0） | 浅层被过滤掉多少；默认全报兜底 |
| 只报某些 SUSPECT | `SuspectReasons=type_range_only+entry_unproven` | 只留这几个原因的 SUSPECT（其余照样写进判定日志） |

业务的分配器、拷贝封装、纯函数（07- 输入 I5 / I8 / I9）写进模型文件：复制 `oobsum/config/oob_models.example.yaml` 改，`OOBSUM_CONFIG` 里加 `ModelFile=<路径>`；
`Dump=models` 看实际生效的表（SPEC-0033）。模型文件留在公司，不带回。

### 2.8 查看已完成结果的摘要与判断上下文

按§0开头设置OOB_REPO/OOB_WD等路径；下面只读取已有产物，不需要先重新单文件分析。
Top_A是本仓库自有函数名示范，换成自己实际保存的函数名；不用尖括号/反引号，单引号可以保留。

```bash
OOB_SUMMARY_NAME='Top_A'
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view local
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view derived
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" explain --workdir "$OOB_WD" "$OOB_SUMMARY_NAME"
```

前两条直接看该函数的摘要；最后一条扫描判断日志，看相关判断和调用链，可能较慢，不是必须先做的一步。
只要某条判断时，把对应日志记录的fp值给explain；fp不是函数名，不要把“指纹1”当真实参数。
也可以直接用编辑器打开dump/solve/summary/index.json及其stem对应的两份JSON，打开decisions/TU.jsonl看fp/chain；脚本不生成摘要。
未保存的函数只能在未来一次solve中选择保存，不能从solved免费恢复；全量保存/少量保存/不保存的完整命令与代价见§0第5步。

### 2.9 撤掉

```bash
python3 /usr1/oobsum-llvm/oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --remove --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

### 2.10 接入 CI：在 clang_check 里设置

CI 只调 clang_check，所以要在 clang_check 里给本规则做两件事（clang_check 是公司的代码，下面只说要加什么，你按它的结构加）：

1. **传选项**：起 bxen 的线程池之前设 `os.environ['OOBSUM_CONFIG'] = 'WorkDir=<本次运行的目录>/oobsum …'`（子进程继承；或者把同样的 `-analyzer-config rb.enhance.core.OobSummary:…`
   拼进本规则的 `-extra_args`，效果相同）。WorkDir 每次运行一个新的空目录。
2. **跨文件（B）要三步**：本规则跑两遍线程池、中间一条 solve——
   ```text
   第 1 遍：OOBSUM_CONFIG="WorkDir=W Phase=export"，全部文件（这一遍不出告警，report.json 不用它的）
   solve：  python3 <oobsum-llvm>/oobsum/tools/oobsum_run.py solve --workdir W --bxen <bxen> --extra "<rbchecker / funcptr 配置>" --run
            （或者不依赖本仓库：在 W/solve_stub/ 写一个只有 "void oobsum_solve_stub(void) {}" 的 stub.c 和只含它的 compile_commands.json，
             用 bxen 对它跑一次，-extra_args 里加 -analyzer-config rb.enhance.core.OobSummary:Phase=solve 和 …:WorkDir=W）
   第 2 遍：OOBSUM_CONFIG="WorkDir=W Phase=report"，全部文件；report.json 只收这一遍的告警
   ```
   只要 A（单文件）的话，第 1 条就够了，跑一遍。

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
