# SPEC-0043 汇合时的符号界：一边的界在另一边自己的守卫下也成立（符号形的钳位）

状态：已实现
来源：SPEC-0042 §9；业务代码在安全函数调用前最常见的写法：

```c
if (ulLen > ulDstLen) { ulLen = ulDstLen; }
(void)memcpy_s(pucDst, ulDstLen, pucSrc, ulLen);
```

两条路：`ulLen` 不变、守卫 `ulLen <= ulDstLen`；`ulLen = ulDstLen`。共同守卫为空，`max(ulLen, ulDstLen)` 比不出来，常数界（SPEC-0042）也求不出来
→ 合并成 Top → 源一侧的读记 SUSPECT（误报）。06- A-15、SPEC-0023、SPEC-0042。

## 2. 设计

`JoinInt` 的某一端在共同环境下比不出来时：先走 SPEC-0042 的常数界（常数最有用）；还求不出来再试**符号候选**：

* 候选 `b` 的上界 `Hb`：若在 `a` 那条路的守卫与类型范围下 `Ha <= Hb` 可证，可取 `Hb`；
* 候选 `Ha`：若在 `b` 那条路下 `Hb <= Ha` 可证，可取 `Ha`；
* 两个都成立（合并后的值其实是 `min(Ha, Hb)`，两个都是可靠的上界）：**不取变量自己的原值**（正在合并的局部若是形参，它的入口符号 `pK`），
  取钳位的目标——`if (ulLen > ulDstLen) ulLen = ulDstLen;` 取 `ulDstLen`。

下界对称。
每边的值在它那条路上都不超过所取的界（一边是可证，另一边就是它自己的界），所以合并的区间包含两边所有的值（可靠）。
标签取被选中那一边的标签（确切值 → `branch`）与另一边证明所用守卫的来源里更差的（取两边标签的 `WorseTag`）。

## 3. 对召回与误报的影响

**误报减少**：钳到形参 / 字段的写法证明 SAFE 或推到调用方判。**召回**：只有证明了才收紧；回归用例守住钳到更大的值时仍报。

## 4. 实现位置

`core/Interp.cpp`：`Comparer` 加两边的守卫与 `self`；`Join` 加 `selfOf`（局部号 → 形参的入口符号）；`JoinInt` 的符号候选（常数界之后）。

## 5. 测试

* lit `join-clamp.test` 续：`if (ulLen > ulDstLen) ulLen = ulDstLen;` 后 `memcpy_s(dst, ulDstLen, src, ulLen)` → `Dump=state` 是 `[0, p1]`，
  调用方 `(a16, 16, b64, 100)` → SAFE（源一侧读 16 ≤ 64）；`(a16, 32, b64, 100)` → 目的一侧 DEFINITE（destMax 32 > 16）；
  `if (i > ulMax) i = ulMax;` → `ulMax = 15` SAFE、`16` POSSIBLE；`?:` 钳到常数 16 仍取常数（POSSIBLE）。
* `run.py csa` 无回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=state`（汇合后的区间端点是符号）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-15 | （SPEC-0042）各自求常数界 | 先试符号候选：一边的界在另一边自己的守卫下也成立就取它；再各自求常数界 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `if (i >= n) i = n - 1;`（无符号 `n - 1`，`n = 0` 时回绕）：赋值那条路上没有 `n >= 1` 的守卫，证明不了，仍是 SUSPECT（正确）；
  要在回绕项上推理（`wrap32(n - 1) = n - 1` 当 `n >= 1`）以后再说。
