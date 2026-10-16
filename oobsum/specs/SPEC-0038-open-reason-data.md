# SPEC-0038 调用点记账的原因：上界只来自 `data` 时记 `type_range_only`（原来误记 `pointer_stepping`）；被调者改字段 / 全局的回归用例

状态：已实现
来源：调试中发现（`Buf_SetLen(&st); Buf_Clear(auc, st.ulLen);` 报成 SUSPECT `pointer_stepping`）；06- D-22（`type` / `widen` / `data` 不是代码给出的界）、
D-27（`pointer_stepping` = 指针在循环里步进，A-45）；SPEC-0001（原因要说清楚证明不了的地方）。

## 1. 要解决什么

调用点判定（A-60）里，末端上界来自非代码给出的界时记账，原因按标签取：`type` → `type_range_only`，**其余一律** `pointer_stepping`。
`data`（值来自内存：被调者改过的字段、全局、数组元素）也落到 `pointer_stepping`，复核的人会去找根本不存在的指针步进。

## 2. 设计

`widen` → `pointer_stepping`（widening 去掉的上界，A-45）；`type`、`data` → `type_range_only`（只剩类型范围）。C++ `apply::Classify` 与 `replay.py` 的 `classify` 同步。

另加回归用例守住"被调者改了调用方的字段 / 全局之后，调用方不再用调用前的值"（mod-ref 没做之前的保守做法：传出地址的局部结构体、
被调者可能写的全局，调用后都当成未知），防止以后做 mod-ref（S8c-2）时引入错误的 SAFE。

## 3. 对召回与误报的影响

无（只改原因的写法）。按原因过滤（`SuspectReasons`）时，原来按 `pointer_stepping` 放行的这类判定改按 `type_range_only`。

## 4. 实现位置

`core/Apply.cpp`、`oobsum/cases/summaries/replay.py`。

## 5. 测试

* lit `callee-writes.test`：被调者把字段从 4 改成 100（同 TU、跨 TU 两种）、把全局从 4 改成 100 → 都报 SUSPECT（不是 SAFE），原因 `type_range_only` / `not_in_tu` / `entry_unproven`。
* 金标准：`replay.py` 0 处不一致（16 个用例的调用点记账里没有 `data` 上界）。

## 6. 调试

`Dump=apply`（"上界只来自 data"）。

## 7. 对主规格的修订

无（D-27 的原因含义不变，改的是实现与它不符的地方）。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 能分清来源时用更具体的原因（`field_written_by_callee`、`global_value`）：要在区间上带"来自哪里"，以后。
