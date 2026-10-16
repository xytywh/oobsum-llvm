# SPEC-0034 `strcpy_s` 族的目的一侧：`upto` 条件与来源标签 `model`

状态：已实现（S7d-2b）
来源：`STATUS.md` S7d-2；06- F-03 / F-04（securec 的 `destMax` 规则）、F-06、D-22（来源标签）、06- §1 P1 范围（"`strcpy_s` 族的源串长度 `[2027]`（`destMax` 一半可在 P1 顺带）"）；
SPEC-0001（标签定义：POSSIBLE = 代码给出的界下可能越界）；SPEC-0033（模型文件）。

## 1. 要解决什么

`strcpy_s(dest, destMax, src)` 等字符串函数在业务代码里和 `memcpy_s` 一样多，现在不在模型表里：调用点只记 `not_in_tu`（SUSPECT）。
它们与 `memcpy_s` 不同：securec 最多写 `min(destMax, 实际长度 + 1)` 字节（出错时只写 `dest[0] = '\0'`），**不是**总写 `destMax` 字节。
所以 `destMax > ext(dest)` 是"代码给出的界下可能越界"（POSSIBLE），不是 DEFINITE；源串长度（`[2027]`）不建模。

## 2. 设计

### 2.1 访问条件的 `upto`

`AccessCond.upto`（YAML `upto: true`）：`bytes` 只是上界，实际访问 `1 … bytes` 字节。解释时把字节数换成区间 `[1, bytes 的上界]`：

* `bytes` 是确切值 → 区间的标签是新的来源标签 **`model`**（库函数的大小实参给的上界）；
* `bytes` 本身是区间 → 标签取 `WorseTag(原标签, model)`（来自类型范围的仍是 `type`，判不了就是 SUSPECT）；
* `bytes` 未知 → 照旧未知。

于是：偏移 + 1 已经超出对象 → DEFINITE；偏移 + destMax 超出 → POSSIBLE（"the bound comes from a size argument of a library function"）；都不超 → SAFE。

### 2.2 来源标签 `model`

D-22 加 `model`：代码给出的界（`IsCodeTag`，可判 L2）；合并优先级排在 `data` 之后、`check` 之前（比所有其它代码给出的界都"差"）。
同步：`core/Range`（枚举、名字）、`core/Alert`（短语）、`summary.schema.json`（`tag` 的枚举）、`replay.py`（`CODE_TAGS`、合并顺序、`BOUND_PHRASE`）。

### 2.3 内置表新增（destMax 下标都是 p1，F-03 的 `destMax` 规则照用：0 或大于上限不写、回绕的减法生成守卫）

| 函数 | 目的一侧 |
|---|---|
| `strcpy_s(dest, destMax, src)`、`strncpy_s(dest, destMax, src, count)` | `W p0[≤p1]` |
| `strcat_s(dest, destMax, src)`、`strncat_s(dest, destMax, src, count)` | `W p0[≤p1]` |
| `sprintf_s(dest, destMax, fmt, …)`、`vsprintf_s(dest, destMax, fmt, ap)` | `W p0[≤p1]` |
| `snprintf_s(dest, destMax, count, fmt, …)`、`vsnprintf_s(dest, destMax, count, fmt, ap)` | `W p0[≤p1]` |

不加：宽字符版本（`wcscpy_s`、`wmemcpy_s`…：`destMax` 以元素计，要乘 `sizeof(wchar_t)`，以后）；`scanf_s` 族、`gets_s`（少见）；源一侧（串长，`[2027]`）。
`strncpy_s` / `strncat_s` / `snprintf_s` 实际最多写 `min(destMax, count + 1)`：不收紧（destMax 超过目的大小本身就违反 securec 的用法，按 POSSIBLE 报，召回优先）。

## 3. 对召回与误报的影响

原来记 `not_in_tu`（SUSPECT）的 `strcpy_s` 族调用点：`destMax ≤ ext(dest)` 证明了 → SAFE；超出 → POSSIBLE / DEFINITE；证明不了（类型范围、未知）→ 仍是 SUSPECT（带原因）。
没有调用点从"报"变成"不报"，除非证明了安全。

## 4. 实现位置

`core/Models.{h,cpp}`（`upto`、默认表、YAML `upto`、`DescribeModels` 写成 `W p0[<=p1]`）、`core/Interp.cpp`（`upto` 的字节数区间）、`core/Range.{h,cpp}`、`core/Alert.cpp`、
`oobsum/cases/summaries/summary.schema.json`、`replay.py`、`oobsum/config/oob_models.example.yaml`。

## 5. 测试

* 单元测试 `Models.DefaultKinds`（`strcpy_s` 是访问函数、`upto`）、`Models.MergeYaml*`（`upto` 读入）、`Range` 的标签名往返。
* lit `securec-string.test`：三层链上 `strcpy_s` / `snprintf_s` / `strcat_s`：destMax 超出 → POSSIBLE（短语）；偏移已经越界 → DEFINITE；destMax 等于大小 → SAFE（`ReportSafe`）；
  destMax 来自形参且调用方没给界 → 入口记账（SUSPECT）。
* `model-file.test` 的"示例文件 = 内置表"守住示例文件同步。

## 6. 调试

`Dump=models`（`W p0[<=p1]`）；`Dump=summary`（Req 的 `bound: model`）；`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-22 | 代码给出的界：`exact`、`const`、`check`、`mask`、`branch`、`loop`、`ret`、`guard` | 加 `model`（库函数大小实参给的上界，访问条件 `upto`；优先级在 `data` 之后、`check` 之前） |
| F-06 后 | — | 新增 **F-07** `strcpy_s` 族（见 §2.3）：目的一侧 `1 … destMax` 字节（`upto`），destMax 规则同 F-03；源串长度不建模 |
| SPEC-0002 §2.4 | 回退的豁免：同一处 SAFE / 改报在汇合点 / 同一函数里 SAFE | 加一条：访问在调用链上某层证明了 SAFE，且以它为终点的判定都是 SAFE 或报出了（LV7-05 `Name_Copy_OK`：原来的 `not_in_tu` 变成入口调用点的 SAFE） |

## 8. 公司侧验证

**等用户确认**：公司 securec 版本的 `strcpy_s` 族出错时只写 `dest[0]`（→ POSSIBLE，现在的做法）还是按 destMax 清零（→ DEFINITE；用例 LV7-05 注释是这么写的，`later=L1`）。
后者只需在内置表去掉 `upto`（或在模型文件里覆盖）。其余行为由 lit 守住。业务上 `strcpy_s` 族的告警条数可随"业务子目录的标签 / 原因计数"一起带回。

## 9. 待定 / 以后

* 宽字符版本；源串长度（`[2027]`）；`count` 收紧上界。
