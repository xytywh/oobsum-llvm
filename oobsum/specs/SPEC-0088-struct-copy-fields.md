# SPEC-0088 结构体整体赋值按字段拷值（`y = x;`、`*pstOut = st;`、`st = *pstIn;`）

状态：已实现
来源：错误 SAFE 排查的第二批探针：

```c
static void UseLen(UINT8 *p, const MSG_S *m) { p[m->len] = 0; }
void T2(void) { UINT8 a[4]; MSG_S x, y; x.len = 100; y = x; UseLen(a, &y); }   /* SUSPECT entry_unproven "m->len + 1"——y.len 的值丢了 */
```

结构体整体赋值降级成 `__oobsum_copy(&dst, &src, sizeof)`（A-06、SPEC-0051），按访问类模型处理：查完读写边界后 `Clobber` **清掉全部内存格**，
目标的字段值、连同无关对象的格都没了。

## 2. 设计

`__oobsum_copy` 是内部调用、语义确定（只写目标的 `sizeof` 字节），查完读写边界后不走 `Clobber`，改成：

1. 结构体类型：从目标 / 源实参的写法取（`&局部`、`&全局` → 它的类型；指针局部 → 所指类型），是记录、大小等于第三个实参。取不到 → 照旧 `Clobber`。
2. 两个指针的基址已知、偏移是常数，否则照旧 `Clobber`。
3. 叶子字段：递归展开嵌套的结构体，取整数与指针字段（数组成员不展开）。**先**按源读出每个叶子的值：整数 → `LoadValue`
   （格、形参所指的入口符号 `p0->len`、全局入口值都照常）；指针 → 源有格就取格，源是形参所指的照 `LoadValue`（类型从字段来），其余不拷（未知）。
4. 目标：与 `[off, off + sizeof)` 重叠的格失效，整段记"写过"（`written`、同类型形参别名 `AliasWritten`、`mods`），再把读出的值写成目标的格。
   **其余对象的格不动**（原来全部清掉）。

## 3. 对召回与误报的影响

**召回**：可靠——只拷读得出来的值，其余字段失效、读成未知；整段记写过，入口符号不会被当成没改过。原来按值能判的照旧。
**误报**：拷贝后的字段按真实值判（T2：`y.len = 100` → DEFINITE，原来 SUSPECT）；赋值后无关对象的格也还在。

## 4. 实现位置

`core/Interp.cpp`（`CopyStruct`、`ScalarLeaves`；调用的模型分支）。

## 5. 测试

* lit `struct-copy-fields.test`：`y = x` 后 `UseLen(a, &y)` → DEFINITE、`len = 2` → SAFE；`*pOut = st`（形参目标）后调用方按写的值判（写效应）；
  `st = *pIn`（形参源）→ 字段是 `p1->len`；指针字段经拷贝（"via y.buf"）；对照：拷贝之后再改源 → 目标不变；`Dump=state` 里目标的格。
  原来的 `struct-copy.test`（SPEC-0051 的访问模型）照旧通过。
* `unmapped-text.test`（SPEC-0067）原来借结构体赋值让字段"不知道"，现在赋值判准了（POSSIBLE 100）；输入改用 `memcpy` 整体拷贝，守住原来要测的文本。
* 单测 `Interp.StructCopyFields`。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=state`（目标的格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-06 / SPEC-0051 | `__oobsum_copy` 按访问类模型（之后清内存格） | 另：按字段拷值，只失效目标那一段 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 数组成员按元素拷；`memcpy` / `memcpy_s` 的常数长度拷贝同样按字段（`memcpy_s` 出错时不拷，要分情形）。
