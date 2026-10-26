# OobSummary 的 lit 用例

这里的每个用例都对应一个设计（文件第一行写 `oobsum/specs/SPEC-NNNN`），用 LLVM 的 lit 运行、FileCheck 比对输出。

## 三种文件

| 形式 | 是什么 | 什么时候用 |
|---|---|---|
| `xxx.c` | **源码和检查写在同一个文件里**。顶部的 `// RUN:` 是 lit 要执行的命令（通常 `%clang_analyze_cc1 … %s`，`%s` 就是这个文件本身）；下面的 `// CHECK…:` 或 `// expected-warning{{…}}` 是期望 | 一个 C 文件就能说清楚的场景 |
| `xxx.test` | **只写命令和期望**，被测的 C 源码放在 `Inputs/<子目录>/` 里，用 `%S/Inputs/…` 引用（`%S` 是用例所在目录） | 要多个源文件（跨 TU）；同一份输入用不同选项跑几遍对照（如 `CharAlias=false / true / verify`）；跑 Python 工具（`run.py`、`oobsum_run.py`、`gen_chain.py`）；检查 WorkDir 里落盘的文件 |
| `Inputs/` | **只是输入，不单独当用例跑**（clang 的 lit 配置排除名为 `Inputs` 的目录）。每个子目录给一个或几个 `.test` 用；`Inputs/cases/` 是 `cases-csa.test` 用的迷你分级用例 | —— |

`.test` 里以 `#` 开头的是注释，`# RUN:` 是命令；`.c` 里是 `// RUN:`。

## 怎么读一个用例

* `RUN` 行里的 `%clang_analyze_cc1` 是 `clang -cc1 -analyze`；`-analyzer-config rb.enhance.ipa.OobSummary:<选项>=<值>` 是本规则的选项；
  `%t` 是 lit 给每个用例的临时目录（WorkDir 一般放在那里）。
* `| FileCheck %s --check-prefix=XXX`：把命令输出与本文件里 `XXX:` 开头的行比对。常见写法：
  * `XXX:` 按顺序出现；`XXX-NEXT:` 紧接上一行；`XXX-DAG:` 不要求顺序；`XXX-NOT:` 不能出现；`{{正则}}` 是正则；
  * 同一个文件可以有好几个前缀（`ON` / `OFF`、`FIRST` / `BASE`…），分别对应不同的 `RUN`。
* `Dump=…` 的输出以 `[oobsum:<种类>]` 开头（`state`、`summary`、`apply`、`fir`…），用例经常拿它检查中间数据，不只是最终告警。
* WorkDir 里的 `fir/`、`solved/` 是缩进写的 JSON（人能读）；用例要按一行匹配时先过 `oobsum/tools/json_compact.py` 压成一行。

## 怎么跑

```bash
cmake-build-release/bin/llvm-lit -sv cmake-build-release/tools/clang/test/Analysis/oobsum                 # 全部
cmake-build-release/bin/llvm-lit -v cmake-build-release/tools/clang/test/Analysis/oobsum/subobject-arg.test   # 单个，失败时打印命令与差在哪一行
```

`core-unittests.test` 跑的是 `oobsum/unittests` 的单元测试；`cases-csa.test` 跑分级用例的回归尺子（迷你版）。

## 新增用例的约定

* 一个 C 文件能说清楚就写 `.c`；否则写 `.test`，输入放 `Inputs/<短名>/`（子目录名与用例相关，不同用例不共用会改动的输入）。
* 第一行注明 SPEC 编号与要守住的行为；`.c` / `Inputs/*.c` 的头部注释说明每个函数是 TP 还是 TN。
* 告警文字用英文；注释用中文。
