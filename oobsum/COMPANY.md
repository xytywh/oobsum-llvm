# 公司侧：从 GitHub 拿代码、同步进 BXPlugin、编译、用 bxen 验证

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。

## 0. 下一次在公司按顺序做（2026-10-26 版，做完一步再做下一步）

> 每一步都写了"预期"。不符合就停下，把那一步的输出贴回来——只贴计数和**我们自己代码**的报错；不贴业务代码、函数名、文件名、告警原文。
> 下面 `<…>` 是要换成你那边的路径的地方。bxen 命令都是一行，IDE 的运行配置和终端都能直接粘贴。

**第 1 步　更新、编译（约 20 分钟）**

```bash
cd /usr1/oobsum-llvm && git pull && git log --oneline -1
ninja -C cmake-build-release clang
cmake-build-release/bin/llvm-lit -s cmake-build-release/tools/clang/test/Analysis/oobsum
python3 oobsum/cases/run.py csa
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

预期：lit `Passed: 105`；`run.py csa` 最后两行 `通过 108，通过+S 20，级别不同 2，超出预期 4`、`回退 0 条`；make 不报错（这次多了一个头文件 `OobSummary/core/AtomicFile.h`，land.py 会一起同步）。

**第 2 步　TestCase（2 分钟）**：§3 的命令原样跑。预期 4 条告警、`[oobsum:stats] … decisions=7 emitted=4`。

**第 3 步　67 个用例对照（5 分钟）**：cdb 用上次生成的 `/usr1/oobsum-cases-cdb`，不用重新生成；WorkDir 先清空。

```bash
rm -rf /usr1/oobsum-cases-wd && mkdir -p /usr1/oobsum-cases-wd
# 跑 §3.1 的那条 bxen 命令（-cc_json=/usr1/oobsum-cases-cdb/ … WorkDir=/usr1/oobsum-cases-wd … Dump=summary）
python3 /usr1/oobsum-llvm/oobsum/cases/run.py csa --from-workdir /usr1/oobsum-cases-wd
```

预期：`通过 106，通过+S 20，级别不同 2，超出预期 4，需跨TU 2`；G2 那行 `0 个`；`回退 0 条`；"变化"只有 5 行——LV1-07、LV6-01 各两行（单文件跑，要跨 TU）、
LV9-02 一行（64 位上多 2 条 SUSPECT）。

**第 4 步　几个架构的目标（1 分钟）**：上次每个架构跑过一个文件的 WorkDir，各执行一次：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir <那个架构的 WorkDir> | head -1
```

记下每个的"目标："一行（§3.2 有解释）。不管结果怎样都接着往下做；单板的 cdb 如果显示 `x86_64 … ptr=8`，告诉我，我来处理。

