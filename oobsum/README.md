# oobsum — CSA Summary 2.0：用函数摘要检测跨多层调用的内存越界

`rb.enhance.ipa.OobSummary` 的全部设计、规格、用例、脚本与历史。代码在 llvm-project 树内
（`clang/lib/StaticAnalyzer/Checkers/OobSummaryChecker.cpp`、`OobSummary/`，lit 用例 `clang/test/Analysis/oobsum/`）。

## 从哪读起

| 你是 | 先读 |
|---|---|
| 第一次接手（人或 Claude Code） | `../CLAUDE.md`（常驻规则）→ `HANDOFF.md`（背景、决策、现状）→ `STATUS.md`（下一步） |
| 要看"为什么这样设计" | `06-spec.md`（主规格）、`specs/`（增量规格）、`history/README.md`（历次对话的问与答）、`git log -- oobsum/` |
| 要在公司验证 | `COMPANY.md` |
| 要搭本地环境 | `09-local-dev-wsl.md`、`devenv/README.md` |

## 目录

| 路径 | 内容 |
|---|---|
| `00-implementation-plan.md` … `09-local-dev-wsl.md` | 实施方案、架构、工作量、实测清单、场景分级、摘要设计与用例、**规格**、**实施计划**、方案与摘要实样、本地环境 |
| `specs/` | 增量规格 SPEC-NNNN（v4.0 起每次设计 / 修改一份） |
| `cases/` | 67 个分级用例（Lv1–Lv10）；`cases/summaries/`：104 份摘要金标准、`replay.py`、`alerts.json` |
| `probes/` | 公司侧集成探针（I0）与早期的 T0 探针 |
| `devenv/` | 构建、冒烟、运行、落地（`land.py`）、公司事实收集（`fork-facts.sh`） |
| `landing/`、`testcase/` | 同步到公司 BXPlugin 的清单；公司 TestCase 目录 |
| `tools/` | `decisions.py`（判定日志汇总）；以后的驱动 `oobsum.py` |
| `history/` | 历史纪要 |
| `web/` | 各文档的离线网页版 |
| `research/` | 前期 Infer 调研（报告、实验、能力边界；只读） |
| `CHANGELOG.md` | v1 起的版本记录（v4.0 起记录分支上的版本节点） |

## 常用命令

```bash
oobsum/devenv/30-build-llvm.sh                                        # 第一次全量构建（产物在 cmake-build-release/）
ninja -C cmake-build-release clang                                    # 增量
cmake-build-release/bin/llvm-lit -sv cmake-build-release/tools/clang/test/Analysis/oobsum
oobsum/devenv/40-smoke.sh                                             # 冒烟（7 项）
oobsum/devenv/cc1-run.sh -checker=rb.enhance.ipa.OobSummary <文件.c> \
    -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/w -analyzer-config rb.enhance.ipa.OobSummary:Dump=stats
python3 oobsum/tools/decisions.py /tmp/w                              # 判定日志汇总
python3 oobsum/cases/summaries/replay.py                              # 摘要金标准回放
```

路径对照：v3.x 调研包里的 `08-summary2-design/` 就是这里的 `oobsum/`；调研包其余部分在 `oobsum/research/`。
