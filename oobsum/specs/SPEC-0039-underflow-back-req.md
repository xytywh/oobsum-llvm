# SPEC-0039 负偏移（下溢）：反向 Req `dir: back` 与 `bext(p)`

状态：实现中（1/2：`bext` 原子已提交；反向 Req、实参 `back`、文本在 2/2）
来源：06- §3.1"负偏移（`off < 0`）同样是越界，按同一套级别判定"、D-23 的 Req 字段 `start`（"最小偏移，判负偏移用"）——**规格有、实现没有**：
`start` 写进了摘要但 C++ / replay.py 都不用。调试中构造的例子（`p[i - 1]`，`i = 0`；`q = p - 4; *q = 0`）都被判成 SAFE（错误的 SAFE）。

## 1. 要解决什么

现在的安全条件只有"末端上界 ≤ 剩余字节"（`end[1] <= have`），访问的起点从不检查：

```c
static void Neg_Set(UINT8 *p, INT32 i) { p[i - 1] = 0; }      void Neg_TP(void)  { UINT8 auc[16]; Neg_Set(auc, 0); }
static void Back_Set(UINT8 *p) { UINT8 *q = p - 4; *q = 0; }   void Back_TP(void) { UINT8 auc[16]; Back_Set(auc); }
```

## 2. 设计

下溢与上溢对称：`bext(p)` = 指针 `p` **前面**还有多少字节（它在对象里的偏移）。访问的偏移下界是 `lo`，下溢安全 ⟺ `−lo ≤ bext(base)`。
于是下溢可以写成一条普通的 Req，复用代换、上传、覆盖合并、调用点判定、入口记账的全部机制，只是对象的"剩余"换成 `bext`。

### 2.1 反向 Req（D-23）

`dir: "back"` 的 Req：

| 字段 | 写法 |
|---|---|
| `have` | 基址前面的字节数：形参基址 `bext(pK)`；形参指针字段 `bext(pK->f)`；局部 / 全局 / 堆 / 字符串、形参所指结构体的成员数组 → `0`（偏移从对象 / 成员起点算） |
| `end` | `[−hi(off), −lo(off)]`：访问越到基址前面的字节数（`hi(off)` 没有时下界写 `0`） |
| `bound` | 偏移的来源标签 |
| `guard`、`access`、`path` | 同正向；`access.end` 写 `back:<−lo>`（指纹与正向区分） |

只在**偏移下界不能证明 ≥ 0**（在守卫与类型范围下）时生成；下界是常数且 ≥ 0、或守卫蕴含 ≥ 0 的访问不生成（不增加 Req / 判定）。
成员数组的外层对象（SPEC-0022 的递归、SPEC-0035 的 `parent`）只查正向：成员起点就是下界（F-02 子对象），比外层更严。

### 2.2 `bext` 原子与调用点的 `back`（D-25）

* LinExpr / replay 的 Lin 加原子 `bext(x)`（与 `ext(x)` 同样的写法与代换规则）。
* 调用点的指针实参加 `back`：调用方这个指针前面还有多少字节（偏移下界，调用方的说法）。局部 / 全局 / 堆 / 字符串基址：偏移下界；形参基址 `pJ + off`：
  `bext(pJ) + 下界`；形参所指结构体的成员数组：相对成员起点的下界；字段里存的指针（`fields`）同样。偏移没有下界时不写 `back`。
* 代换：`bext(pK)` → 第 K 个实参的 `back`；**实参没写 `back`** → 代换成未知（`LinExpr` 的 unknown），这一条在调用点记账（`type_range_only`），不当成 0。

### 2.3 判定与文本

调用点判定（A-60）不变：末端上界 ≤ 剩余 → SAFE；末端下界 > 剩余 → L1；代码给出的界 → L2；否则记账 / 上传；入口上推不下去 → 入口账本。
判定记录（`reps[]`）与判定日志带 `dir`。文本：

* L1：`Definite out-of-bound write through a 1-level call chain: up to 4 bytes before 'auc' are accessed (0 bytes available before the pointer)`
* L2：`Possible out-of-bound write …: up to 4 bytes before 'auc' may be accessed (0 bytes available before the pointer); the bound comes from …`
* 入口账本：`Cannot prove the write … stays within '*p': up to 4 bytes before the pointer may be accessed`
* SAFE：`Proven within bounds: up to 4 bytes before 'auc' (8 bytes available before the pointer)`

C++（`core/Alert`、`Engine`）与 `replay.py` 的 `alert_msg` 同一模板。

## 3. 对召回与误报的影响

**召回**：修掉负下标 / 指针回退的错误 SAFE。**误报风险**：内存池内部"从用户指针往回找块头"（`(HDR *)p - 1`）在分析范围内且分配器被建模时，
会报 DEFINITE（用户可见对象之前的字节）——这类代码通常是平台库、不在业务 TU 里；在的话判定日志里可见、由复核过滤。
带符号下标、只检查了上界的（`if (i >= 16) return; p[i]`，`i` 是 `INT32` 形参）会多一条下溢的 SUSPECT（`i` 可以是负数，召回优先）。

## 4. 实现位置

`core/LinExpr`（`bext`）、`core/Summary`（`Req.dir`、`PtrArg.back`、`Rep.dir`）、`core/Interp`（生成反向 Req、实参 `back`）、`core/Apply`（代换、判定记录）、
`core/Solver`（`dir` 随 Req 走、覆盖合并分开）、`core/Alert`、`core/Engine`（文本、指纹）、`replay.py`、`summary.schema.json`、`golden_regen.py`。

## 5. 测试

* 单测：`LinExpr`（`bext` 解析、显示、代换）；`Summary`（`dir`、`back` 往返）。
* lit `underflow.test`：`Neg_TP`（`INT32` 下标 0 − 1）→ DEFINITE；`Back_TP`（`p − 4`、实参在起点）→ DEFINITE；`Back_OK`（实参 `auc + 8`）→ SAFE；
  `Neg_Checked_OK`（`if (i < 1) return;`）→ 不生成反向 Req；入口函数 `Back_Entry(UINT8 *p)` → 入口账本；无符号下标（`ul - 1` 回绕）不生成反向 Req。
* 金标准：`replay.py` 0 处不一致；`engine_check` 不回退（16 个用例里没有负偏移；有的话按 SPEC 有意更新）。

## 6. 调试

`Dump=summary`（反向 Req 的 `dir`、实参的 `back`）、`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-23 | Req 的 `start`：最小偏移（判负偏移用） | 负偏移用反向 Req（`dir: back`、`have` 是 `bext(…)`）判；`start` 保留作说明 |
| D-25 | 指针实参 `{obj, have, …}` | 加 `back`（指针前面的字节数） |
| D-20 / LinExpr | 原子 `ext(x)` | 加 `bext(x)` |

## 8. 公司侧验证

不需要。业务子目录的计数（可选）里看 `dir: back` 的判定有多少、是否集中在内存池代码。

## 9. 待定 / 以后

* 字段里存的指针的 `back` 只记一层（同 `fields`）。
* `start` 字段以后可去掉（反向 Req 取代它）。
