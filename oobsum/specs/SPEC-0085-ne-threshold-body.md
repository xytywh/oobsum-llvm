# SPEC-0085 `!=` 阈值的候选扩到循环体里的比较（`while (1) { if (i == n) break; … }`）

状态：已实现
来源：SPEC-0082 的待定项（STATUS："退出测试不在循环头上"）：

```c
static void B1(UINT8 *p, UINT32 n) { UINT32 i = 0; while (1) { if (i == n) { break; } p[i] = 0; i++; } }   /* 原来 SUSPECT type_range_only */
```

循环头是 `while (1)`，`i == n` 在循环体的块里，SPEC-0082 只从循环头的出边取候选，取不到。

## 2. 设计

* 候选从**自然循环体里每个块**（含头）的出边上的 `x != c` / `x == c` 取，其余条件同 SPEC-0082（`x` 是本循环里被赋值的局部，那一块的指令不赋 `x`、
  也不赋 `c` 里用到的局部）。
* 可靠性不变：阈值只是循环头上候选的不变式，汇合两边都要证明 `x ≤ c`，下一圈证明不了就照常 widen 掉——与比较在哪一块无关。
* `do { … } while (i != n)` 第一圈回来时 `i = 1` 证明不了 `≤ n`（`n = 0` 时真的会回绕），照旧不用阈值。

## 3. 对召回与误报的影响

**召回**：`while (1)` + `if (i == n) break;` 的越界判出级别（原来记账）。**误报**：界内的判 SAFE。

## 4. 实现位置

`core/Interp.cpp`（`Run`：`loopBlocks`、`neBounds`）。

## 5. 测试

* lit `ne-loop.test` 加 `while (1) { if (i == n) break; p[i] = 0; i++; }` → 17 POSSIBLE、16 SAFE；对照 `do { p[i] = 0; i++; } while (i != n);` → 照旧记账。
* 单测 `Interp.NeThresholdFromBody`（`end=[1, p1] bound=check`）。
* `run.py csa`、`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=state`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0082 §2 | 候选：循环头 H 出边上的比较 | 自然循环体里每个块出边上的比较 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `do { … } while (i < n)`（先访问后检查，STATUS 的已知精度问题）。
