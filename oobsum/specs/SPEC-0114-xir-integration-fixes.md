# SPEC-0114 公司 xir 集成反馈：头文件搜索路径、`Decision::msg` 改名、WorkDir 里的 JSON 缩进写

状态：已实现
来源：用户 2026-10-26 在公司机器上把代码同步进 BXPlugin（`land.py --apply`）后编译、用 bxen 跑 TestCase（结果与预期一致：4 条告警、`decisions=7 emitted=4`），反馈三个问题。

## 2. 设计

1. **头文件搜索路径**：源码按 `#include "OobSummary/core/…"` 包含子目录里的头文件。上游 clang 的 Checkers 目录本来就在搜索路径里；
   BXPlugin 的 `xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` 只有 `target_include_directories(csaCheckers PUBLIC ${LLVM_INCLUDE_DIRS})`，
   报 `OobSummary/core/Label.h: No such file or directory`。`land.py`（bxplugin 布局、有子目录时）：
   `add_library(<目标> …)` 的目标已有 `target_include_directories(<目标> …)` → 行内补 `${CMAKE_CURRENT_SOURCE_DIR}`（与用户手改的相同）；没有 → 在 `add_library` 之后加一行
   `target_include_directories(<目标> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})`。重复执行不重复加；`--remove` 不删（留着无害）。
2. **`Decision::msg` 改名 `warnMsg`**：在 xir 里与别处的同名符号冲突（用户改成 `warnMsg` 后编译通过）。只改 C++ 成员名；判定日志的 JSON 键仍是 `"msg"`。
   `Alert::msg` 虽然没报错，用户要求同样改成 `warnMsg`（隐患相同）；`apply` 回放里的局部变量 `msg` 改成 `lineText`。代码里不再有叫 `msg` 的标识符。
3. **WorkDir 里的 JSON 缩进写**：`fir/<tu>.json`、`solved/<tu>.json`、`WriteSummaryFile` 写的摘要原来整个写成一行，人不好读；改成 `formatv("{0:1}")` 缩进写
   （与 `Dump=summary` 写的 `dump/<tu>/summary/*.json` 相同）。`decisions/<tu>.jsonl` 仍是一行一条（JSONL：方便按行 grep、追加、流式读）。
   读的一方都是 JSON 解析，不受影响；lit 用例要按一行匹配时先过新工具 `oobsum/tools/json_compact.py`（键排序、无空格、中文不转义，与 LLVM 紧凑写法逐字相同）。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`oobsum/devenv/land.py`（`cmake_include`）；`core/Decision.h`、`Engine.cpp`、`DecisionLog.cpp`、`report/Reporter.cpp`、`OobSummaryChecker.cpp`（改名）；
`core/FirJson.cpp`、`core/Summary.cpp`、`OobSummaryChecker.cpp`（缩进写）；`oobsum/tools/json_compact.py`；lit `phase-export.c`、`phase-solve.test`、`global-identity.test`、
`header-key.test`（先压成一行再匹配）；`clang/test/Analysis/oobsum/README.md`（用例目录说明）。

## 5. 测试

`cmake_include` 的三种情形（已有行补上、已补的不再动、没有行就新加）手工核对；llvm-lit oobsum 104/104；单测 158；`run.py csa` 回退 0；`replay.py`、`engine_check` 不变。

## 6. 调试

无新增。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §4.5 WorkDir 布局 | `fir/<tu>.json` 等 | 缩进写（SPEC-0114）；`decisions/*.jsonl` 一行一条 |

## 8. 公司侧验证

`git pull` → `land.py --apply`（这次会显示 CMakeLists.txt 的 include 行已是最新）→ `make -j16` 不再需要手改；把自己改的 `warnMsg` 撤掉，以同步过去的为准。

## 9. 待定 / 以后

无。
