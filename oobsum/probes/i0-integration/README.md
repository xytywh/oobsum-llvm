# I0 集成探针：写正式代码之前，在 fork 里跑一次（约 1 小时，其中全量那一遍挂机）

> 调研包 v3.12 · 2026-10-13 · 对应 `07-dev-plan.md` 的输入 **I2 / I2′ / I4** 与 `06-spec.md` §12 的待定项。

一个只读的 CSA checker（`rb.enhance.core.OobSummaryProbe`），不报任何缺陷，只打印 `[OSP]` 开头的行、
按需每个 TU 写一份 JSON。跑三次就能回答正式 checker 依赖的宿主能力和全量代码的形态，
**省掉一长串问答**：

| # | 它确认什么 | 看哪 | 影响 spec 的哪里 |
|---|---|---|---|
| 1 | fork 的 Clang 版本串、C++ 标准、RTTI / 异常开关 | `[OSP] build` | N-07 编码约束（C++14、无 RTTI、无异常） |
| 2 | `-extra_args` 里的 `-analyzer-config <checker>:Opt=Val` 能透传到 checker | `[OSP] opt` | §4.2 选项（I-10…I-23） |
| 3 | 分析业务代码时的目标 ABI：指针 / long / size_t 字节数、大小端 | `[OSP] tu … ptr= long= sizet= endian=` | §5.4 字节布局（D-39，`03-` T0-1c） |
| 4 | `check::ASTDecl<TranslationUnitDecl>` 在 bxen 下被调用；**ExprEngine 不跑** | `[OSP] tu` 有输出；`-analyzer-stats` 里**没有** `Path exploration time` | D11、X-02 宿主回调（沙箱实测：多挂 `EndOfTranslationUnit` 就会让每个函数跑一遍 ExprEngine） |
| 5 | `clang::CallGraph` + `scc_iterator` 给出"被调者先"的顺序；递归能认出来 | `[OSP] scc #k [cycle]` | A-80、A-81 调度 |
| 6 | `BasicBugReport` + `addNote` 在 bxen 里的呈现 | 每个用例 TU 一条 `OobSummary probe:` 告警 + 一条 note | F-30…F-35 告警格式与 trace（D12） |
| 7 | 子目录里的源文件按 `OobSummary/xxx.cpp` 追加进 `add_library()` 能编译链接 | 编译通过即可 | §9.1 代码布局（I-01） |
| 8 | 多进程同时往 `WorkDir` 写文件 | `[OSP] wrote` / `[OSP] write-failed` | §4.5 工作目录 |
| 9 | **全量形态数据**：CFG 规模分布、函数指针调用占比、跨 TU 被调者是否唯一定义、**多轮扫描要几轮、总共重复分析多少 TU**、解析 + 建 CFG 的总耗时 | `probe_stats.py` 的输出 | X-07 分批、N-02 / N-03 性能目标、D-07 重名、`MaxBlocks`（I-18） |

## 1. 落地（五处，与 `csa-checker-dev` 的约定一致；用完整体删除即可）

| 本目录文件 | 放到 |
|---|---|
| `OobSummaryProbeChecker.cpp` | `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/OobSummaryProbeChecker.cpp` |
| `OobSummary/ProbeSubdirUnit.{h,cpp}` | `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/OobSummary/`（**故意放子目录**：验证"入口平铺 + 引擎在子目录"的布局，`06-spec.md` §9.1） |
| （两行） | `BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` 的 `add_library()` 末尾追加 `OobSummaryProbeChecker.cpp` 与 `OobSummary/ProbeSubdirUnit.cpp` |
| `checkers_td_snippet.td` | 内容追加到 `Checkers.td` 的 `let ParentPackage = RosaRBEnhanceCore in { … }` 块最后 |
| `TestCase/OobSummaryProbeChecker/`（整个目录） | `BXPlugin/xir/llvm_ast/TestCase/OobSummaryProbeChecker/` |
| （一行） | `BXPlugin/xir/llvm_ast/TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(OobSummaryProbeChecker)` |

编译照常：

```bash
cd /usr1/BXPlugin/cmake-build-release-release
cmake -G "Unix Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_VERBOSE_MAKEFILE=ON -Darch=64 -Dos=linux \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ROOT=/usr1/llvm-project-14/cmake-build-release/ -Dprovider=llvm_ast -Dptype=lib \
  -DCLANG_TABLEGEN_EXE=/usr1/llvm-project-14/cmake-build-release/bin/clang-tblgen ../
make -j16
```

