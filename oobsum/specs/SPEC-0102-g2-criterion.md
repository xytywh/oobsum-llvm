# SPEC-0102 G2 验收口径：TN 入口零 DEFINITE / POSSIBLE，SUSPECT 单列（用户 2026-10-17 确认）

状态：已实现
来源：SPEC-0002 §7 的提议，用户 2026-10-17 确认（HANDOFF D21）；原则不变——宁可多报，证明不了安全就报 SUSPECT（D16）。

## 2. 设计

* 07- §6 / 06- T-09 的 G2"期望为 `-` / `记账` 的入口**零告警**"改为：
  * **TN 入口**（无论 `pa` 是 `-` 还是 `记账`）：**零 DEFINITE / POSSIBLE**；报出的 SUSPECT 不算失败，条数单列；
  * 期望 `记账` 的 **TP 入口**：至少报出 SUSPECT（报出 DEFINITE / POSSIBLE 是"超出预期"，不算失败）；
  * 期望 `-` 的 TP 入口（若有）同上。
* `run.py csa` 汇总后加一行 G2：TN 入口个数、其中报出 DEFINITE / POSSIBLE 的个数（须为 0）、带 SUSPECT 的个数与 SUSPECT 条数。

## 3. 对召回与误报的影响

无（只改验收口径与统计）。

## 4. 实现位置

`oobsum/cases/run.py`、`oobsum/06-spec.md` T-09、`oobsum/07-dev-plan.md` G2、`oobsum/HANDOFF.md` §3。

## 5. 测试

`run.py csa` 的输出多一行 G2；lit `cases-csa.test` 照旧（合成用例的统计）。

## 6. 调试

无。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| T-09 G2 | 期望为 `-` / `记账` 的入口零告警 | TN 入口零 DEFINITE / POSSIBLE，SUSPECT 条数单列；`记账` 的 TP 至少报出 SUSPECT |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
