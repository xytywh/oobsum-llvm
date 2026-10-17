# SPEC-0090 形参结构体的成员数组与外层对象是同一块内存：写一边要失效另一边重叠的格（修错误 SAFE）

状态：已实现
来源：SPEC-0089 时想到的探针，**错误的 SAFE**：

```c
typedef struct { UINT32 n; UINT8 idx[4]; } CFG_S;
void S1(CFG_S *pc) { UINT8 a[4]; pc->idx[1] = 2; ((UINT8 *)pc)[5] = 200; UseIdx(a, pc->idx[1]); }   /* SAFE：实际写 a[200] */
void S2(CFG_S *pc) { UINT8 a[4]; ((UINT8 *)pc)[5] = 2; pc->idx[1] = 200; UseIdx(a, ((UINT8 *)pc)[5]); } /* 同上 */
```

形参所指结构体里的**数组成员**单独当基址（`ParamField`，id `P0.idx`，D-20 / D-31 的 `p0->idx`），标量字段与按字节偏移的访问用外层基址 `P0`。
store 只失效同一个 id 上重叠的格，同一块内存的另一种写法上的旧格还在。局部 / 全局结构体的成员访问用同一个基址加偏移，没有这个问题。

## 2. 设计

store（以及 SPEC-0088 的结构体拷贝写目标）失效格时，连带：

* 写成员数组 `P0.m`（成员在外层的偏移 `parentOff`）：外层 `P0` 上与 `[parentOff + off, + size)` 重叠的格（偏移不定时：整个成员的范围）失效；
* 写外层 `P0`：按 `P0` 所指记录的布局找出每个数组成员 `m` 的范围 `[fo, fo + fs)`，与 `[off, off + size)` 重叠（偏移不定时：全部）→ `P0.m` 上的格全部失效。
  找不到记录 / 成员 → `P0.` 开头的格全部失效（保守）。

## 3. 对召回与误报的影响

**召回**：修掉错误 SAFE（S1 / S2 → SUSPECT `type_range_only`：换一种写法读时旧格已失效，值不知道）。只多失效格，不会少报。**误报**：无新增；写标量字段（`pc->n = 5`）不碰成员数组上的格（范围不重叠）。

## 4. 实现位置

`core/Interp.cpp`（`InvalidateFamily`；`Store`、`CopyStruct`）。

## 5. 测试

* lit `member-alias-store.test`：S1 / S2 → SUSPECT（不再 SAFE）；对照：写标量字段后成员数组的格照旧（`pc->idx[1] = 2; pc->n = 5;` → SAFE）；局部结构体照旧。
* 单测 `Interp.MemberAliasStore`（不带修复时失败：读出 2、末端 `[3, 3]`）。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=state`（格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-12 内存格 | 写 → 失效同一基址上重叠的格 | 另：形参结构体的成员数组与外层对象互相失效重叠的格 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 联合体成员（同一偏移的不同写法）本来就同一基址，不受影响。
