# SPEC-0054 返回值范围 `ret_int`：不是常数的返回也推出（取值函数 / 截断函数）

状态：已实现
来源：`run.py csa` 的"级别不同"LV4-05（**年内承诺**，期望 L2）：

```c
static UINT32 Cfg_ClampDrbNum(UINT32 ulCfg) { return (ulCfg > 8) ? 8 : ulCfg; }   /* eff ret ∈ [0, 8] */
static UINT32 Drb_GetMaxNum(UINT32 ulCfg) { return Cfg_ClampDrbNum(ulCfg); }
void Ue_OnReconfig_TP(UINT32 ulCfgDrbNum) { DRB_S astDrb[8]; UINT32 ulNum = Drb_GetMaxNum(ulCfgDrbNum); Ue_ResetDrb(astDrb, ulNum); /* i <= ulNum */ }
```

SPEC-0025 的 `ret_int` 只在每条返回都是常数时随 `ret_cond` 推出；一般的返回值范围不传给调用方 → `ulNum = ?(call)` → 只能记账。STATUS S8c-2 列过"ret_int 单独推出"。
06- D-24（`ret_int`：`range`、`bound`）、A-63。

## 2. 设计

### 2.1 推导（`Effects`，没有 `ret_cond` / `ret_new` 时）

所有返回都是整数（不是指针）：

* 各条返回是**同一个确切式子** `E`（接口符号的线性式，如 `p0 + 1`）→ `ret_int range [E, E]`，`bound` 取 `exact`；
* 否则每条返回在**它那条路的守卫**（`RecordReturn` 记下的）与类型范围下求常数上下界，取并 → `ret_int range [lo, hi]`，`bound` 取各条来源标签里最差的
  （确切的常数 → `const`；被守卫框住 → `check` 等；只剩类型范围 → `type`）；有一条求不出来就不记。

### 2.2 调用点（`CondResult`）

* 常数区间：照旧（`bound` 是代码给出的界时保留它，否则同原来的 `const`）。
* 符号式：被调者形参换成实参（实参是可信的确切值才代）；代完还含被调者形参 → 未知（同原来）。

## 3. 对召回与误报的影响

**召回**：长度 / 个数来自取值函数、截断函数（"兄弟链"）的，按返回值范围判 L2 / SAFE（原来记账）。可靠性：每条返回的界都在它自己的路径条件下成立，取并包含所有返回值。

## 4. 实现位置

`core/Interp.cpp`（`Effects`、`CondResult`）。

## 5. 测试

* lit `ret-int.test`：LV4-05 同形 → `Dump=summary` 的 `Cfg_ClampDrbNum` / `Drb_GetMaxNum` 有 `ret_int [0, 8]`；TP（`i <= ulNum`）L2、TN（`i < ulNum`）SAFE；
  `return x + 1` → 调用方代换成确切值；对照：没有守卫的 `return x` → 只有类型范围（`type`，记账）。
* `run.py csa`：LV4-05、LV5-06（承诺）、LV5-03（尽力）从"级别不同"变成"通过"；不回退。`replay.py`、`engine_check` 不回退。
* `guards.c`、`out-new.test` 里检查摘要 `effs` 的两处按新结果更新（多了 `ret_int`）。

## 6. 调试

`Dump=summary`（`effs`）、`Dump=state`（调用后的值）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 / A-63 | `ret_int`：随 `ret_cond`（返回常数时） | 不是常数的返回也推出：同一个确切式子 → 符号区间；否则各条返回在自己的路径条件下的常数界取并 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 返回值与形参的关系（`ret <= p1`）。