**第 5 步　看全量 cdb、挑一个子系统（5 分钟）**：用**单板架构**的 cdb（不是 x86 仿真版），挑一个几百个文件的子系统目录。

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py cdb --cc-json <全量 cdb 目录>
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py cdb --cc-json <全量 cdb 目录> --under <子系统目录> --out /usr1/oobsum-cdb/sub
```

第一条打印全量的条目数、文件数、出现多次的文件数；第二条写出只含这个子系统的 `/usr1/oobsum-cdb/sub/compile_commands.json`，下面让 clang_check 用它
（clang_check 自己能按目录过滤的话，用它自己的办法也行）。

**第 6 步　子系统上跑 A：默认模式（§4.2 A）**

1. clang_check：`-checker=rb.enhance.ipa.OobSummary`，`-extra_args` 用 §4.2 A 那串，WorkDir 写 `/usr1/oobsum-work/sub-a`（先确认这个目录不存在或是空的）。记下总耗时。
2. 跑完：
   ```bash
   python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work/sub-a --counts-only
   du -sh /usr1/oobsum-work/sub-a
   ```
   `--counts-only` 只打印计数，整段可以带回；不加它会多两张表，列出报得最多的函数名、文件名，那两张只给你自己看。
3. 抽查：在 clang_check 的报告里，DEFINITE、POSSIBLE 各挑 5–10 条，你判断是不是真问题。只带回条数，误报用你自己的话描述它是什么写法
   （或写一个不含业务代码的最小例子）。

**第 7 步　子系统上跑 B：跨文件（§4.2 B）**

1. clang_check 第 1 遍：A 那串再加 `-analyzer-config rb.enhance.ipa.OobSummary:Phase=export`，WorkDir 写 `/usr1/oobsum-work/sub-b`。记下耗时。
2. solve，在终端里跑（要用 `time` 量耗时和内存）：
   ```bash
   python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir /usr1/oobsum-work/sub-b --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
   /usr/bin/time -v python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir /usr1/oobsum-work/sub-b --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" --run
   ```
   第一条只打印命令，看一眼；第二条执行，记下 `time -v` 输出里的 `Elapsed (wall clock) time` 和 `Maximum resident set size`。
3. clang_check 第 2 遍：同一个 cdb，`Phase=export` 换成 `Phase=report`，WorkDir 不变。记下耗时。
4. `stats --workdir /usr1/oobsum-work/sub-b --counts-only`、`du -sh /usr1/oobsum-work/sub-b`。

**第 8 步　带回来**（照这个格式填，贴进对话就行）

```text
1. git log -1：
2. lit / run.py csa：Passed __；通过 __、回退 __
3. TestCase：告警 __ 条；decisions=__ emitted=__
4. 用例对照：合计那行；G2 那行；"变化" __ 行
5. 目标：<架构 1> → ____；<架构 2> → ____；<架构 3> → ____
6. 全量 cdb：__ 条、文件 __ 个、重复 __ 个；子系统：__ 条
7. A：耗时 __；du __；stats --counts-only 全文
8. B：export 耗时 __；solve Elapsed __、Maximum resident set size __；report 耗时 __；du __；stats --counts-only 全文
9. 抽查：DEFINITE 看了 __ 条、真问题 __ 条；POSSIBLE 看了 __ 条、真问题 __ 条；误报是什么写法（自己的话）
10. 有没有崩溃 / 报错：bxen 退出码；崩溃栈里带 OobSummary 的那几行
```

全量（整个 cdb）和选项对照（§4.1）等这些数回来、我看过再做。

## 1. 第一次：只取需要的部分（不用拉整个 llvm-project 源码）

```bash
cd /usr1
git clone --filter=blob:none --no-checkout --branch oobsum/dev <你的私有仓库地址> oobsum-llvm
cd oobsum-llvm
git sparse-checkout set --no-cone /CLAUDE.md /oobsum/ \
    /clang/include/clang/StaticAnalyzer/Checkers/Checkers.td \
    /clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp \
    /clang/lib/StaticAnalyzer/Checkers/OobSummary/ \
    /clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt \
    /clang/test/Analysis/oobsum/
git checkout oobsum/dev
git log --oneline | head -30          # 完整的提交记录：每一步做了什么、对应哪个 SPEC
```

部分克隆只下载检出文件的内容（几 MB），提交历史与目录结构照常可看（`git log -p -- oobsum/specs/` 看规格的演进）。

## 2. 同步进 BXPlugin、编译

```bash
cd /usr1/oobsum-llvm
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin            # 先演示：列出要拷 / 删的文件与三处登记的 diff
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply    # 再落地
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

`land.py` 做的是 `csa-checker-dev` 的五处落点，并且是**同步**：

| 本仓库 | BXPlugin |
|---|---|
| `clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp`、`OobSummary/**` | `xir/llvm_ast/StaticAnalyzer/Checkers/` 同名路径（`OobSummary/` 里多出来的旧文件会删掉） |
| 上面的 `.cpp` | `xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` 的 `add_library()` 末尾追加（`OobSummary/` 下已删除的文件去掉登记） |
| `Checkers.td` 里的 `def OobSummaryChecker` | `xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` 的 `RosaRBEnhanceIpa` 块最后（已有则整段替换） |
| `oobsum/testcase/OobSummaryChecker/` | `xir/llvm_ast/TestCase/OobSummaryChecker/`，并在 `TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(OobSummaryChecker)` |

