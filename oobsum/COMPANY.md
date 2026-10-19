# 公司侧：从 GitHub 拿代码、同步进 BXPlugin、编译、用 bxen 验证

> 本仓库是 OobSummary 的唯一源头。公司电脑只做三件事：拉代码、同步进 BXPlugin 编译、用 bxen 跑。
> **不要在 xir 里直接改 OobSummary 的代码**——要改就在 WSL 里改、推上来，再同步过去；否则下次同步会把它覆盖。
> 公司电脑能否访问个人 GitHub 私有仓库、能否把结果带出来，以公司规定为准；下面的命令只是技术上怎么做。

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
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c \
  -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/oobsum-tc -analyzer-config rb.enhance.ipa.OobSummary:Dump=options+stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /tmp/oobsum-tc
```

预期（v4.0 骨架）：3 条告警 `[OOB-DEFINITE]`、`[OOB-POSSIBLE]`、`[OOB-SUSPECT:entry_unproven]`；`decisions.py` 显示 5 条判定，
其中 2 条没报（`safe`、`inline_depth`）。引擎接上之后（SPEC-0021，默认 `Phase=inline`）：多一条 `Tc_Engine_TP` 处的
`[OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain`，共 4 条告警、7 条判定（`Dump=stats`：`decisions=7 emitted=4`）。每个版本的预期写在 `TC_OobSummaryChecker.c` 的文件头与对应 SPEC 的"公司侧验证"一节。

### 3.1 用 bxen 跑 67 个分级用例，和本地的 `run.py csa` 对照（SPEC-0111）

```bash
cd /usr1/oobsum-llvm
python3 oobsum/cases/run.py cdb --out /usr1/oobsum-cases-cdb --cc gcc        # --cc 换成公司编译用例要用的编译器
mkdir -p /usr1/oobsum-cases-wd
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
  -cc_json=/usr1/oobsum-cases-cdb/ -analysis_folder=/usr1/oobsum-llvm/oobsum/cases \
  -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-cases-wd -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 oobsum/cases/run.py csa --from-workdir /usr1/oobsum-cases-wd       # 不调 clang，读 bxen 写的 WorkDir 对照
```

预期（与本地同一版本）：`合计 134 个入口：通过 106，通过+S 20，级别不同 2，超出预期 4，需跨TU 2`、`回退 0 条`（单 TU 跑，跨文件的 LV1-07、LV6-01 显示"需跨TU"）。
`Dump=summary` 必须带（对照要用调用图）。不一致就把这张表贴回来；按子目录分开跑、每个子目录一个 WorkDir 也行，`--from-workdir` 给它们的父目录。

* rb.enhance.ipa 规则：`-extra_args` 里**不加** `-analyzer-config ipa=none`（它不走 CSA 的 inline，只挂 TU 级回调）。
* `-analyzer-config` 的列表值用 `+` 连接（`Dump=options+stats`），逗号会被当成分隔符。

## 4. 跑业务代码（引擎接上之后）

```bash
mkdir -p /usr1/oobsum-work
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
  -cc_json=<业务工程>/compile_commands.json -analysis_folder=<目录1>,<目录2> \
  -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work                 # 按标签 / 原因计数
python3 /usr1/oobsum-llvm/oobsum/tools/decisions.py /usr1/oobsum-work --emitted --jsonl review.jsonl   # 给大模型复核
```

上面是默认的 `Phase=inline`（单个 TU 内推到底，跨文件的被调者记 `not_in_tu`）。**跨 TU**（06- §7 的 export → solve → report，SPEC-0028…0031）用驱动生成三段命令：

```bash
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py bxen --workdir /usr1/oobsum-work \
  --bxen /usr1/BXPlugin/cmake-build-release-release/bxen --cc-json <业务工程目录> --analysis-folder <目录1>,<目录2> \
  --extra "-analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
# 打印 export、solve 两段（solve 用生成的桩 TU：/usr1/oobsum-work/solve_stub/）；跑完这两段后再执行同一条命令，打印 report 那段
#（report 的 cdb 从原 cdb 原样复制有判定的 TU：/usr1/oobsum-work/report_cdb/）。加 --run 直接执行；status 子命令看到哪一步了
python3 /usr1/oobsum-llvm/oobsum/tools/oobsum_run.py status --workdir /usr1/oobsum-work
```

**待确认**：bxen 跑多个文件的写法（`-cc_json` 给目录还是文件、`-analysis_folder` 与过滤后的 cdb 怎么配合）——模板是按本节上面的命令猜的，
不对就告诉我正确的写法改 `oobsum_run.py` 的 `bxen_cmd`。
业务的分配器、拷贝封装、纯函数（07- 输入 I5 / I8 / I9）写进模型文件：复制 `oobsum/config/oob_models.example.yaml` 改，`-extra_args` 里加
`-analyzer-config rb.enhance.ipa.OobSummary:ModelFile=<路径>`；`Dump=models` 看实际生效的表（SPEC-0033）。模型文件留在公司，不带回。
定位问题时加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+apply -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=<函数名正则>`。

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
2. I0 探针（`oobsum/probes/i0-integration/README.md`）：
   `python3 oobsum/devenv/land.py --drop oobsum/probes/i0-integration --into bxplugin --root /usr1/BXPlugin --apply`，编译后跑 R1–R3，
   用完 `--remove --apply` 撤掉。R2 的 `[OSP] tu … ptr= long= endian=` 决定摘要里的字节布局能不能信（上游 clang 14 不支持 aarch64 ILP32）。

## 7. 带回来什么

不敏感的结论即可：编译报错原文（我们自己代码的）、告警条数与标签分布（`decisions.py` 的计数表）、耗时、`fork-facts.txt`、探针输出。
在 WSL 里把它们交给 Claude Code（贴进对话，或写进 `oobsum/STATUS.md` 的"公司验证结果"一节再提交），它据此修改。
