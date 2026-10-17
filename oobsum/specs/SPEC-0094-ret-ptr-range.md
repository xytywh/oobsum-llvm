# SPEC-0094 `ret_ptr` 的多条返回偏移不同：记常数区间

状态：已实现
来源：SPEC-0070 §9 的待定项：

```c
static UINT8 *GetPos(MSG_S *m, UINT32 type) { if (type == 1) { return m->buf + 4; } return m->buf + 8; }
void T_TP(UINT32 t) { MSG_S m; UINT8 b[16]; m.buf = b; (void)memset_s(GetPos(&m, t), 12, 0, 12); }   /* 原来 SUSPECT untracked_pointer */
```

`ReturnPointer` 要求每条非空返回的基址与偏移都相同，偏移不同就不记 `ret_ptr`，调用方拿到未知指针。

## 2. 设计

* 推导：各条非空返回的基址相同（同一个 `*pK` 或 `*pK->f`）、偏移不同但都是常数（或界是常数的区间）→ `{kind: ret_ptr, target, range: [最小, 最大]}`（`maybe_null` 照旧）。
  偏移都相同 → 照旧 `value`。有一条偏移带符号且与别的不同 → 照旧不记。
* 调用点：`range` 时返回指针 = 实参的基址 + `[lo, hi]`（标签 `branch`），常数不用代换。

## 3. 对召回与误报的影响

**召回**：可靠——返回的指针一定是这几个偏移之一，区间盖住全部。**误报 / 级别**：取子缓冲区的函数分支返回不同偏移时，调用方的访问按区间判
（越界 POSSIBLE、界内 SAFE，原来 SUSPECT `untracked_pointer`）。偏移区间交给下一层时按 SPEC-0095 判级（不会把"可能越界"判成 DEFINITE）。

## 4. 实现位置

`core/Interp.cpp`（`ReturnPointer`、`RetPtrResult`）；`summary.schema.json` 不用改（`range` 本来就允许）。

## 5. 测试

* lit `ret-ptr-range.test`：`GetPos` → `ret_ptr p0->buf range [4, 8]`；写 12 字节 → POSSIBLE、写 8 字节 → SAFE；对照：偏移带符号且不同 → 照旧不记。
* `ret-ptr.test` 与单测 `Interp.RetPtrEffect` 里原来"偏移不同 → 不记"的对照有意更新：`Pick` 记 `range [2, 6]`，`Pick_TP` → POSSIBLE
  （这依赖 SPEC-0095：先做了 0095，否则按最小剩余会判成 DEFINITE）。
* `run.py csa`、`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs` 的 `ret_ptr` / `range`）、`Dump=state`（调用结果的指针）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 `ret_ptr`（SPEC-0070） | 各条返回同一个偏移 | 偏移不同但都是常数时记 `range` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 基址是全局的返回；偏移带符号且不同（要比较、取界）。