找不到预期结构（比如包块结尾不是 `} // end "rb.enhance.ipa"`）会停下来，不猜；把报错贴回来改脚本。
改了 `Checkers.td` 后如果新选项不生效，多半是 `Checkers.inc` 没重新生成：做一次完整编译（`csa-checker-dev` 的 TableGen 注意事项）。

## 3. 跑测试用例

```bash
mkdir -p /tmp/oobsum-tc
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/oobsum-tc -analyzer-config rb.enhance.ipa.OobSummary:Dump=options+stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /tmp/oobsum-tc
```

预期（v4.0 骨架）：3 条告警 `[OOB-DEFINITE]`、`[OOB-POSSIBLE]`、`[OOB-SUSPECT:entry_unproven]`；`decisions.py` 显示 5 条判定，
其中 2 条没报（`safe`、`inline_depth`）。引擎接上之后（SPEC-0021，默认 `Phase=inline`）：多一条 `Tc_Engine_TP` 处的
`[OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain`，共 4 条告警、7 条判定（`Dump=stats`：`decisions=7 emitted=4`）。每个版本的预期写在 `TC_OobSummaryChecker.c` 的文件头与对应 SPEC 的"公司侧验证"一节。

### 3.1 用 bxen 跑 67 个分级用例，和本地的 `run.py csa` 对照（SPEC-0111）

```bash
cd /usr1/oobsum-llvm
python3 oobsum/cases/run.py cdb --out /usr1/oobsum-cases-cdb --cc /usr/local/bin/cc   # 与 TestCase 同一个编译器；自动写它的系统头文件目录（SPEC-0115）
mkdir -p /usr1/oobsum-cases-wd
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/oobsum-cases-cdb/ -analysis_folder=/usr1/oobsum-llvm/oobsum/cases -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-cases-wd -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 oobsum/cases/run.py csa --from-workdir /usr1/oobsum-cases-wd       # 不调 clang，读 bxen 写的 WorkDir 对照
```

预期（与本地同一版本）：`合计 134 个入口：通过 106，通过+S 20，级别不同 2，超出预期 4，需跨TU 2`、`回退 0 条`（单 TU 跑，跨文件的 LV1-07、LV6-01 显示"需跨TU"）。
`Dump=summary` 必须带（对照要用调用图）。不一致就把这张表贴回来；按子目录分开跑、每个子目录一个 WorkDir 也行，`--from-workdir` 给它们的父目录。

* bxen 不会自己找 gcc 的系统头文件（`stddef.h` 等），`compile_commands.json` 的命令里要显式写 `-isystem`（TestCase 的 CMakeLists 就是这么加的）。
  `run.py cdb` 用 `<cc> -E -x c - -v` 问出这些目录、按顺序写进去，打印"系统头文件目录 K 个"；和 BXPlugin 的 `compile_commands.json` 里 TestCase 那条的
  `-isystem` 对一下应当相同。还缺参数用 `--flags="…"` 追加（要带等号，因为值以 `-` 开头；SPEC-0115）。
* rb.enhance.ipa 规则：`-extra_args` 里**不加** `-analyzer-config ipa=none`（它不走 CSA 的 inline，只挂 TU 级回调）。
* `-analyzer-config` 的列表值用 `+` 连接（`Dump=options+stats`），逗号会被当成分隔符。

### 3.2 看分析时用的目标（几个架构的 cdb 各跑各的，SPEC-0117）

