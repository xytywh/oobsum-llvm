# SPEC-0074 写效应 `field` 的值是几个常数之一时记区间

状态：已实现
来源：SPEC-0073 的对照 `Two`：`if (c) pst->ulLen = 32; else pst->ulLen = 8;`——两条路径写的值不同，不记效果，调用方只能记账。按条件选常数长度的初始化函数很常见。

## 2. 设计

* 推导：写了的值都是常数但不全相同 → `{kind: "field", target, range: [最小, 最大]}`（不写 `value`）；`may` 规则不变。有一个不是常数就照旧不记。
* 调用点（`FieldWrites`）：`range` → 区间值（来源标签 `branch`，代码给出的界）；`may` 时照样与调用前的值汇合。
* `range` 字段本来就在 schema 的 `eff` 里（`ret_int` 用），读写不用改。

## 3. 对召回与误报的影响

**召回**：按条件写不同常数的字段，调用方按区间判 L2 / SAFE（原来记账）。区间可靠（包含每条路径写的值）。

## 4. 实现位置

`core/Interp.cpp`（`Effects`、`FieldWrites`）。

## 5. 测试

* lit `field-effect.test`：`Two` → `field p0->ulLen range [8, 32]`，`Two_TP`（16 字节）→ POSSIBLE（末端 32）；加 `Two_TN`（32 字节）→ SAFE。
* 单测 `Interp.FieldEffect`：写 32 / 8 的那一组改成期望 `range`。
* `run.py csa` 不回退。

## 6. 调试

`Dump=summary`（`effs`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 `field`（SPEC-0073） | 写的值都相同才记 | 都是常数时也可以记区间 `range` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 不同的符号式（`p1` / `p2`）。
