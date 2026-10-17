# SPEC-0070 效果 `ret_ptr`：被调者返回"实参指针（或它所指结构体的指针字段）加偏移"

状态：已实现
来源：06- D-24 / A-63 / A-70 列了 `ret_ptr`（base 是形参的返回值），没有实现。取子缓冲区的小函数在业务里很常见：

```c
static UINT8 *Msg_Body(UINT8 *pucMsg) { return pucMsg + MSG_HDR_LEN; }
static UINT8 *Ctx_Tail(CTX_S *pstCtx) { return pstCtx->pucBuf + 4; }
void Top_TP(void) { UINT8 a[16]; UINT8 *q = Msg_Body(a); Put(q, 12); }   /* 4 + 12 → 越界 */
```

没有它时调用方拿到的是未知指针 → 只能记 `untracked_pointer`。

## 2. 设计

### 2.1 推导（`Effects`）

记录那一遍里所有返回值都是指针、基址相同且是：

* 形参 K 指向的对象（`*pK`）→ `target: "pK"`；
* 形参 K 所指结构体的指针字段在**入口时**的值（`*pK->f`，SPEC-0040：改过的字段读出来不是这个基址）→ `target: "pK->f"`；

偏移是同一个确切式子（接口符号）→ `{kind: "ret_ptr", target, value: 偏移}`。有一条返回是空指针常数时加 `maybe_null`（与越界无关，只记下）。偏移不同、或基址不是形参的不记（调用方照旧未知）。

### 2.2 调用点

在应用 `mods` **之前**（字段要取调用前的值）：第 K 个实参的指针值（`pK->f` 时按被调者 `fields` 表里的偏移从实参所指对象读出字段的指针值，与调用方自己读 `pst->pucBuf` 同一套规则）
加上偏移（被调者形参换成可信的确切实参、全局按 SPEC-0064；换不了 → 偏移未知）→ 调用结果。之后对它的访问按真实对象判。

## 3. 对召回与误报的影响

**召回**：经"取子缓冲区"函数拿到的指针按真实对象判（原来记 `untracked_pointer`）。可靠：只在每条返回都是同一个基址、同一个偏移时记。

## 4. 实现位置

`core/Interp.cpp`（`ReturnPointer`、`RetPtrResult`、`Effects`、调用点；被调者式子换成调用方的那段从 `CondResult` 提成 `CallerExpr`，两处共用）。

## 5. 测试

* lit `ret-ptr.test`：`return p + 4` → `Dump=summary` 有 `ret_ptr target p0 value 4`，调用方 `Put(q, 12)` → DEFINITE、`Put(q, 11)` → SAFE；
  `return pst->pucBuf + 4`（局部结构体字段指向 16 字节数组）→ 同样判；经一层包装函数传递；对照：两条返回偏移不同 → 没有效果、记账。
* 单测 `Interp.RetPtrEffect`。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`）、`Dump=state`（调用结果的指针）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 | `ret_ptr`（只列了名字） | `{kind: ret_ptr, target: pK / pK->f, value: 偏移, maybe_null?}`，推导与调用点按 §2 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 偏移不同的多条返回（取区间）〔SPEC-0094 已做：都是常数时记 `range`〕；基址是全局的返回。