每次运行都会在 `WorkDir/fir/<tu>.json` 里记下 bxen 分析这个 TU 时用的目标（不用加 `Dump`）。`stats` 的第一行起就是它：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/1383-all | head -3
# 目标：<三元组> ptr=<指针字节数> long=<long 字节数> <big-endian 大端 | little-endian 小端>（K 个 TU）
```

| 看到的 | 意思 | 怎么办 |
|---|---|---|
| `ptr=4 long=4 big-endian` | 按单板（ILP32 + 大端）算字节布局 | 可信，什么都不用改 |
| `ptr=8 long=8 little-endian`（`x86_64…`），而这个 cdb 是单板产品 | bxen 按宿主 x86_64 算：含指针 / `long` 的结构体大小、成员偏移会算大；纯 `UINT8/16/32` 的数组、下标、常数长度不受影响 | 把这一行带回来，我们再定（SPEC-0117 §9） |
| x86 仿真版本（如 `lx86`）显示 `ptr=8 … little-endian` | 本来就是 64 位 x86 | 正确 |

* **布局本来就会影响结果，这是对的**：越界是按字节算的，同一个结构体在 32 位上 8 字节、在 64 位上 16 字节，`memcpy_s(&st, 8, …)` 在一个架构上安全、另一个上越界。
  要保证的是"每个 cdb 都按它自己真实的目标算"，不是"几个架构结果一样"。67 个用例上实测：32 位和 64 位的判据完全相同，只有 LV9-02 在 64 位上多 2 条
  SUSPECT（`UINT32` → 64 位 `size_t`），以及字段偏移这类字节数不同。
* 几个架构都要看时：每个 cdb 一条 bxen 命令、各自一个 WorkDir（`/usr1/oobsum-work/1383`、`…/1380`、`…/lx86`），分别看 `stats`；以产品实际运行的架构为准。

## 4. 跑业务代码（引擎接上之后）

```bash
mkdir -p /usr1/oobsum-work
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=<compile_commands.json 所在的目录>/ -analysis_folder=<目录1>,<目录2> -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work                 # 按标签 / 原因计数
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work --emitted --jsonl review.jsonl   # 给大模型复核
```

上面是默认的 `Phase=inline`（单个 TU 内推到底，跨文件的被调者记 `not_in_tu`）。**跨 TU**（06- §7 的 export → solve → report，SPEC-0028…0031）用驱动生成三段命令：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py bxen --workdir /usr1/oobsum-work \
  --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --cc-json <业务工程目录> --analysis-folder <目录1>,<目录2> --extra "-analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
# 打印 export、solve 两段（solve 用生成的桩 TU：/usr1/oobsum-work/solve_stub/）；跑完这两段后再执行同一条命令，打印 report 那段
#（report 的 cdb 从原 cdb 原样复制有判定的 TU：/usr1/oobsum-work/report_cdb/）。加 --run 直接执行；status 子命令看到哪一步了
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py status --workdir /usr1/oobsum-work
```

上面两种都是"一条 bxen 命令跑几个目录"的写法，只用于挑子目录试跑。**全量用 clang_check 逐个文件并行跑，见 §4.2。**
业务的分配器、拷贝封装、纯函数（07- 输入 I5 / I8 / I9）写进模型文件：复制 `oobsum/config/oob_models.example.yaml` 改，`-extra_args` 里加
`-analyzer-config rb.enhance.ipa.OobSummary:ModelFile=<路径>`；`Dump=models` 看实际生效的表（SPEC-0033）。模型文件留在公司，不带回。
定位问题时加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+apply -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=<函数名正则>`。

看结果（SPEC-0112，只读 WorkDir）：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work          # 标签 × 层数、SUSPECT 按原因、没报的原因、热点函数 / TU
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py explain --workdir /usr1/oobsum-work <函数名或指纹>   # 一条判定的来龙去脉（带 Dump=summary 跑的会列链上各函数的摘要）
```

### 4.0 业务代码一步一步（给一个 compile_commands.json，同其他 CSA 规则的跑法）

1. **更新**：`cd /usr1/oobsum-llvm && git pull`；`land.py … --apply`（§2，头文件搜索路径会自动补，不用手改）；`make -j16`。
2. **默认模式跑**（单 TU，一条 bxen 命令先挑一两个子目录试；**全量用 clang_check，见 §4.2**）：
   ```bash
   mkdir -p /usr1/oobsum-work/base
   time /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=<compile_commands.json 所在的目录>/ -analysis_folder=<子目录1>,<子目录2> -checker=rb.enhance.ipa.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work/base -analyzer-config rb.enhance.ipa.OobSummary:Dump=stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
   ```
   * 被调者在别的文件里：记 `SUSPECT:not_in_tu`（跨文件用第 5 步）。
   * 1–2 层就能判的 DEFINITE / POSSIBLE 默认不报（交现有规则）；想全看加 `-analyzer-config rb.enhance.ipa.OobSummary:InlineDepth=0`。
   * 要用 `explain` 看链上各函数的摘要：`Dump=stats+summary`。
