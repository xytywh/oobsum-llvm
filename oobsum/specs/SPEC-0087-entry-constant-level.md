# SPEC-0087 入口函数上深度 0 的常数 Req 按 F-10 / F-11 / F-12 判级（不再一律记"入口未证明"）

状态：已实现
来源：错误 SAFE 排查时的探针：

```c
void T9(void) { UINT8 a[4]; UINT32 i = 100; a[i] = 0; }                         /* SUSPECT entry_unproven "up to byte 101" */
void T12(void) { UINT8 a[4]; UINT32 i; for (i = 0; i < 10; i++) { a[i] = 0; } }  /* SUSPECT entry_unproven */
```

入口函数（没有调用方）上推不下去的 Req 一律记账（A-80、SPEC-0019 / 0021）。可这两条的 `have`、末端都是常数，F-11（末端下界 > 大小：对所有取值都越界）、
F-12（代码给出的界下可能越界）就能判；"入口未证明"的原因也不对——它们不依赖入口的任何值。非入口函数里同样的访问会上传、在调用方的调用点判出 L1 / L2，
只有入口函数例外。

## 2. 设计

* Solver 在入口函数上，对推出的每条 Req：**深度 0**、正向（不是 `back`）、`have` / 末端上下界都是常数、守卫都是常数且成立 →
  * 末端下界 > `have` → L1；
  * 否则末端上界 > `have` 且上界来源是代码给出的界（F-12）→ L2；
  * 其余照旧记账（`type_range_only` 等）。
* 判出的生成与调用点相同的 `Rep`（`site` = 访问点，`chain` = 访问路径，指纹同 F-34），不再进入口账本。
* 报出与否照 F-20 / Policy：深度 0 ≤ `InlineDepth` 的 DEFINITE / POSSIBLE 交现有规则、只写判定日志（同非入口函数在调用点判出的那些）；`InlineDepth=0` 时报出。
* 文本：深度 0 不写 "through a 0-level call chain"：`Definite out-of-bound write: up to byte 101 of 'a' (4 bytes) is accessed`。

## 3. 对召回与误报的影响

**召回**：判定日志里这类访问的标签从 SUSPECT 变成 DEFINITE / POSSIBLE（更准）。**报出**：默认 `InlineDepth` 下这类深度 0 的访问不再由本规则报出（F-20：
交现有规则——CSA 的数组越界检查本来就覆盖函数内的常数越界），与非入口函数一致；只要不想分工，`InlineDepth=0` 就照报。
带接口符号的（入口形参、全局、字段）照旧记账，不受影响。

## 4. 实现位置

`core/Solver.cpp`（入口分支：`EntryLevel`）、`core/Alert.cpp`（深度 0 的文本）。

## 5. 测试

* lit `entry-constant.test`：`T9` → DEFINITE（`InlineDepth=0` 报出；默认只写日志，`not_emitted_because: inline_depth`）；`T12` → POSSIBLE；
  对照：入口形参当下标 → 照旧 SUSPECT `type_range_only`。
* `securec-string.test` 有意更新：`snprintf_s(buf, 64, …)` 写 8 字节的 `buf`（入口函数）从 SUSPECT `entry_unproven` 变成 POSSIBLE（界来自库函数的大小实参）。
* 单测 `Propagate.EntryConstantLevel`（L1、L2、含形参照旧记账、只有类型范围照旧记账）。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退（金标准的入口 Req 都带接口符号）；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=decisions`、判定日志。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-80 / SPEC-0021 §2.2 | 入口函数上推不下去的 Req → SUSPECT | 深度 0 的常数 Req 先按 F-10 / F-11 / F-12 判，判出的是 L1 / L2，其余记账 |

## 8. 公司侧验证

可选：确认现有规则（CSA 的数组越界检查）在 bxen 下确实报函数内的常数越界（F-20 分工的前提）。

## 9. 待定 / 以后

* 入口函数上带守卫的符号 Req 按 F-12b（守卫框住的界）判。
