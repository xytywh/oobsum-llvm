# SPEC-0083 元素宽于 1 字节的指针比较：收窄出的上界不当"代码给出的界"（修 SPEC-0081 的 POSSIBLE 误报）

状态：已实现
来源：SPEC-0082 之后的探针：

```c
static void W1(UINT32 *p, UINT32 n) { UINT32 *e = p + n; while (p < e) { *p = 0; p++; } }
void W1_TN(void) { UINT32 a[4]; W1(a, 4); }     /* SPEC-0081 之后：POSSIBLE "up to byte 19 of 'a' (16 bytes)"——误报 */
```

SPEC-0081 按字节偏移收窄：`p < e` → `p.off ≤ 4n − 1`。真实的 `p.off` 是 4 的倍数，最多 `4n − 4`；可域里没有同余信息，访问 4 字节的末端就成了 `4n + 3`，
安全的代码被判 POSSIBLE（SPEC-0081 之前是 SUSPECT `pointer_stepping`）。

不能按类型直接假设"偏移是元素大小的倍数"：解析代码里常见 `ENT_S *pEnd = (ENT_S *)(pucBuf + ulLen)`，`ulLen` 不一定是 `sizeof(ENT_S)` 的倍数，
`for (pst = …; pst < pEnd; pst++)` 的最后一个元素真的会越界——那样假设会漏报。

## 2. 设计

* 指针比较（SPEC-0081）收窄上界时看**被收窄的那个指针局部**声明的类型：指向 1 字节的类型（`char` / `UINT8`）→ 照旧，标签 `loop` / `check`；
  指向宽于 1 字节的类型、或不知道（`void *`）→ 照样收窄（界是对的），但标签标 `widen`（不是代码给出的界）：超出时报 SUSPECT `pointer_stepping`
  （文字里给出能访问到的末端），没超出时仍能判 SAFE。
* 有类型的指针按元素大小走（`p++`），这是这一类误报的主要来源；`UINT8 *` 按 4 字节走、经类型转换按 4 字节访问的少见写法，与整数 `i += 4` 的循环一样
  （区间域本来的局限：区间里的值不一定都取得到，`for (i = 0; i < n; i += 4) *(UINT32 *)(p + i)` 的 TN 现在也报 POSSIBLE），留给同余信息。
* 整数比较、SPEC-0082 的阈值不变（宽元素的 `!=` 循环本来就证明不了阈值）。

## 3. 对召回与误报的影响

**召回**：不变（原来报 POSSIBLE 的现在报 SUSPECT，仍然报）。**误报**：宽元素指针循环的安全访问不再报 POSSIBLE（回到 SPEC-0081 之前的 SUSPECT）。
以后有了同余信息（偏移总是元素大小的倍数），能证明的可以恢复 `loop`。

## 4. 实现位置

`core/Interp.cpp`（`AssumeCmp` 的指针分支按 `WrittenLocal` 的类型判断、`NarrowPtr`、`NarrowRange` 的 `coarse`）。

## 5. 测试

* lit `pointer-compare.test` 加 `UINT32 *` 的 `while (p < e)`：TP、TN 都是 SUSPECT `pointer_stepping`，界内的（`W1(a, 3)`）SAFE；`Dump=state` 体内 `p=*p0+[0, 4*p1 - 1]:widen`；
  `UINT8 *` 的照旧 POSSIBLE / SAFE。
* 单测 `Interp.WidePointerCompareIsCoarse`（`end=[4, 4*p1 + 3] bound=widen`）。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=state`（收窄后的区间标签 `:widen`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0081 §2 | 收窄区间那一边的偏移 | 元素宽于 1 字节（或不知道）时，收窄出的上界标 `widen` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 同余信息（偏移 ≡ r mod 元素大小，汇合 / widen / 加常数时保持）：宽元素的 `<` / `!=` 指针循环、`i += 2` 的整数循环都能判准。