3. **看结果**：`decisions.py <WorkDir>`（计数）、`oobsum_run.py stats --workdir <WorkDir>`（标签 × 层数、原因、热点）、
   `oobsum_run.py explain --workdir <WorkDir> <函数名或指纹>`（单条判定的来龙去脉）。
4. **选项对照**：同一子目录、每种取值一个 WorkDir（§4.1）。
5. **跨文件（可选）**：`oobsum_run.py bxen --workdir … --bxen … --cc-json <目录> --analysis-folder … --extra "…" --run`（下面的命令块）；`status` 看到哪一步。
6. **发回**（§7）：`git log --oneline -1`、子目录文件数与耗时、每种取值的 `stats` 计数、有没有崩溃（退出码、栈里 OobSummary 的几行）、
   误报 / 漏报用自己的话或不含业务代码的最小例子描述。不带业务代码、函数名、告警原文。

### 4.1 选项对照（D25：取舍都做成了选项，按实测选）

同一批目录、**每种取值一个 WorkDir**，各跑一遍，比 `decisions.py` 的计数表。`-extra_args` 里在上面那条命令的基础上加一个 `-analyzer-config`：

| 跑哪一遍 | 加的选项 | 看什么 |
|---|---|---|
| 基线（默认） | （不加） | 计数表作对照 |
| char 写别名 | `rb.enhance.ipa.OobSummary:CharAlias=true`、`…:CharAlias=verify` | SUSPECT 条数；`verify` 下 `alias_unverified` 的条数，抽几条看是否真有别名（SPEC-0103 / 0106） |
| 内层界 | `rb.enhance.ipa.OobSummary:Subobject=label`、`…:Subobject=off` | `subobject_overflow` 条数；`label` 下内层的 DEFINITE / POSSIBLE（SPEC-0104 / 0105） |
| 不回绕假设 | `rb.enhance.ipa.OobSummary:AssumeNoWrap=true` | SUSPECT 少了多少；抽几条打开后不再报的，看检查是否确实不会回绕（SPEC-0108） |

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work-base                       # 计数表
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work-base --reason subobject_overflow   # 只看某个原因（默认只列报了的）
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work-base --label definite --jsonl d.jsonl
```

带回来的只是计数与结论；要说明某类误报 / 漏报，**用自己的话描述写法、或写一个不含业务代码的最小例子**，不要贴业务代码与告警原文。

### 4.2 用 clang_check 跑全量（cdb 每个文件一个 bxen 进程、线程池并行，SPEC-0118）

和其他单文件规则一样交给 clang_check。本规则只要求：`-checker=rb.enhance.ipa.OobSummary`，`-extra_args` 里带下面这些（clang_check 平时加的
`-rbchecker-config`、`-funcptr-config` 照旧）；**所有进程用同一个 WorkDir（绝对路径），每一轮用一个新的空目录**（旧文件会混进 solve 和统计）。

**A. 默认模式（单 TU，先做这个）——clang_check 跑一遍**

```text
-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work/full-1383 -analyzer-config rb.enhance.ipa.OobSummary:Dump=stats
```

告警由 clang_check 照常收集；被调者在别的文件里的记 `SUSPECT:not_in_tu`。跑完看
`python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py stats --workdir /usr1/oobsum-work/full-1383`（带回时加 `--counts-only`，不列函数名 / 文件名）：第一行"目标："的 TU 数是成功分析的 TU 数，
比 cdb 的文件数少多少就是失败 / 超时多少。

**B. 跨文件（export → solve → report）——clang_check 跑两遍，中间一条 solve**

1. clang_check 第 1 遍，`-extra_args` 用 A 的那串再加 `-analyzer-config rb.enhance.ipa.OobSummary:Phase=export`（WorkDir 换成 `…/xtu-1383`）：
   每个 TU 写 `fir/<tu>.json`，不报告警、不跑引擎（比 A 快）。
2. solve（单进程，读全部 FIR，写 `solved/`）：
   ```bash
   python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py solve --workdir /usr1/oobsum-work/xtu-1383 --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --extra "-rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
   # 先看打印的命令；再加 --run 执行（建议在命令前加 /usr/bin/time -v，记下耗时和峰值内存）
   ```
3. clang_check 第 2 遍，同一个全量 cdb，`Phase=export` 换成 `Phase=report`：每个 TU 读自己的 `solved/<tu>.json` 报告警（只解析源码、不跑引擎），
   告警由 clang_check 照常收集。

`oobsum_run.py status --workdir …` 看 `fir/`、`solved/`、`decisions/` 各有多少。A 跑过的 WorkDir 里已经有 `fir/`（默认模式设了 WorkDir 就会写），
可以跳过第 1 步直接 solve——但第 3 步会覆盖 `decisions/`，要留 A 的结果就先复制一份。solve 在 1 万个 TU 上还没实测：
**先拿一个子系统的 cdb（过滤出一部分文件）试**，再全量。

**注意**

* 磁盘：WorkDir 大约是源码大小的 10 倍（`fir/` 缩进写占大头；本地 67 个用例 146 KB 源码 → `fir/` 0.9 MB、`decisions/` 0.4 MB）。全量前在子系统上
  `du -sh` 估一下。全量不要加 `Dump=summary`（`dump/` 还要再大 2 倍）；要看某个函数时只对那个文件单独跑，加 `Dump=summary+apply` 和 `DumpFilter=<函数名>`。
* cdb 里同一个文件出现多次（不同宏各编一次）：TU id 只按路径算，判定日志 / FIR 只留最后写的那份（告警不受影响）。数一下有多少（条目数、文件数、重复的文件数）：
  ```bash
  python3 -c "import json,os,collections; d=json.load(open('<cdb 目录>/compile_commands.json')); c=collections.Counter(os.path.normpath(os.path.join(e.get('directory',''),e['file'])) for e in d); print(len(d), len(c), sum(1 for v in c.values() if v>1))"
  ```
* 进程被超时杀掉：只会留下 `*.tmp`，不会有写了一半的 `.json`（读的一方看不到 `.tmp`）。

## 5. 更新与撤掉

```bash
cd /usr1/oobsum-llvm && git pull                                   # 拿到最新提交
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16

