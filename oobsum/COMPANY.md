# 公司侧：在公司电脑上同步、编译、跑 OobSummary

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen / clang_check 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。
>
> **怎么用这个文件**：每次需要你在公司做事，Claude 都会重写 §0（带清单版本号，CLAUDE.md §5 第 8 条）——`git pull` 后只看 §0、照着做、把最后的模板填好贴回来。
> §1 讲这个规则是怎么跑的、告警怎么看；§2 是各项操作的完整命令（§0 里的步骤都来自这里）；§3 是首次搭建（已完成）。

## 0. 下一次在公司按顺序做

**清单版本：2026-11-04-1**（统一为一套完整流程，去掉“空索引优先恢复”；说明计算/保存/读取各自做什么，盘点可选，新增一次保存全部摘要与跳过预检。已完成的两函数solve无需重跑；全量保存留到下一次计划中的全量运行选择验证。）

开头只设置路径变量。函数名等第3步需要选择保存范围时再设置；看已保存摘要时也可在第4步再设置。
OOB_WD是WorkDir根（含fir/、solved/、dump/），OOB_REPORT是报告JSON文件，不能是log目录；路径只在这里修改。

```bash
OOB_REPO=/usr1/oobsum-llvm
OOB_ROOT=/usr1/BXPlugin
OOB_BUILD=/usr1/BXPlugin/cmake-build-release-release
OOB_CDB=/usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all
OOB_GBL_OLD=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
OOB_GBL=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/gbl_config.json
OOB_REPORT=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/log/allfiles-buffer_overflow_report.json
OOB_WD=/usr1/oobsum-work/full-b-20261103
OOB_INI=/usr1/clang_check/src/cfg/ini/config.ini
OOB_FUNCPTR=/usr1/clang_check/src/cfg/ini/config_funcptr.ini
```

### 已经做完、确认没问题的

| 项 | 带回的结果 |
|---|---|
| 旧版回归 / TestCase | lit 107；csa 通过 108、回退 0；TestCase 告警 4、decisions=7 emitted=4 |
| C++ 崩溃修复（SPEC-0121） | Release / Debug 都 exit=0，无断言；decisions=242 emitted=222 |
| CSA IPA 对照 | 同一 TestCase 加 / 不加 ipa=none：均 7 条判定、4 条告警 |
| 全板 cdb | 10686 条、10262 个文件、261 个文件出现多次；C 6689、C++ 3573 |
| 全板 B export | 6m53.384s，FIR 10245；clang_check 成功 10507 / 失败 179（按 cdb 条目计） |
| 全板 B solve | exit=0；19m49.53s；峰值 30840260 KiB（29.4 GiB）；solved 10245，判定 2211387（9861 个 TU 非空） |
| 全板 B report | 6m58.762s；成功 10576 / 失败 110；日志判定 2210526、报出 769203；report.json 629741（DEFINITE 478 / POSSIBLE 104 / SUSPECT 629159） |
| 目标 | FIR 中 10235 个 aarch64_be ILP32、10 个 x86_64 LP64；旧版本混合求解；SPEC-0124 起自动分目标，需用新版本结果复核 |
| 本次摘要盘点（2026-11-04） | 函数268184、无法盘点FIR=0；当时索引0 |
| 本次两函数摘要保存（已完成） | FIR/solved=10257，判定2231627（9884个TU非空）；摘要2个、local/derived均可读；exit=0；25m07.76s，峰值47346220 KiB（约45.2 GiB） |

下面是一套完整操作顺序，用于理解和复现整个流程，不要求每次从头重复。
你这次已经完成10257个TU的solve并保存两个函数；现在可直接到第4步打开已保存JSON。report、尾数组对照和抽查等尚未带回的结果继续保留在下面。
只有源码/编译选项变化才需重新export；只有引擎/模型/策略变化或要补保存未留存的摘要才需重新solve；改读哪个已保存函数无需重新分析。
同一源文件多次编译若宏/ABI不同，目前TuId只留一个变体；cdb差异计数非0时需在公司确认影响，带回计数和概括，不贴名字或命令。
旧批次17个唯一文件无FIR、report比solve少861条判断的覆盖问题仍待核查；本次FIR增加到10257，不能直接沿用旧差数认定当前缺失数量。

