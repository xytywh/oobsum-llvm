# SPEC-0027 调用点记被调者的函数键（`callee_key`），引擎内部按键找被调者

状态：已实现（S9-1）
来源：`STATUS.md` S9；06- D-07（函数键：`static` 函数带文件 id，同名不串）、D-25（调用点记录）、§7（跨 TU：所有 TU 的函数放在一起求解）；SPEC-0003（键）。

## 1. 要解决什么

引擎内部（`RunInline` 的摘要表、`Propagate`、`TopoOrder`、效果代入）按**函数名**找被调者。单个 TU 里没问题；跨 TU 求解时，各文件的 `static` 函数大量重名
（`static void Init(void)`），按名字会把 A 文件的调用接到 B 文件的同名函数上。

## 2. 设计

* 调用点记录加可选字段 `callee_key`：被调者的函数键（D-07，`F@<文件 id>:名` / `F:名`）。`callee` 仍是名字（金标准、`replay.py`、告警的调用链都用名字）。
* 引擎的摘要表按函数键索引（`RunInline`、以后的 `solve`）。找被调者：有 `callee_key` 且表里有 → 用它；否则按 `callee`（金标准的摘要没有 `callee_key`，照旧按名字）。
  `Propagate`、`TopoOrder`、入口判断、`not_in_tu`、效果代入都走这一条。
* `Dump=apply` 的行仍写函数名（`DumpFilter` 按名字过滤）。

## 3. 对召回与误报的影响

无（单个 TU 里键与名字一一对应）；为跨 TU 求解做准备。

## 4. 实现位置

`core/Summary.{h,cpp}`（`Call::calleeKey`）、`summary.schema.json`、`core/Apply.cpp`（`CalleeOf`、`TopoOrder`）、`core/Solver.cpp`、`core/Interp.cpp`（`RecordCall`、效果代入）、
`core/Engine.{h,cpp}`（摘要表按键、`NameOf`）、`OobSummaryChecker.cpp`（`Dump=summary` 按键取）、`engine_check.py`（规范化时去掉 `callee_key`）。

## 5. 测试

* 单元测试 `SummaryTest`：`callee_key` 读写；`PropagateTest` 不变（金标准没有 `callee_key`，按名字）。
* `EngineTest`：两个 `static` 同名函数（不同文件 id）各自的调用接到各自的函数上。
* 全部 lit、`engine_check.py`、`run.py csa` 不变。

## 6. 调试

`Dump=summary` 的调用点多一个 `callee_key`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-25 | `callee` | 加可选 `callee_key`（引擎写；有它时按它找被调者） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