# 整体撤掉（删除文件、去掉三处登记）
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --remove --apply
```

## 6. 第一次去公司顺便做的两件事

1. `bash oobsum/devenv/fork-facts.sh > fork-facts.txt`：公司 clang 的版本、llvm-project-14 是否上游 14.0.0、构建开关、
   xir 内嵌 CSA 与上游的差异计数（只读，不输出业务代码；贴回之前自己过一眼）。
2. （**可选，2026-10-26 起不再要求**：它要确认的事大部分已由 TestCase、fork-facts、§3.2 确认，见 SPEC-0117；
   想要全量的规模数据再跑它的 R3，或者直接用 §4.0 的正式规则全量跑 + `stats`）I0 探针（`oobsum/probes/i0-integration/README.md`）：
   `python3 oobsum/devenv/land.py --drop oobsum/probes/i0-integration --into bxplugin --root /usr1/BXPlugin --apply`，编译后跑 R1–R3，
   用完 `--remove --apply` 撤掉。R2 的 `[OSP] tu … ptr= long= endian=` 决定摘要里的字节布局能不能信（上游 clang 14 不支持 aarch64 ILP32）。

## 7. 带回来什么

不敏感的结论即可：编译报错原文（我们自己代码的）、告警条数与标签分布（`decisions.py` 的计数表）、耗时、`fork-facts.txt`、探针输出。
在 WSL 里把它们交给 Claude Code（贴进对话，或写进 `oobsum/STATUS.md` 的"公司验证结果"一节再提交），它据此修改。
