# 公司侧：在公司电脑上同步、编译、跑 OobSummary

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen / clang_check 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。
>
> **怎么用这个文件**：每次需要你在公司做事，Claude 都会重写 §0（带清单版本号，CLAUDE.md §5 第 8 条）——`git pull` 后只看 §0、照着做、把最后的模板填好贴回来。
> §1 讲这个规则是怎么跑的、告警怎么看；§2 是各项操作的完整命令（§0 里的步骤都来自这里）；§3 是首次搭建（已完成）。

## 0. 下一次在公司按顺序做

**清单版本：2026-11-03-4**（SPEC-0122 / 0123 / 0124 / 0125 / 0126；保留前版步骤；新增默认 sink 告警、元素范围、完整主行调用链与同一行合并；结果不符合预期就停下，带回计数和本规则栈。）

路径只改这里；`OOB_REPORT` 必须指向 JSON 文件，不能是 log 目录：

```bash
OOB_REPO=/usr1/oobsum-llvm
OOB_ROOT=/usr1/BXPlugin
OOB_BUILD=/usr1/BXPlugin/cmake-build-release-release
OOB_CDB=/usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all
OOB_GBL_OLD=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.ipa.OobSummary/gbl_config.json
OOB_GBL=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/gbl_config.json
OOB_REPORT=/usr1/BXPlugin/xir/llvm_ast/sa_check/rb.enhance.core.OobSummary/log/allfiles-buffer_overflow_report.json
OOB_WD=/usr1/oobsum-work/full-b-v2
OOB_INI=/usr1/clang_check/src/cfg/ini/config.ini
OOB_FUNCPTR=/usr1/clang_check/src/cfg/ini/config_funcptr.ini
OOB_SUMMARY_FILTER='^(<待查函数1>|<待查函数2>)$'  # 从 explain 打出的链挑函数；只在公司填，不带回名字
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

同一源文件若有不同宏 / ABI 配置，现有 TuId 只保存一个变体。先看 cdb 新增的差异计数；若非 0，先在公司确认差异是否真的影响预处理 / ABI，带回计数与概括，不贴命令或文件名。不能把重复条目当成全部等价。

全板有结果不表示每个 TU 都成功：10262−10245=17 个唯一文件无 FIR，report 与 solve 也少了 861 条判定。条目失败数不能直接当成缺失文件数（cdb 有重复）。

### 第 1 步：更新、同步、编译和改规则配置

```bash
cd "$OOB_REPO" && git pull
ninja -C "$OOB_REPO/cmake-build-release" clang
"$OOB_REPO/cmake-build-release/bin/llvm-lit" -s "$OOB_REPO/cmake-build-release/tools/clang/test/Analysis/oobsum"
python3 "$OOB_REPO/oobsum/cases/run.py" csa
python3 "$OOB_REPO/oobsum/devenv/land.py" --drop "$OOB_REPO/oobsum/landing/OobSummaryChecker.manifest" --into bxplugin --root "$OOB_ROOT" --apply
make -C "$OOB_BUILD" -j16
mkdir -p "$(dirname "$OOB_GBL")"
cp "$OOB_GBL_OLD" "$OOB_GBL"
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

