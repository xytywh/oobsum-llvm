# SPEC-0032 全局大小表：`extern` 不完整数组在 solve 时取定义所在 TU 的大小

状态：已实现（S7e）
来源：`STATUS.md` S7e；06- D-34（`extern` 全局：求解时取定义所在 TU 的大小；全工程都没有定义的 → 未知）、A-80（solve 建全局大小表）；
金标准 `LV6-01`（`extern UINT32 g_aulPortStat[];` 在使用它的文件里看不到大小，`port_stat_def.c` 定义 16 项）；SPEC-0029（solve）。

## 1. 要解决什么

使用方的 TU 里 `extern` 数组没有大小，访问只能记账 `alloc_size_unknown`；定义在别的 TU 里，solve 时是知道的。

## 2. 设计

* `RunProgram` 先扫所有模块的全局：类型完整（`TypeSize` 成功）的按全局的键记大小（定义优先，否则第一份）。
* 函数内解释时，`&g` 的类型在本模块里不完整 → 查这张表；查到就用（`have` = 定义处的大小），查不到照旧大小未知（记账）。
* inline 时表里只有本 TU 的全局，不完整的照旧查不到——行为不变。
* `Phase=solve` 也打 `Dump=summary`（全局求解后的每个函数的摘要）；`engine_check.py` 对跨 TU 的用例改走 export + solve 取摘要。

## 3. 对召回与误报的影响

`extern` 数组的访问从记账（SUSPECT）变成按真实大小判定（LV6-01 TP：`ulPort = 16` → 68 > 64 → L1）。

## 4. 实现位置

`core/Interp.{h,cpp}`（`AnalyzeFunction` 的 `globalSizes`、`GAddr`）、`core/Engine.cpp`（建表）、`OobSummaryChecker.cpp`（solve 的 `Dump=summary`）、`oobsum/tools/engine_check.py`。

## 5. 测试

* lit `global-size.test`：驱动 `local` 跑 `Inputs/xglobal/`（使用方 + 定义方）→ DEFINITE；只跑使用方的 inline → `alloc_size_unknown`。
* `engine_check.py`：LV6-01、LV1-07 走 solve；LV6-01 视结果加入 `engine-check.test`。

## 6. 调试

`Phase=solve` + `Dump=summary`：使用方函数的 Req `have` 是定义处的大小。

## 7. 对主规格的修订

无（按 D-34 实现）。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 全工程都没有定义的 `extern`（只有声明）：现在是大小未知（记账）；golden 写成符号 `ext(g:名)` 再在链接时代换，以后需要时再做。
