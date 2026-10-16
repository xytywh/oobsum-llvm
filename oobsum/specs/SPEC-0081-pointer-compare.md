# SPEC-0081 同一对象的两个指针比较：按偏移收窄（`while (p < pEnd)`）

状态：已实现
来源：指针循环探针：

```c
static void P1(UINT8 *p, UINT32 n) { UINT8 *e = p + n; while (p < e) { *p = 0; p++; } }
void P1_TP(void) { UINT8 a[16]; P1(a, 17); }   /* 原来：被调者里 SUSPECT pointer_stepping */
void P1_TN(void) { UINT8 a[16]; P1(a, 16); }
```

`AssumeCmp` 对指针比较直接返回（"指针比较本步不产生信息"）：循环头 widening 去掉了 `p` 偏移的上界，条件 `p < e` 收不回来 → 记账。
"拿结束指针比较"的循环在解析 / 拷贝代码里到处都是。

## 2. 设计

* 两边都是指针、**基址相同**（同一个对象：同一个形参所指对象、同一个局部 / 全局 / 堆对象）：比较两个指针就是比较两个偏移。越过对象的指针算术是未定义行为，
  偏移按数学整数看，不考虑回绕。
* 一边的偏移是区间、另一边是确切值 → 按整数的收窄规则收窄区间那一边的偏移（`NarrowRange`，与整数的 `Narrow` 共用），写回指针局部；收成空 → 这条边不可达。
* 两边都是确切值 → 差是常数时直接判真假；否则不产生信息（以后可同整数记守卫原子）。〔由 SPEC-0082 修订：两边确切时按偏移走整数的同一套原子逻辑（`p != p + n` → `n >= 1`）〕
* 基址不同（`pEnd` 是另一个形参，两者关系不知道）、基址不知道 → 照旧不产生信息。

## 3. 对召回与误报的影响

**召回**：`while (p < pEnd)` 的越界判出级别（原来记账）。**误报**：界内的判 SAFE（原来记账）。

## 4. 实现位置

`core/Interp.cpp`（`NarrowRange`、`Narrow`、`NarrowPtr`、`AssumeCmp`）。

## 5. 测试

* lit `pointer-compare.test`：`while (p < e)`（`e = p + n`）→ 17 POSSIBLE、16 SAFE；`Dump=state` 的循环体 `p=*p0+[0, p1 - 1]`；对照：`pEnd` 是另一个形参 → 照旧 `pointer_stepping`。
* 单测 `Interp.PointerCompareNarrows`。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=state`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-14（`assume` 边） | 只看整数比较 | 同一对象的两个指针比较按偏移收窄（区间一边写回指针局部；两边确切、差是常数直接判真假） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `for (q = p; q != p + n; q++)`（`!=` 结束条件）；计数器与指针同步前进（`for (i < n) *p++ = 0;`，需要两者的关系）。