### 第 3 步：全板重新跑 B

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" cdb --cc-json "$OOB_CDB"
time OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=export" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
/usr/bin/time -v python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" solve --workdir "$OOB_WD" --bxen "$OOB_BUILD/bxen" --extra "-rbchecker-config=$OOB_INI -funcptr-config=$OOB_FUNCPTR -analyzer-config rb.enhance.core.OobSummary:Dump=progress+summary -analyzer-config rb.enhance.core.OobSummary:DumpFilter=$OOB_SUMMARY_FILTER" --run
time OOBSUM_CONFIG="WorkDir=$OOB_WD Phase=report" /usr/local/bin/python3 /usr1/clang_check/src/main.py -e "$OOB_BUILD/" -j "$OOB_CDB/compile_commands.json" -r rb.enhance.core.OobSummary -c "$OOB_GBL"
cp "$OOB_REPORT" "$OOB_WD.report.json"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" stats --workdir "$OOB_WD" --counts-only --top 50
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" report "$OOB_WD.report.json"
```

预期：export 只在 fir/ 写每个 TU 的中间表示，不求解、不告警；solve 读取这些文件，stub.c 只触发一次 checker 回调，不是业务分析对象；report 将预计算的判定交给 CSA 输出并写 decisions/。
预期：solve 终端与 logs/solve.log 会实时出现 [oobsum:progress]，依次是 read / analyze / propagate / decisions / write / done；多目标时中间阶段带 group1/2、group2/2 前缀；阶段内计数不是总耗时百分比。
在另一个终端粘贴同一组路径变量后，可以看：

```bash
cat "$OOB_WD/dump/solve/progress.json"
tail -20 "$OOB_WD/logs/solve.log"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" status --workdir "$OOB_WD"
ps -p <progress.json里的pid> -o pid,etime,pcpu,rss,stat
```

read 在解析已导出的全部 FIR，analyze 在按被调者优先解释每个函数，propagate 在调用点代换与上传条件，decisions 在生成标签与证据，write 才批量写 solved/。
因此 analyze / propagate 期间没有新增 solved 文件是正常的；一个慢函数内部计数可能暂不变化，结合 CPU / RSS 判断，不能仅凭计数不动认定卡死。
RSS 的单位是 KiB；/usr/bin/time -v 的 Maximum resident set size 也是 KiB。上次约 29.4 GiB，终端的 elapsed 是墙钟时间。

时间基准是上次同一单板约 7 / 20 / 7 分钟，不是承诺值。出现崩溃或 solve 超 2 小时 / 100 GiB 时停止，记录退出码和本规则栈。
`cp` 的源必须是文件；`cp -r` 复制整个 log 目录不能替代复制 JSON。

### 第 4 步：在公司看两条问题的摘要

先在旧的完整 WorkDir 上用 decisions.py 挑两个指纹（不要复制业务告警到仓库），用下面命令看调用链：

```bash
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" explain --workdir /usr1/oobsum-work/full-b <指纹1>
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" explain --workdir /usr1/oobsum-work/full-b <指纹2>
```

把链上要看的函数写进开头 OOB_SUMMARY_FILTER 再跑第 3 步。新导出的 FIR 中每条访问有 diag 编号和 accesses（表达式、读写类型、元素大小、字段偏移、来源）；判定的 access_key / access_ids / sink 可对应回该指令。须重新 export 才有这些信息，旧 FIR 可读但没有新描述。

新 solve 的 dump/solve/summary/index.json 列出选中函数、键、TU 与文件 stem；
索引 target 区分架构；同名函数的 stem 不同。同一个 stem 的 .local.json 是真实的函数解释事实，.derived.json 是调用传播后的 Req / reps 视图。索引与摘要只在公司打开，不带回名字或正文。

```bash
python3 -m json.tool "$OOB_WD/dump/solve/summary/index.json"
python3 -m json.tool "$OOB_WD/dump/solve/summary/<索引里的stem>.local.json"
python3 -m json.tool "$OOB_WD/dump/solve/summary/<索引里的stem>.derived.json"
python3 "$OOB_REPO/oobsum/tools/oobsum_run.py" explain --workdir "$OOB_WD" <新判定的指纹>
```

按 warning 的 group=编号可直接用 explain 查询组内全部上下文；同一行主标签取最严重的一条，不能把所有上下文都当作 DEFINITE。

先从 leaf 的 Req 看访问公式与守卫，再看上层调用实参和 derived 的具体大小 / 末端。摘要里的链是证据，不是重复执行全部路径；报告策略在 report 阶段才应用，是否报出以 decisions/*.jsonl 的 emitted 为准。
如果原全量 solve 没开 Dump=summary，不存在可供读取的摘要文件；仅 report 不会补出全程序的闭包，需重新 solve（可以复用 FIR）。
不要只取两条调用链的文件冒充完整上下文；副作用和其它被调者也会影响结果。生成两种摘要不会改变 solved 的判定。

### 第 5 步：检查告警格式并抽查

打开两条已确认问题，检查：位置是实际下标 / 成员 / 解引用 / 内存函数行；主行含 Access、Element view、Range / exclusive end、Reason、Context 与完整 Call chain。
20 字节结构体、3 个元素的可用容量应能直接看出 0..2 是合法位置；exclusive end=64 表示末端不包含 64，对 60 字节对象最多越 4 字节。
同一个 sink 行只应有一条 warning，下一行源码、再下一行 caret；不同理由 / 上下文用 Also 合并。全部原判断仍在 decisions 日志，report_group 标识对应 warning。
旧收集报告若含多个 warning 被粘在同一 reason 内，report 工具会提示；已有报告不能自动修回丢失的源码行，须重新 report。
`type_range_only` 的人类说明是“没找到可用的代码约束”；机器原因名保留以兼容脚本。全局指针的告警应列出表达式和可能的全局来源。

### 带回模板

按 §1.6 的方法抽查；业务代码、名字、路径与告警原文不要带回，只给计数和对写法的概括。

```text
清单版本：2026-11-03-4
1. 本地 lit __；csa 回退 __；同步 / 公司编译 __
2. TestCase：InlineDepth=__ decisions=__ emitted=__，告警 __
   进度 read/analyze/propagate/decisions/write/done 是否可见 __；两种摘要是否可读 __
