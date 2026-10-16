# SPEC-0052 `ret_new` 允许常数偏移：带管理头的分配器（返回 `pucRaw + 16`）

状态：已实现
来源：`run.py csa` 的"级别不同"LV4-03（**年内承诺**，期望 L1）：

```c
static UINT8 *Buf_Alloc(UINT32 ulLen) { UINT8 *pucRaw = Pub_MemAlloc(ulLen + 16); if (pucRaw == NULL) return NULL; return pucRaw + 16; }
```

用例头：`eff ret = new(ulLen + 16) + 16 ∨ NULL`，即 `ext(ret) = ulLen`。SPEC-0024 的 `ret_new` 推导只认偏移为 0 的返回，`Buf_Alloc` 没有效果 →
调用方拿到未知指针 → 只能记账 `_unknown0`。06- D-24（`ret_new`）、A-63。

## 2. 设计

`Effects()`：所有非空返回都是**同一个堆对象、同一个常数偏移 `k >= 0`** 时记 `ret_new`，`size = S − k`（从返回的指针起还剩的字节）。
偏移为 0 时与原来相同。调用方把返回的指针当成新对象的起点（`back = 0`）：往回写进管理头会按负偏移（SPEC-0039）报出——从用户的角度看就是越界
（会破坏分配器的管理信息）。

## 3. 对召回与误报的影响

**召回**：带头部的分配器（业务里常见：内存池、消息缓冲）返回的缓冲区按真实大小判（原来记账）。

## 4. 实现位置

`core/Interp.cpp`（`Effects`）。

## 5. 测试

* lit `ret-new-offset.test`：LV4-03 同形 → `Dump=summary` 的 `Buf_Alloc` 效果 `ret_new size p0`；TP（申请 32、写到第 33 字节）DEFINITE、TN（申请 33）SAFE；
  对照：两条返回路径偏移不同 → 没有效果（记账）。
* `run.py csa`：LV4-03 从"级别不同"变成"通过"；不回退。`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 / A-63 | `ret_new`：返回新对象（偏移 0） | 也认"同一个新对象 + 常数偏移 `k`"：`size = S − k` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 符号偏移（`return p + hdrLen;`，`hdrLen` 是形参）。
