# SPEC-0105 选项 `Subobject`：内层界怎么报——`suspect`（默认）/ `label` / `off`（用户决定 D24、D25）

状态：已实现
来源：用户 2026-10-17 决定。D24："不管哪个维度，每个越界都告警"；同时"保留一个参数，都越界了才报（防止我记错了业务代码的写法）"。
D25：取舍一律做成选项，默认与本地验证用"每一维都报"。另外上次问的"确定越出一行的（`a[0][5]`）要不要标 DEFINITE"按 D25 也做成取值。

## 1. 取值

| 取值 | 只越出内层（成员数组 / 多维数组的一行）、没越出整个对象 | 越出整个对象 |
|---|---|---|
| `suspect`（默认） | SUSPECT `subobject_overflow`（SPEC-0104 的行为） | 照常 DEFINITE / POSSIBLE / SUSPECT |
| `label` | 内层 Req 按常规判：确定越出 → DEFINITE、按代码给出的界可能越出 → POSSIBLE、证明不了 → SUSPECT `subobject_overflow` | 同上 |
| `off` | 不查（只查整个对象，SPEC-0104 之前的行为） | 同上 |

## 2. 设计

* 选项 `rb.enhance.ipa.OobSummary:Subobject`（字符串，默认 `suspect`；别的值报选项错误），进 `interp::Options::subobject`（`SubMode::Suspect / Label / Off`）。
* `off`：`Eval` 的 `subobj` 不推窗口——函数内不查、实参不记 `sub`、不产生内层 Req。
* `label`：
  * `CheckSubs`：上界证明不了时**一律**记内层 Req（不再区分含不含接口符号；本函数里就定了的常数也走 Req，由入口 / 调用点按常规判）；
    下界（负下标）证明不了、窗口没有界仍记账 `subobject_overflow`。
  * `apply::Classify`：内层 Req 的 L1 / L2 不改判（`open` 的原因仍是 `subobject_overflow`）；3b 的内层变体同样。
  * `solver::Propagate`：入口函数上深度 0 的常数内层 Req 按 F-10 / F-11 / F-12 判（`EntryLevel`，SPEC-0087）。
  * 告警文字（`alert::AlertMessage`、`replay.py` 的 `alert_msg`）：`Definite out-of-bound write: up to byte 6 of the array member or row (4 bytes) of 'a' is accessed`。
* 层数过滤（F-20，`Policy`）：**内层的判定（`dir: "sub"`）不按 `InlineDepth` 过滤**——CSA 现有规则不查子对象，过滤掉就漏了（检出优先）。三个取值都适用。
* 选项经 `engine::RunInline / RunSolve / RunProgram` 的 `interp::Options` 传给 `solver::Propagate(fns, subLabel)` → `apply::ClassifyAll / Classify(…, subLabel)`。
  `replay.py` 用模块变量 `SUBOBJECT`（默认 `suspect`）镜像。

## 3. 对召回与误报的影响

`suspect` / `label` 报出的访问相同，只是标签不同；`off` 回到 SPEC-0104 之前（少报内层越界，只给"业务代码确实这样写"时用）。
层数过滤豁免让 `label` 下深度 ≤ `InlineDepth` 的内层 DEFINITE / POSSIBLE 也报出。

## 4. 实现位置

`Checkers.td`（rb.enhance 块）、`OobSummaryChecker.cpp`；`core/Interp.{h,cpp}`；`core/Engine.cpp`；`core/Solver.{h,cpp}`；`core/Apply.{h,cpp}`；`core/Alert.cpp`；
`core/Policy.cpp`；`replay.py`；06- §4 I-27。

## 5. 测试

* lit `subobject-option.test`：同一组输入（`Inputs/subobj/o.c`、`u.c`、`a.c`）按三个取值各跑一遍——`label`：`a[0][5]` → DEFINITE、`Mark(1, 8)` → DEFINITE（经调用链）、
  `Loop_Tp`（`a[0][i]`，`i < 8`）→ POSSIBLE、`Fill(st.aucBuf, 20)` → POSSIBLE；`off`：一条 `subobject_overflow` 都没有、`Dump=options` 里 `Subobject=off`；
  `suspect` 与 `subobject-overflow.test` 相同（默认）。
* 单测：`Apply.SubObjectReqLabelMode`（`subLabel` 为真时 L1 不改判）、`Policy.SubobjectNotFilteredByDepth`、`Interp.SubObjectOff`。
* `run.py csa`（默认）不变。
* 实测：上游 `clang/test/Analysis/*.c` 在 `label`、`off` 下都是 213 正常 / 0 崩溃 / 0 超时。

## 6. 调试

`Dump=options` 打印 `Subobject=…`；`Dump=apply` 行尾 `[sub]`（3b 的内层变体）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §4 选项表 | …、I-26 `CharAlias` | 加 I-27 `Subobject` |
| F-20 层数过滤 | DEFINITE / POSSIBLE 深度 ≤ InlineDepth 不报 | 内层的判定（`dir: sub`）除外 |

## 8. 公司侧验证

业务子目录上三个取值各跑一遍，比 `subobject_overflow` / 内层 DEFINITE / POSSIBLE 的条数并抽查。

## 9. 待定 / 以后

无。