### 第 1 步：更新、同步、编译和改规则配置

```bash
cd "$OOB_REPO" && git pull
ninja -C "$OOB_REPO/cmake-build-release" clang
"$OOB_REPO/cmake-build-release/bin/llvm-lit" -s "$OOB_REPO/cmake-build-release/tools/clang/test/Analysis/oobsum"
python3 "$OOB_REPO/oobsum/cases/run.py" csa
python3 "$OOB_REPO/oobsum/devenv/land.py" --drop "$OOB_REPO/oobsum/landing/OobSummaryChecker.manifest" --into bxplugin --root "$OOB_ROOT" --apply
make -C "$OOB_BUILD" -j16
mkdir -p "$(dirname "$OOB_GBL")"
if [ ! -f "$OOB_GBL" ]; then cp "$OOB_GBL_OLD" "$OOB_GBL"; fi
python3 -c 'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text(p.read_text().replace("rb.enhance.ipa.OobSummary", "rb.enhance.core.OobSummary"))' "$OOB_GBL"
```

预期：编译、lit 全通过、csa 回退 0；land.py 将同一个 checker def 从 ipa 块移动到 core 块（不会留下两个 def）。
检查新 gbl_config 的 log_path 改到新目录；clang_check 项目配置中旧的规则名改为 rb.enhance.core.OobSummary，归到 core / cmn 的分类。
这两个公司配置文件不在仓库里；保留其它规则的配置。如果报 unknown checker 或旧选项前缀，停下带回本规则相关错误。

### 第 2 步：TestCase 与 IPA 对照

```bash
OOBSUM_CONFIG="WorkDir=/tmp/oobsum-tc-core Dump=options+stats" "$OOB_BUILD/bxen" -exec_mode=onlySa -cc_json=$OOB_BUILD -analysis_file=$OOB_ROOT/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-config ipa=none -analyzer-max-loop 3 -rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR"
python3 "$OOB_REPO/oobsum/tools/decisions.py" /tmp/oobsum-tc-core
```

预期：InlineDepth=0；5 条告警；decisions=7 emitted=5。引擎那条应在 Tc_Leaf 的下标访问处，主行有 [access=array-index]、元素大小和完整 Call chain。与旧版相比只多浅层 DEFINITE。若不符，带回本 TestCase 的输出（都是我们自己的代码）。

### 第 3 步：全量 export → solve → report，保存范围在这里选择

**3.1 export：把源码翻成FIR，给后面的solve使用。** 每个成功TU在fir/生成一个JSON，含目标、类型、函数列表、基本块和指令；一个JSON通常对应一个主源文件，不是一份函数摘要。
此步不做跨文件求解，不输出越界告警。已有同一批源码/编译配置的完整FIR可复用，不必为了查看摘要再次export。

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" cdb --cc-json "$OOB_CDB"
time OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
```

预期fir/逐渐增加，status列数量；失败数按cdb条目计，不等于唯一缺失文件数。若无FIR或报错，停下带回数量、退出码和本规则相关错误。

**3.2 决定是否额外保存摘要。** solve本来就会算各函数摘要并据此生成solved判定，下面的选择只控制额外留存的调试JSON，不限制实际分析哪些函数。
三种方式只选一种运行；不能把三条solve依次都跑一遍。保留相同的ModelFile、MaxBlocks等自定义选项（如上次使用过，在--extra中补回）。

| 方式 | 什么时候用 | 运行后能看什么 |
|---|---|---|
| A 不额外保存 | 只要分析/告警结果，节约磁盘 | solved是判断记录；不能由它反推任意函数的完整local/derived |
| B 仅保存已知函数 | 只复核少量函数 | 选中的local/derived；换成未保存函数需在下一次solve保存 |
| C 保存全部 | 后续可能随时查不同函数 | 本次可用函数的local/derived；之后任意已保存函数只读查询，无需重算 |

A：不设置任何函数名变量，不加--summary-function/--summary-all，**仍会完整求解**。Dump=progress只显示进度，不额外保存摘要。

```bash
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR -analyzer-config rb.enhance.core.OobSummary:Dump=progress" --run
```

B：先填要保存的准确显示函数名。下面用本仓库合成用例的Top_A/Fill示范写法，业务全量时改成自己的实际名字；不要照抄这两个示例去跑业务FIR。

```bash
OOB_SUMMARY_NAME='Top_A'
OOB_SUMMARY_NAME2='Fill'
```

赋值时不要添加尖括号或反引号。单引号是shell引用语法，**可以保留**，不会成为变量值；普通C函数名也可以不加引号。
例如这条赋值的实际值就是Top_A；写成带尖括号的名字时，尖括号会真的进入值，导致匹配失败。后续引用变量统一写双引号，兼容带空格/括号的C++显示名。
这些只是当前shell变量，无需export，脚本通过命令参数接收它们。

**不必先跑盘点脚本。** 已知道对应FIR文件时，直接用编辑器打开fir/中那个JSON，找到funcs数组：name是显示名，key是函数键，target在文件顶层。
如果看的是已保存摘要，它的名字字段叫fn；FIR里的字段叫name。FIR里的函数体不是已经求好的摘要。
不知道准确名字或对应文件时，才用下面的可选盘点；--limit只是限制展示50行，--contains只是筛选显示，底层仍需解析全部FIR，数据多时慢是预期。

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --list --contains "$OOB_SUMMARY_NAME" --limit 50
```