`check_checker_conventions.py`（`csa-checker-dev` 的规范检查）在模拟目录上的结果：PASS 42 / WARN 2 / FAIL 0；
两条 WARN 是"用例没有 TP/TN/FP/FN 四象限"——探针不检测缺陷，没有象限可言。

**编译不过时**最可能的两处：`llvm/Support/xxhash.h` / `llvm/Support/FormatVariadic.h` 路径（以 fork 为准），
或 fork 的 `CheckerRegistry` 不认 `CheckerOptions`。把报错原样贴给我。

## 2. 跑三次

**R1：测试用例**（两个 TU：跨 TU 调用、函数指针、循环、递归、头文件 static inline）

```bash
mkdir -p /tmp/osp-tc
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_folder=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryProbeChecker/src/ \
  -checker=rb.enhance.core.OobSummaryProbe \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-stats -analyzer-config rb.enhance.core.OobSummaryProbe:EmitProbeReport=true -analyzer-config rb.enhance.core.OobSummaryProbe:WorkDir=/tmp/osp-tc -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" \
  > r1.txt 2>&1
grep -c '^\[OSP\]' r1.txt; grep 'Path exploration time' r1.txt || echo "no ExprEngine (expected)"
```

期望（上游 LLVM 14.0.6 在 x86-64 上的输出见 `expected-upstream14.txt`；`id=` 那串哈希随路径变，不用比）：
2 条 `OobSummary probe:` 告警、各带 1 条 note；`scc` 行的顺序 `Probe_Min → Probe_Leaf → Probe_Mid → Probe_Top`；
`_b.c` 里 `Probe_Depth` 标 `cycle`；**没有** `Path exploration time`（有的话说明 bxen 另外开了路径敏感 checker，把 `-checker=` 之外默认开启的 checker 清单告诉我）。

**R2：一个业务源文件**（看目标 ABI）

```bash
mkdir -p /tmp/osp-one
/usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
  -cc_json=<业务工程>/compile_commands.json -analysis_file=<任选一个业务 .c> \
  -checker=rb.enhance.core.OobSummaryProbe \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-config rb.enhance.core.OobSummaryProbe:WorkDir=/tmp/osp-one -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" \
  2>&1 | grep '^\[OSP\] tu'
```

`ptr=4 long=4` → fork 按 ILP32 分析业务代码，字节布局可信；`ptr=8` → 现有规则和摘要通道都按 LP64 布局算，
含指针 / `long` 的结构体字节偏移要打折（数组下标与元素个数的比较不受影响，`03-` T0-1c）。

**R3：全量**（挂机；只建 CFG、不做路径敏感，应明显快于现有规则的全量运行）

```bash
mkdir -p /usr1/osp-full
time /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
  -cc_json=<业务工程>/compile_commands.json \
  -checker=rb.enhance.core.OobSummaryProbe \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-config rb.enhance.core.OobSummaryProbe:WorkDir=/usr1/osp-full -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini" \
  > r3.log 2>&1
grep -c 'write-failed' r3.log
python3 probe_stats.py /usr1/osp-full --wall-seconds <time 的 real 秒数> --md probe-full.md --json probe-full.json
```

## 3. 回我这几样

1. `r1.txt`（全文）；编译若有告警 / 报错也贴上。
2. R2 的那一行 `[OSP] tu …`。
3. R3：`time` 的三行、`probe-full.md`、`write-failed` 的个数；**bxen 全量模式是不是多进程并行、并发度多少、由哪个参数控制**。
4. 若 R1 里出现了 `Path exploration time`：bxen 默认还开了哪些 checker。

`probe_stats.py` 的"多轮扫描"两行估计的是**回退方案 A**（多轮 TU 扫描，`06-` X-08）的代价：TU 要被重复解析的倍数。
默认的方案 B（export → solve → report）遍数固定为 2，与它无关；这两行用来判断"万一 B 在你们环境里不可行，A 的代价有多大"。

## 4. 沙箱复现

`./sandbox_build.sh`：用 Ubuntu 的 `clang-14` / `libclang-14-dev`（上游 14.0.6）把同一份源码编成插件（`-DOOBSUM_PLUGIN_BUILD`），
在测试用例上跑出 `expected-upstream14.txt`。在 67 个分级用例上跑 `probe_stats.py`：71 个 TU、435 个函数、3 轮、TU 分析总次数 70
（`cases/lv1/lv1_07_cross_tu/` 的三文件链需要 3 轮）。【实测数据】
