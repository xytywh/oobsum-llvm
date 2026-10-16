# SPEC-0041 全局标量的当前值：调用过可能写全局的函数之后不用入口符号

状态：已实现
来源：探针发现的错误 SAFE（SPEC-0040 §9 记下的缺口"调用后全局标量的当前值"）；06- A-28（"其余全局 → `g:K`"，`g:K` 是**入口时**的值）、D-20；SPEC-0040（形参字段的同类问题）。

## 1. 要解决什么

```c
static UINT32 g_ulIdx;  static UINT8 g_aucTbl[16];
static void Idx_Inc(void) { g_ulIdx += 8; }
static void Tbl_Set(void) { Idx_Inc(); g_aucTbl[g_ulIdx] = 1; }
void Tbl_TP(void) { if (g_ulIdx < 16) { Tbl_Set(); } }
```

`Tbl_Set` 里调用 `Idx_Inc()` 之后读 `g_ulIdx`，读成入口符号 `g:g_ulIdx`；`Tbl_TP` 的守卫 `g:g_ulIdx < 16` 蕴含 `g:g_ulIdx + 1 <= 16` → **SAFE**。实际下标最大 23。

## 2. 设计

`LoadValue` 读全局标量（偏移 0、整数）：格里没有时，**只有进函数后它没可能被改过**才读成 `g:K`；否则读成 `data`（类型范围）。"可能被改过"：

* 本函数调用过**可能写全局**的函数之后（新的状态位 `called`）：不在模型表里的被调者（包括函数指针调用）。纯函数、释放函数、访问类模型
  （只写目的一侧，目的是全局时由下一条覆盖）、分配器不算；
* `clobbered`（经未知指针写过、把未知指针交给了被调者）；
* 全局的地址交给过被调者、偏移不定地写过（`writtenAll`），或写过与它重叠的格（`written`）——同 SPEC-0040 的 `Changed`。

调用之前读到的值（局部里存着的、守卫里的 `g:K`）不受影响。

## 3. 对召回与误报的影响

**召回**：修掉 §1 的错误 SAFE。**噪声**：调用过别的函数之后再读的全局，原来是 `g:K`（调用方从不代换全局符号，一路上传到入口记 `entry_unproven`，
或被调用方的守卫框住），现在是 `data` → 在本函数的调用点记 `type_range_only`。被调者到底写哪些全局（mod-ref）以后做，做了之后没写的全局又能用入口值。

## 4. 实现位置

`core/Interp.cpp`：`State::called`（相等、合并）、调用的处理、`LoadValue` 的全局分支。

## 5. 测试

* lit `global-current.test`：§1 → 调用方不再是 SAFE（SUSPECT）；对照：调用之前读的全局、只调用纯函数 / 访问类模型之后读的全局 → 照旧 `g:K`
  （守卫证明 SAFE / 推到入口记账）。
* `run.py csa` 无回退；`replay.py`、`engine_check` 视结果（有变化按 SPEC 有意更新）。

## 6. 调试

`Dump=state`（全局读出来的值）、`Dump=summary`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-28 | 其余全局 → `g:K` | 只在进函数后没可能被改过时；调用过可能写全局的函数、经未知指针写过、地址交给过被调者之后 → `data` |

## 8. 公司侧验证

不需要。业务子目录的计数（可选）里看 `type_range_only` 是否明显变多。

## 9. 待定 / 以后

* 全局的 mod-ref：摘要记"可能写哪些全局"（传递闭包），调用没写到的全局仍用 `g:K`（S8c-2）。