你已确认名字时可用下面的--skip-summary-precheck，省去Python在bxen前再解析一遍全部FIR；去掉它就会提前核对名字，但更慢。
跳过后若名字写错，得等完整solve结束才提示没有保存；它不会跳过bxen真正的FIR读取或求解。

```bash
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --summary-function "$OOB_SUMMARY_NAME" --summary-function "$OOB_SUMMARY_NAME2" --skip-summary-precheck --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR" --run
```

--summary-function可重复，不必一定给两个；它设置Dump=progress+summary并生成准确名字的DumpFilter。
选两个只表示“额外保存这两个”，不是“只分析这两个”。不传这两个参数不会影响solve本身；但若也没有另行开Dump=summary/summary-files，就不会为本次生成新的摘要索引与正文。

C：本次把所有可用函数的两种视图保存下来，日后换函数只读取。**新功能需先完成第1步同步编译**，旧二进制不认识summary-files。
不需要先设置两个函数名，不做Python全量盘点；自动启用Dump=progress+summary-files并用匹配全部的 `.*`覆盖可能遗留的DumpFilter。
summary-files与summary保存相同JSON，但不把几十万份函数JSON逐条打印到终端/log；progress和汇总计数仍可见。

```bash
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --summary-all --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR" --run
```

代价是每函数两份JSON：按本次268184个函数估算，正文文件约536368个，另有index/status；会增加磁盘空间、inode占用和写盘时间，具体体积/耗时以实测为准，不承诺和只存两个一样快。
可按复核需要选B或C；这次已经保存的两个直接读即可，不为验证新开关立即重跑25分钟。
C完成预期summary/status.json中available=matched=saved、write_failures=0、index_written=true；驱动核对本次索引与计数，不再把所有摘要正文解析一遍。
同名函数按目标+函数键分别保存；FIR盘点数与实际available可能因坏FIR或重复定义选择而不同，以实际solve计数为准。

**3.3 solve到底做了什么、进度怎么看。** stub.c只让bxen触发一次checker；真正输入是完整fir/目录。已知调用者、被调者、类型布局、全局对象与副作用共同决定结果，不能只拿两个文件假装同一全量上下文。

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

另开终端，先粘贴本节开头同一组路径变量，再观察：

```bash
cat "$OOB_WD/dump/solve/progress.json"
tail -20 "$OOB_WD/logs/solve.log"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
OOB_SOLVE_PID=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["pid"])' "$OOB_WD/dump/solve/progress.json")
ps -p "$OOB_SOLVE_PID" -o pid,etime,pcpu,rss,stat
```

