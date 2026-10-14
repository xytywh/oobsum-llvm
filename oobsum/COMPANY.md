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
其中 2 条没报（`safe`、`inline_depth`）。每个版本的预期写在 `TC_OobSummaryChecker.c` 的文件头与对应 SPEC 的"公司侧验证"一节。

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

跨 TU 的 export → solve → report 三阶段（06- §7）由驱动 `oobsum/tools/oobsum.py` 负责（WP0 / WP10 实现后补命令）。
定位问题时加 `-analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+apply -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=<函数名正则>`。

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