3. cdb 条目 / 唯一文件 / C / C++ __；去掉输出文件选项后命令仍不同的文件 __；export 成功 / 失败 __；FIR __；目标组数 __
4. solve Elapsed __；Maximum resident set size __；solved __；判定 __；report 成功 / 失败 __
5. stats --counts-only 全文（含报出判定 / 物理告警两种计数）；report 输出
6. 抽查（每个原因）：看了 __、真问题 __、误报 __、看不出 __；写法概括 __
7. 异常：阶段 __、退出码 __、仅本规则栈 __
```

## 1. 这个规则是怎么跑的

### 1.1 摘要是什么

每个函数**只分析一次**，得到它的摘要：它对实参的要求（例如"写 `p[0 … n]`，要求 `n < p 指向的对象大小`"）和它对外的效果（写了哪个成员、返回值的范围……）。
在每个调用点把**实参代进**被调者的摘要：能判的就地判（越界 → DEFINITE / POSSIBLE，证明了 → SAFE）；判不了的，这条要求换成调用者的形参，变成调用者的要求，
继续往上传——直到碰到给出缓冲区和长度的那一层。所以 3 层、5 层、20 层的链都一样处理（本地测过 20 层），告警报在"界和长度第一次同时已知"的那个调用点，
note 里列出整条调用链。

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
  跨文件时**不导入 AST**，只交换每个文件导出的中间表示；solve 按调用图自底向上算、在调用点代换。代价大致和代码量成正比，链多长都行。

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

标签在方括号里（`[OOB-DEFINITE]`、`[OOB-SUSPECT:not_in_tu]`），可以按它筛。调用链在 note 行里；clang_check 不收 note 的话报告里只有第一行，
要看链和中间的摘要就用 §2.8。`oobsum_run.py report <report.json>` 按标签 / 原因数本规则的告警（只有数字，可以带回）。

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
* 磁盘：WorkDir 大约是源码大小的 10 倍（`fir/` 缩进写占大头）。全量不要加 `Dump=summary`（`dump/` 还要再大 2 倍）。
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

### 2.8 单独跑一个文件、看一条判定的来龙去脉

```bash
rm -rf /usr1/oobsum-work/one && mkdir -p /usr1/oobsum-work/one
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=<cdb 目录>/ -analysis_file=<那个文件> -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-config ipa=none -analyzer-max-loop 3 -analyzer-config rb.enhance.core.OobSummary:WorkDir=/usr1/oobsum-work/one -analyzer-config rb.enhance.core.OobSummary:Dump=summary+apply -analyzer-config rb.enhance.core.OobSummary:DumpFilter=<函数名正则> -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py explain --workdir /usr1/oobsum-work/one <函数名或指纹>
```

`explain` 列出这条判定的调用链和链上每个函数的摘要（`Dump=summary` 跑的才有）。跨文件的判定：对 B 的 WorkDir 执行 `explain`。

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
