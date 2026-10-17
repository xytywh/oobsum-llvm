# SPEC-0089 访问类模型（memcpy_s / memset_s / strcpy_s …）只失效目的对象的格

状态：已实现
来源：SPEC-0088 之后的探针：

```c
void M1_TP(const UINT8 *src) { UINT8 a[4]; UINT8 tmp[16]; MSG_S m; m.len = 100; (void)memcpy_s(tmp, 16, src, 16); UseLen(a, &m); }
/* SUSPECT entry_unproven "m->len + 1"：memcpy_s 写的是 tmp，m.len 也丢了 */
```

`Clobber` 对访问类模型也先 `s.cells.clear()`（注释："被调者可能经指针 / 全局写内存"）。可访问类模型的语义是确定的：只写模型条件里 `W` 的那几个实参所指的对象
（SPEC-0018 的表），不写全局、不写别的对象。securec 调用在业务代码里到处都是，每调一次函数里跟踪的字段、局部数组的格、全局全没了。

## 2. 设计

* 访问类模型（`models::Kind::Access`）：不再清掉全部格；对每个 `W` 实参：
  * 基址已知 → 这个对象所在的整块内存的格全部失效（偏移、长度不细分，保守）：同一基址、它的成员数组（形参结构体的 `P0.aucBuf` 与 `P0` 是同一块内存）、
    成员数组的外层对象及其成员数组（`EraseObject`）；`MarkWritten`（同类型形参别名照旧）、`mods` 照旧；
  * 基址不知道、或实参不是指针 → 照旧清掉全部格、记 `clobbered`。
* 其余种类（没有摘要 / 模型的被调者）照旧清掉全部格。纯函数、释放函数本来就不清。

## 3. 对召回与误报的影响

**召回**：可靠——模型只写 `W` 实参所指的对象；那个对象整块失效。**误报**：securec 调用之后，其它对象的字段、全局按真实值判（M1：DEFINITE / SAFE，原来 SUSPECT）。

## 4. 实现位置

`core/Interp.cpp`（`Clobber`、`EraseObject`）。

## 5. 测试

* lit `access-model-cells.test`：`memcpy_s(tmp, …)` / `memset_s(tmp, …)` 之后 `m.len` 照旧 → DEFINITE / SAFE；对照：`memcpy_s(&m, …)` 写的就是 `m` → 照旧 SUSPECT；
  形参结构体先经成员数组写 `idx[1] = 2`、再 `memset_s(pc, …)` 整个结构体（以及反过来）→ 不能当 `idx[1]` 还是 2（SUSPECT，不是 SAFE）。
* 单测 `Interp.AccessModelKeepsOtherCells`。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=state`（调用后的格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-65 访问类模型 | 按条件生成访问检查；调用后内存格清空 | 调用后只失效 `W` 实参所指对象的格 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `memcpy_s` / `memcpy` 的常数长度拷贝按字段拷值（`memcpy_s` 出错时不拷，要分情形）。
