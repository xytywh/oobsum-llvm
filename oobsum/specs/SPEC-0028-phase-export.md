# SPEC-0028 `Phase=export`：每个 TU 写 `fir/<tu>.json`，不求解、不报

状态：已实现（S9-2）
来源：`STATUS.md` S9；06- §7 X-01（导出全部有函数体的函数、全局、记录类型、目标信息到 `fir/<tu>.json`，紧凑 JSON，先写 `.tmp` 再改名，幂等）、
X-02（不求解、不发告警，只挂 `ASTDecl<TranslationUnitDecl>`）、X-06（inline 模式设置了 `WorkDir` 时额外写一份 `fir/`）；SPEC-0005（FIR 的 JSON）、SPEC-0003（TU id）。

## 1. 要解决什么

跨 TU（方案 B）的第一阶段：每个 TU 把降级结果落盘，供 `solve` 一次读入全部 TU。

## 2. 设计

* `Phase=export`：降级 → `WorkDir/fir/<tu-id>.json`（`fir::WriteModuleFile`：紧凑 JSON、先写 `.tmp` 再改名；重跑覆盖）。不跑引擎、不处理调试内建函数、
  不发告警、不写判定日志。`WorkDir` 为空时什么都不写（提示一行告警到 stderr）。
* `Phase=inline` 且设置了 `WorkDir`：判定照常，另写一份 `fir/<tu-id>.json`（X-06，方便对照）。
* `Dump=stats` 在 export 时打 `[oobsum:stats] export tu=<id> functions=<n> fir=<路径>`。
* `Dump=fir` 等调试输出与 Phase 无关，照常。
* `Phase=report`（S9-4 之前的过渡）：不在本 TU 跑引擎（判定将来自 solve 的结果），调试内建函数照常——`cases-csa.test` 用它只看调试判定。

## 3. 对召回与误报的影响

无（不出判定）。

## 4. 实现位置

`OobSummaryChecker.cpp`：`checkASTDecl` 的 export 分支与 `writeFir`。

## 5. 测试

* lit `phase-export.c`：`Phase=export` 不报（含调试内建函数）、写出 `fir/<id>.json`、内容能读回（`Dump=stats` 的函数数）；inline + `WorkDir` 也写 `fir/`。

## 6. 调试

`WorkDir/fir/*.json` 本身就是落盘的中间数据；`Dump=fir` 的文本形式。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

bxen 全量模式跑 `Phase=export`：看 `WorkDir/fir/` 的文件数与 TU 数一致、总大小（只带回数字，用来估 solve 的内存，06- X-07 / 07- I2）。

## 9. 待定 / 以后

* S9-3 `Phase=solve`、S9-4 `Phase=report`、S9-5 驱动。
