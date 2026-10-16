# SPEC-0049 调用点消去调用方局部时也进入不透明项（`div` / `mod` / `wrap`）

状态：已实现
来源：`run.py csa` 的"级别不同"LV2-06（**年内承诺**，期望 L2）：

```c
static void Bit_Set(UINT32 *pulMap, UINT32 ulIdx) { pulMap[ulIdx / 32] |= (1u << (ulIdx % 32)); }
void Cell_RefreshActive_TP(void) { UINT32 aulMap[2]; UINT32 ulUe; for (ulUe = 0; ulUe < 256; ulUe++) { Cell_MarkUe(aulMap, ulUe); } }
```

被调者上传的末端是 `4*div(ulUe, 32) + 4`；`ulUe` 是调用方局部（调用点 `locals`：`[0, 255]`，`loop`）。调用点把局部换成区间（A-60 ②，`eliminate_locals`）只处理
线性项里直接出现的局部，**不进入不透明项**——`div(ulUe, 32)` 里还留着局部 → 不是接口符号 → 记账 `entry_unproven`（"含调用方局部且没有界"）。06- A-62（代换并判定）、D-21。

## 2. 设计

`eliminate_locals(l, locs, upper)` 对系数为 `c` 的不透明项 `a`（内层含调用方局部时）求它在方向 `dir = ((c > 0) == upper)` 上的界：

| 项 | 界 |
|---|---|
| `div(x, n)` | `n > 0`：`div(x 在 dir 上的界, n)`（向下取整对 `x` 单调不减）；`n < 0`：方向反过来 |
| `mod(x, n)`，`n > 0` | 先求 `x` 的上下界：都是常数且 `0 <= lo`、`hi < n` → 就是 `x` 在 dir 上的界；只知道 `lo` 是常数且 `>= 0` → `dir` 上界 `n − 1`、下界 `0`（标签 `mask`）；否则不动 |
| `wrap(bits, x)` | `x` 的上下界都是常数且在 `[0, 2^bits)` 内 → `x` 在 dir 上的界；否则 `dir` 上界 `2^bits − 1`、下界 `0`（标签 `type`） |
| `mul(x, y)` | 不动（以后） |

内层局部的来源标签、非空约束（`lo − hi <= 0`）照常收集。C++（`apply::EliminateLocals`）与 `replay.py` 同一规则。

## 3. 对召回与误报的影响

**召回**：LV2-06 这类"下标 / 32 定位字""回绕减法"在调用点的循环区间下判出 L2 / SAFE（原来记账）。可靠性：每一步都是单调 / 区间包含的推导，
`wrap` 越过范围时取整个类型范围（不当成 L2 的代码界）。

## 4. 实现位置

`core/Apply.cpp`（`EliminateLocals`）、`oobsum/cases/summaries/replay.py`（`eliminate_locals`）。

## 5. 测试

* lit `eliminate-opaque.test`：位图 `idx / 32`（循环 256 → L2、64 → SAFE）；`(i % 4)` 下标；`wrap32(ul - 1)` 在 `ul ∈ [1, 16]`（实参 `u + 1`，`u ∈ [0, 15]`）→ SAFE、
  `ul ∈ [0, 16]`（含 0，回绕）→ 只剩类型范围。（`for (u = 1; …)` 这种从 1 开始的循环另有问题：循环头丢界，SPEC-0050。）
* 金标准：`replay.py` 0 处不一致（16 个用例里有没有受影响的，有就按 SPEC 有意更新）；`gen_apply_golden.py` 重生成后 C++ 与 Python 一致。
* `run.py csa`：LV2-06 从"级别不同"变成"通过"；不回退。

## 6. 调试

`Dump=apply`（调用点的结局）、`replay.py -v`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-62 | 调用方局部换成区间 | 也进入不透明项：`div` 单调代入、`mod` / `wrap` 在区间内时等同内层、否则取 `[0, n − 1]` / 类型范围 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `mul(x, y)` 两个都含局部时的乘积界。
