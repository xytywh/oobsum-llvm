# SPEC-0053 出参式分配器：效果 `out_new`（`Buf_Create(&p, len)` 把新对象写进 `*pp`）

状态：已实现
来源：`run.py csa` 的"级别不同"LV4-04（**年内承诺**，期望 L1）：

```c
static UINT32 Buf_Create(UINT8 **ppucBuf, UINT32 ulLen) { *ppucBuf = Pub_MemAlloc(ulLen); return (*ppucBuf == NULL) ? VOS_ERR : VOS_OK; }
static UINT32 Rpt_CreateBuf(UINT8 **ppucBuf) { return Buf_Create(ppucBuf, 64); }
void Rpt_Send_TP(void) { UINT8 *pucBuf = NULL; if (Rpt_CreateBuf(&pucBuf) != VOS_OK) return; Rpt_Prepare(pucBuf); /* 按 72 清零 */ }
```

用例头：`eff *ppucBuf = new(ulLen) ∨ NULL`。06- D-24 列了 `out_new`，schema 里也有这个名字，但没有实现：被调者经出参写回的新对象不传给调用方，
调用方的 `pucBuf` 是未知指针 → 只能记账。业务里"出参返回缓冲区"的写法很常见（I-42 的 `MEM_ALLOC=<名字>,<下标>` 非 −1 也是这一类）。

## 2. 设计

### 2.1 效果 `out_new`

`{kind: "out_new", target: "pK", size, maybe_null: true, eff_depth}`：被调者在**每条返回路径**上都把同一个新对象（可带常数偏移 `k`，同 SPEC-0052）写进了
`*pK`（形参 K 所指位置偏移 0、指针大小的格）。`size = S − k`（被调者形参的式子）。

推导（`Effects`）：记录那一遍里每个 `ret` 时，记下每个指针形参 K 的格 `(PK, 0, 指针字节数)` 里的值；所有返回都有这个格、都是同一个新对象同一个偏移（或空指针常数）→ 记一条。
与 `ret_cond` / `ret_new` 并存（`Buf_Create` 同时有 `ret_cond`：返回 `VOS_OK` 时对象不为空）。

### 2.2 调用点

被调者有摘要时，先按 `mods`（SPEC-0045）让被调者写到的位置失效，再对每条 `out_new`：第 K 个实参指向的对象（常数偏移 `o`）的格 `(基址, o, 指针字节数)`
强更新成新对象 `heap@调用点`（大小按实参代换，`eff_depth + 1`）。之后读这个局部（`pucBuf`）就是那个新对象。中间层（`Rpt_CreateBuf` 把自己的形参交给 `Buf_Create`）
于是也推出 `out_new`（传递）。

### 2.3 读写

`Eff.target` 允许 `pK`（原来只有 `field` 用 `pK->f`）；schema 的 `target` 模式放宽；`apply` 加 `out_new`（`arg`、`size`、`maybe_null`）；C++ `ApplyEff` 与 `replay.py` 的
`apply_eff` 同步。

## 3. 对召回与误报的影响

**召回**：出参式分配器申请的缓冲区按真实大小判（原来记账）。只在每条返回路径都写了出参时才记，有一条路径没写就不记（调用方照旧是未知 → 记账，可靠）。

## 4. 实现位置

`core/Interp.cpp`（`RecordReturn` 记出参格、`Effects`、调用点应用）、`core/Apply.cpp`（`ApplyEff`）、`replay.py`（`apply_eff`）、`summary.schema.json`。

## 5. 测试

* lit `out-new.test`：LV4-04 同形 → `Dump=summary` 的 `Buf_Create` / `Rpt_CreateBuf` 都有 `out_new`（`size p1` / `64`）；TP DEFINITE、TN SAFE；
  对照：只有一条路径写出参 → 没有效果、记账。
* `run.py csa`：LV4-04 从"级别不同"变成"通过"；不回退。`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`）、`Dump=state`（调用后局部格里的新对象）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 | `out_new` / `out_int`（只列了名字） | `out_new` 的写法与推导、调用点的应用（§2）；`out_int` 以后 |

## 8. 公司侧验证

不需要。I-42（`MEM_ALLOC=<名字>,<下标>` 非 −1，模型表里的出参式分配器）以后可接到同一个效果上。

## 9. 待定 / 以后

* `out_int`（出参写回长度）；模型表的出参式分配器（I-42）。
