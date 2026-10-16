# SPEC-0037 `MaxBlocks`：块数超限的函数不分析、报一条 SUSPECT；没有 CFG 的函数同样处理

状态：已实现（S5b 的一半）
来源：`STATUS.md` S5b；06- I-18（`MaxBlocks` 默认 5000）、A-09（超限只导出签名与 `flags: ["cfg_too_large"]`，求解时 `skipped:cfg_too_large`）、
D-27（账本原因 `cfg_too_large`）、D-28（状态）；SPEC-0001（证明不了就是 SUSPECT，不悄悄跳过）。

## 1. 要解决什么

* 业务代码里有几千个块的大函数（状态机、协议解析）：不限的话 lower 与不动点都可能很慢，FIR 也大（X-07）。
* lower 遇到 `getCFG` 失败的函数会打 `no_cfg` 标记、放一个空块，但 core 没人看这个标记：函数被当成空函数，**悄悄没有判定**。

## 2. 设计

* 选项 `MaxBlocks`（整数，默认 5000，0 = 不限；负数是选项错误）。`Dump=options` 带上它。
* lower（`LowerBody`）：CFG 的块数（`getNumBlockIDs`）> `MaxBlocks` → 不降级函数体，`flags: ["cfg_too_large"]`、只放一个空块（同 `no_cfg`）。
  签名、形参、局部照常（调用方的调用点记录不受影响）。
* core（`Interp`）：带 `cfg_too_large` / `no_cfg` 标记的函数 → `status = skipped:cfg_too_large` / `skipped:no_cfg`，不产出 Req、效果。
* 引擎：`partial:*` 与 `skipped:*` 的函数都在函数声明处报一条 SUSPECT，原因 `unanalyzed:<后缀>`（文本 "the function was not fully analyzed (cfg_too_large)"）。
  这个函数自己的访问都没查，所以这一条覆盖它的全部访问；调用方把指针传给它不另记（与 `partial:iter_limit` 相同）。

## 3. 对召回与误报的影响

超限 / 没有 CFG 的函数：原来没有判定（`no_cfg`）或者要跑很久，现在每个函数一条 SUSPECT（带原因），复核时知道这里没查。
默认 5000 远大于用例与上游测试里的函数（最大几十个块），不影响现有结果。

## 4. 实现位置

`Checkers.td`（`MaxBlocks`）、`OobSummaryChecker.cpp`（选项、传给 lower）、`lower/Lowering.{h,cpp}`、`core/Interp.cpp`、`core/Engine.cpp`。

## 5. 测试

* 单测 `Engine.SkippedFunctionsAreSuspect`：函数带 `cfg_too_large` / `no_cfg` 标记 → 状态 `skipped:…`、没有 Req、一条 `unanalyzed:…`。
* lit `max-blocks.test`：一个 12 个 case 的 `switch` 里越界写——默认报 DEFINITE；`MaxBlocks=5` → 函数声明处 SUSPECT `unanalyzed:cfg_too_large`、`Dump=fir` 有
  `cfg_too_large`、没有 DEFINITE；`MaxBlocks=-1` → 选项错误。

## 6. 调试

`Dump=fir`（函数的 `flags`）、`Dump=summary`（`status`）、`Dump=options`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-28 | `skipped:cfg_too_large|timeout|no_body` | 加 `skipped:no_cfg`（Clang 建不出 CFG）；`partial:*` / `skipped:*` 的函数在声明处报 SUSPECT `unanalyzed:<后缀>` |

## 8. 公司侧验证

**等用户提供（可选）**：业务代码里函数块数的分布（P99、最大值；`Dump=fir` 或 I0 探针），据此定默认值（I-18 原文"探针给出 P99 后再定"）。

## 9. 待定 / 以后

* 超时（`skipped:timeout`）；不支持的构造计入判定日志（`unsupported_construct`，S5b 的另一半）。
