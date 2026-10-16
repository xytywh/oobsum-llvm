# SPEC-0061 出参式分配器写进结构体字段：`out_new` 的 `target` 可以是 `pK->字段`

状态：已实现
来源：`run.py csa` 的"级别不同"LV5-05（尽力，期望 L1）：

```c
static UINT32 Ue_AllocRes(UE_RES_S *pstUe) { pstUe->pucRes = Pub_MemAlloc(RES_LEN); if (pstUe->pucRes == NULL) return VOS_ERR; ...; return VOS_OK; }
void Ue_OnSetup_TP(UINT32 id) { UE_RES_S stUe; ...; if (Ue_AllocRes(&stUe) != VOS_OK) return; Ue_PrepareRes(&stUe, RES_LEN); /* 三层下写 pucRes[RES_LEN] */ }
```

用例头：`eff pstUe->pucRes := new(64) ∨ NULL`（alloc_free_identity 的 `{para: 0, offset: [4]}`）。SPEC-0053 的 `out_new` 只认 `*pK`（偏移 0 的格），
写进字段的新对象传不到调用方 → `stUe.pucRes` 未知 → 记账。

## 2. 设计

### 2.1 推导

`RecordReturn` 记下每个指针形参 K 所指对象里**所有**指针大小的格 `(PK, off, 指针字节数)`（原来只记 `off = 0`）。`Effects`：某个 `(K, off)` 在所有返回路径上都是同一个
新对象（或空指针常数）→ `out_new`：

* `off = 0`：`target: "pK"`（同 SPEC-0053）；
* `off > 0`：`target: "pK->字段名"`，字段名取形参所指记录里偏移为 `off`、大小为指针字节数的字段；并把 `pK->字段名` 写进摘要的 `fields` 表（`off`、`size`、`type`），
  调用点按它找偏移。找不到这样的字段（经类型转换写的）→ 不记（调用方照旧未知 → 记账，可靠）。

### 2.2 调用点

`pK->f`：第 K 个实参指向的对象（常数偏移 `o`）的格 `(基址, o + fields[pK->f].off, 指针字节数)` 强更新成新对象 `heap@调用点`。其余同 SPEC-0053（先按 `mods` 失效）。
之后读 `stUe.pucRes` 就是那个对象；把 `&stUe` 交给下一层时，字段值随实参 `fields` 带下去（SPEC-0026）。

### 2.3 读写与回放

`apply` 的 `out_new` 带 `field`（`pK->f` 时）；C++ `ApplyEff` / `SameApply` / `FmtApply` 与 `replay.py` 的 `apply_eff` / `same_apply` / `fmt_apply` 同步
（文本"实参 K 所指对象的 f := new(…)"）。schema 不变（`target` 模式本来就允许 `pK->f`，`apply` 本来就有 `field`）。

## 3. 对召回与误报的影响

**召回**：申请结果写进结构体字段的分配器（业务里"资源挂在上下文结构体上"的写法），之后经字段访问按真实大小判（原来记账）。只在每条返回路径都写了这个字段时才记。

## 4. 实现位置

`core/Interp.cpp`（`RecordReturn`、`Effects`、调用点）、`core/Apply.cpp`（`ApplyEff`、`SameApply`、`FmtApply`）、`replay.py`。

## 5. 测试

* lit `out-new-field.test`：LV5-05 同形 → `Dump=summary` 的 `Ue_AllocRes` 有 `out_new target p0->pucRes size 64`、`fields` 里有 `p0->pucRes`；TP DEFINITE、TN SAFE；
  对照：只有一条路径写字段 → 没有效果、记账。
* 单测 `Apply.OutNewTargets`：`pK` / `pK->字段` 的 `apply`（`arg`、`field`、代换后的大小）；两层字段不认。
* `run.py csa`：LV5-05 从"级别不同"变成"通过"；不回退。`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`、`fields`）、`Dump=state`（调用后局部结构体的格）、`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24（SPEC-0053） | `out_new`：`target: "pK"` | `target` 也可以是 `pK->f`（新对象写进形参所指结构体的指针字段） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 两层字段（`pK->a.b`、`pK->pstX->pucBuf`）；经类型转换写的字段。
