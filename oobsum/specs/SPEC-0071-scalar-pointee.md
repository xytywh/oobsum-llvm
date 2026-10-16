# SPEC-0071 经指针传进来的整数（`UINT32 *pulLen`）：`*pK` 当成形参符号 `pK->_0`

状态：已实现
来源：探针：

```c
static void Fill(UINT8 *p, const UINT32 *pLen) { for (i = 0; i < *pLen; i++) p[i] = 0; }
void A_TP(void) { UINT8 a[16]; UINT32 n = 20; Fill(a, &n); }   /* 越界 */
void A_TN(void) { UINT8 a[16]; UINT32 n = 16; Fill(a, &n); }
```

`*pLen` 读出来是 `data`（只有类型范围）→ 被调者里记 `type_range_only`，各个调用方都只有这一条 SUSPECT。业务代码里"长度经指针传进来（往往还是出入参）"很常见。
SPEC-0026 / D-20 的字段符号只给**指向结构体**的形参。

## 2. 设计

* **被调者**（`LoadValue`）：形参 K 指向宽于 `char` 的整数（`TypeKind::Int`，宽度 > 8；字节缓冲区的第 0 个字节不当标量），偏移 0、按整数大小读、进函数后没改过 → 伪字段符号 `pK->_0`（与 `pK->字段` 同一套：`fields` 表记
  `{off: 0, size, type}`，类型范围给 `Env`）。改过的照旧读成 `data`（SPEC-0040）。
* **别名**（`AliasWritten`）：原来只看"所指结构体相同"的形参；推广成"所指类型相同"（同一种结构体，或同宽同符号的整数）——`F(&n, &n)` 经一个写、从另一个读不再当入口值。
* **调用方**（`ArgFields`）：实参指向（宽于 `char` 的）整数局部 / 全局（或这种整数数组的第 0 个元素）、偏移 0 → 伪字段 `_0` 的当前值写进 `fields`（确切值；代码给出的区间按 SPEC-0069；
  不知道的不写，代换时按 SPEC-0063 当不知道）；实参是调用方自己的整数指针形参（别名）→ 没改过的不写（按别名换成调用方的 `pJ->_0`），改过的写当前值或未知。
* 文本：`->_0` 写成 `[0]`（"up to byte pulLen[0] may be accessed"）。

代换、判定、上传都不用改：`pK->_0` 就是一个接口字段符号。

## 3. 对召回与误报的影响

**召回**：经指针传长度的写法按调用方的值判（`n = 20` → POSSIBLE，原来记账）。**误报**：`n = 16` 判 SAFE（原来记账）。
取舍（同 SPEC-0040）：形参与全局的别名（`pLen` 指向全局、被调者又直接写这个全局）不处理。

## 4. 实现位置

`core/Interp.cpp`（`LoadValue`、`AliasWritten`、`PointeeKey`、`ArgFields`）、`core/Engine.cpp`（`Readable`）。

## 5. 测试

* lit `scalar-pointee.test`：`A_TP` → POSSIBLE、`A_TN` → SAFE；经调用方自己的形参原样传下去（别名）→ 上传成调用方的 `p1->_0`；`Set(&n)` 之后再传 → 记账；
  `F(&n, &n)` 先写后读 → 不当入口值；`Dump=summary` 的 `fields` 有 `p1->_0`。
* 单测 `Interp.ScalarPointee`。
* `ret-int.test` 的 `Raw`（`return *p`）按新结果更新：原来 `data` 范围，现在确切的 `p0->_0`；调用方入口上照旧只有类型范围、记账。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`fields`、`calls[].args[].fields`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-20 / SPEC-0026 | 字段符号 `pK->f` 只给指向结构体的形参 | 指向整数的形参：伪字段 `pK->_0`（`*pK`） |
| SPEC-0040 §2.2 | 同类型**结构体**形参算别名 | 所指类型相同（结构体或同宽同符号整数）的形参算别名 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 出参写回的整数（`*pulLen = n`，D-24 的 `out_int`）。