阶段内completed/total不是总耗时百分比；最慢函数内部计数可能暂不动，结合CPU/RSS看。RSS和/usr/bin/time的Maximum resident set size均为KiB。
本次solve实际命令墙钟25:07.76、峰值47346220 KiB≈45.2 GiB；它包含驱动预检/尾部汇总与子进程，不是每TU耗时。
你提到的约36分钟与本次time输出25:07.76分别记录；比较性能时先统一输入、选项和计时范围。全量保存另有写盘成本。
出现崩溃、写盘失败、solve超过2小时/100 GiB时停止，带回阶段、退出码、计数和仅本规则栈，不贴业务内容。

**3.4 report：把solved判断交给CSA定位、合并、输出告警。** 它再解析对应源码，不重新进行全程序求解；同时写decisions/*.jsonl，包含报出的和未报出的判断。
仅补保存摘要而未改变分析输入/选项时，已有report不用重跑；从新一批输入得到solved或要验证新格式时执行下面这一遍。

```bash
time OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL"
cp "$OOB_REPORT" "$OOB_WD.report.json"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" stats --workdir "$OOB_WD" --counts-only --top 50
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" report "$OOB_WD.report.json"
```

OOB_REPORT的源必须是文件，cp -r整个log目录不能替代复制JSON。若仍是目录，先修正开头变量。
预期stats有所有标签/原因与物理告警计数，report列JSON中的数量；不一致先核查失败TU和合并口径，不直接认为漏报。

### 第 4 步：读取已经保存的摘要；脚本与直接打开文件任选

**这里没有生成或保存动作。** summary --function只读index，按函数名/键定位一个文件，再读取local或derived正文打印；不扫描fir/，不调用bxen，不重新solve。
summary --list则是从FIR盘点可用函数，已索引摘要数是附带统计；它与查看一份保存的正文是两种动作，没有必要每次先盘点。
保存位置：$OOB_WD/dump/solve/summary/；当前路径变量对应/usr1/oobsum-work/full-b-20261103/dump/solve/summary/。

| 文件 | 内容与用途 |
|---|---|
| index.json | 数组中每项有fn、key、target、tu、stem，用fn/key找到对应正文；摘要粒度是“目标内的函数” |
| stem.local.json | 该函数解释所得的实际local事实，作为传播输入；有参数、Req、Eff、调用点及当时实参/区间、未证明项目 |
| stem.derived.json | 保留local的参数/调用点/效果等，替换为传播后Req与reps、补入口opens，展示调用者上下文中的传递结果 |
| status.json | available/matched/saved/write_failures等计数；只反映保存状态，不是告警统计 |

stem是“目标+函数键”的哈希文件名；与判断fp不是同一种东西。打开index找到想看的fn，复制同条的stem，再在文件管理器/编辑器中打开那两个JSON即可。
普通文本编辑器就能查看，以下json.tool也只是格式化读取；变量只在需要看文件时填写，把示例字符串换成index中的实际stem。

```bash
python3 -m json.tool "$OOB_WD/dump/solve/summary/index.json"
OOB_SUMMARY_STEM='从index中复制的实际stem'
python3 -m json.tool "$OOB_WD/dump/solve/summary/$OOB_SUMMARY_STEM.local.json"
python3 -m json.tool "$OOB_WD/dump/solve/summary/$OOB_SUMMARY_STEM.derived.json"
```

或者按函数名使用脚本，不用自己拼哈希文件名；第3步B已设置名字可直接沿用，选A/C但现在要查看时才设置。下面仍是自有Top_A/Fill写法示范，请换成自己要看的名字，不加尖括号/反引号。

```bash
OOB_SUMMARY_NAME='Top_A'
OOB_SUMMARY_NAME2='Fill'
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view local
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME" --view derived
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME2" --view local
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" summary --workdir "$OOB_WD" --function "$OOB_SUMMARY_NAME2" --view derived
```

预期返回JSON且退出0；同名static/跨目标函数有多项时工具不会任选，改用index里的key给--function，并用--target给完整目标字符串。
索引没有该函数时只会提示缺失，不会自动启动求解；先确认实际名字和目标，再判断上次是否保存。等本次done且摘要保存成功后再按当前index查看，写盘过程中可能还有上次产物。
后续不开启保存的solve不会刷新旧摘要；输入/模型/选项变化时应使用新的WorkDir，或明确重新保存，避免把旧索引当本次结果。
若上次选C且保存成功，换任意已索引函数只需改变量再读；若上次只选两个，新的第三个未保存，就必须在下一次solve选它或选C。
solved含的是判断，不含所有函数的完整local/derived；FIR含的是指令，也不能直接当摘要；无法凭现有solved免费恢复未落盘的完整摘要。
你这次两个均已保存，因此现在直接读取，不需要为它们再跑solve。旧Dump=summary且不设Filter也能存全部，但会把全部local JSON打印到日志；新C专门避免这一份庞大重复输出。

**local/derived的区别，用自有Top_A → Fill说明：** Fill写入形参长度n要求的字节；Top_A给出16字节数组与长度17。
local的Top_A记录“调用Fill，传入对象大小16与n=17”等实参事实；传播会把Fill的访问要求代入这些实参，derived的Top_A因而可以含一条跨调用的POSSIBLE及证据链（具体级别以本用例的界来源为准）。
local并非只扫描本函数语法：analyze时已经应用已知被调者的效果；derived的重点是上传要求和在当前上下文已定级的结果。
两个都是同一函数的视图；derived没有reps不等于安全，要求可能继续上传，opens也可能仍未证明；是否报警最终看report策略与decisions日志。
先在local中看leaf的Req公式/守卫和上层calls实参，再看derived中对象大小、末端与调用链，便于核对推理是否对应源码。

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

### 第 5 步：尾数组对照（复用新 FIR，无需再次 export）

默认 FlexibleArrayMax=4 / ReportFlexibleArray=false；结构体最后一个静态数组元素数<=4 是约定候选，[] 始终为候选。设置0只保留[]/[0]。
这项是报告策略，不代表证明了安全。默认日志仍有原标签，not_emitted_because=flexible_array。普通固定内层数组、非末尾成员、union仍报告。
为避免覆盖默认结果，把 FIR 复制到独立工作目录，打开报告再 solve / report：

```bash
OOB_COMPARE=$OOB_WD.flex-on
mkdir -p "$OOB_COMPARE"
cp -a "$OOB_WD/fir" "$OOB_COMPARE/"
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_COMPARE" --bxen "$OOB_BUILD/bxen" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR -analyzer-config rb.enhance.core.OobSummary:Dump=progress -analyzer-config rb.enhance.core.OobSummary:ReportFlexibleArray=true" --run
time OOBSUM_CONFIG="WorkDir=$OOB_COMPARE Phase=report ReportFlexibleArray=true" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" stats --workdir "$OOB_COMPARE" --counts-only --top 50
```

预期：两遍原始判断标签相同，打开后候选上的判断恢复报警，默认过滤原因的计数相应消失；只差报告策略和物理分组。
在公司抽查候选：确认真的是末尾数组的分配约定；已知实际分配大小的真越界也可能被该策略过滤，不能据此宣布代码安全。
如果要改阈值，solve / report 两阶段统一传 FlexibleArrayMax=0 或所需值，FIR 已保留原声明元素数。

### 第 6 步：检查告警格式并抽查

打开两条已确认问题，检查：位置是实际下标 / 成员 / 解引用 / 内存函数行；主行含 Access、Element view、Range / exclusive end、Reason、Context 与完整 Call chain。
20 字节结构体、3 个元素的可用容量应能直接看出 0..2 是合法位置；exclusive end=64 表示末端不包含 64，对 60 字节对象最多越 4 字节。
同一个 sink 行只应有一条 warning，下一行源码、再下一行 caret；不同理由 / 上下文用 Also 合并。全部原判断仍在 decisions 日志，report_group 标识对应 warning。
旧收集报告若含多个 warning 被粘在同一 reason 内，report 工具会提示；已有报告不能自动修回丢失的源码行，须重新 report。
`type_range_only` 的人类说明是“没找到可用的代码约束”；机器原因名保留以兼容脚本。全局指针的告警应列出表达式和可能的全局来源。

### 第 7 步：场景分布与专家复核

```bash
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --workdir "$OOB_WD"
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --workdir "$OOB_WD" --multi
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --workdir "$OOB_WD" --unit decisions --include-unemitted --json > "$OOB_WD.scenes-all.json"
python3 "$OOB_REPO/oobsum/tools/scene_stats.py" --report "$OOB_WD.report.json"
```

预期：LV1..LV10 与 UNCLASSIFIED 都有计数；默认主类取最高已观察等级，--multi 的多标签合计可大于总数。WorkDir 默认按 report_group 计物理告警，--unit decisions 计原判断。
这是 observed_features，不能仅凭层数分类，也不表示某一级全部能力支持。Lv10 业务语义要人工判定，自动不猜；旧报告没有标签时列未分类。
可在公司创建 annotations JSON，以指纹或 group 编号映射到 ["LV10"] 等，再传 --annotations；该文件只留在公司。
脚本默认只输出计数，不列函数名、源码路径或告警正文。完整解释见 10-analysis-and-diagnostics.md（包括复杂度、摘要、字节末端与模型边界）。

复核两条问题时，记录的只是：访问类别、读 / 写、元素大小、已知容量、下标 / 长度的数值或抽象范围、是否来自类型界、哪个比较保护了哪个对象。
不要根据“给变量取地址”直接判断误报，也不要把对第2实参的写误看成第1实参的数组读；以 sink 的操作和 local / derived 条件为证据。
3 元素结构体的 64/60 可能是索引3的4字节字段（3*20+4），合成的严格 <n 反例已证明安全；业务自己的64仍须核查来源，不直接定性。

### 带回模板

只带计数、自有测试输出和本规则报错；业务函数名、文件名、路径、告警正文与摘要正文都留在公司。
已经确认的盘点/两函数solve不再重复填；本次若只阅读现有摘要，填可读性与抽查即可，其它写“未重跑”。

```text
清单版本：2026-11-04-1
1. 本地 lit __；csa 回退 __；同步/公司编译 __（未重跑可注明）
2. TestCase：InlineDepth=__ decisions=__ emitted=__，告警 __
3. 本批cdb条目/唯一文件/C/C++ __；编译命令仍不同的文件 __；export成功/失败 __；FIR __；目标组数 __
4. solve方式 A/B/C __；是否跳过Python预检 __；Elapsed __；Maximum resident set size __；solved __；判定 __
   summary available/matched/saved/write_failures __；index_written __；summary阶段可见 __；摘要目录du/inode计数（若选C）__
5. local/derived可直接打开 __；脚本查看 __；选C时换一个未预选函数能否直接读 __；是否有逐函数JSON大量刷屏 __
6. report成功/失败 __；默认和flex-on的stats --counts-only全文；report计数输出
7. 各原因抽查：看了 __、真问题 __、误报 __、看不出 __；写法概括 __
8. 场景统计默认/multi计数；两条问题的元素大小/容量/范围/标签/判断 __（只给抽象写法）
9. 异常：阶段 __、退出码 __、仅本规则栈 __
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

标签在方括号里（`[OOB-DEFINITE]`、`[OOB-SUSPECT:not_in_tu]`），可以按它筛。默认sink格式还将完整调用链写在warning主行，note是附加展开；要看已保存摘要和判断上下文可直接读JSON，或用§0第4步的只读查询。`oobsum_run.py report <report.json>` 按标签 / 原因数本规则的告警（只有数字，可以带回）。

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

预期：5 条告警——调试内建函数的浅层 DEFINITE 与 `[OOB-DEFINITE]`、`[OOB-POSSIBLE]`、`[OOB-SUSPECT:entry_unproven]`，加 `Tc_Engine_TP` 处的
`[OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain`（带调用链 note）；`[oobsum:stats] … decisions=7 emitted=5`；
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
* 磁盘：FIR/solved/摘要/日志分别计量，不能固定按源码倍数估算。只要少量摘要用--summary-function，想日后任意查看用--summary-all；它只落盘不逐函数刷屏，但每函数两份文件会增加体积/inode/写盘时间，§0第3步列出取舍。
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
未保存的函数只能在未来一次solve中选择保存，不能从solved免费恢复；全量保存/少量保存/不保存的完整命令与代价见§0第3步。

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
