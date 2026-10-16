# SPEC-0051 结构体整体赋值的内置访问模型 `__oobsum_copy`（规格有、模型表里漏了）

状态：已实现
来源：`run.py csa` 的"级别不同"LV3-04（**年内承诺**，期望 L2）：`astDst[i] = astSrc[i];`（`NBR_S` 结构体）。lower 按 06- A-06 写成
`call __oobsum_copy(&astDst[i], &astSrc[i], 4)`，A-06 规定它是"内置访问模型：W / R 各 sizeof 字节"，但 SPEC-0018 的默认模型表里**没有它**：
调用点只记 `not_in_tu`（"指针交给了 `__oobsum_copy`"），目的一侧的写没有 Req，`astDst` 的越界判不出来。所有结构体整体赋值 / 用结构体初始化都受影响。

## 2. 设计

* 默认模型表加访问模型 `__oobsum_copy`：`W p0[p2]`、`R p1[p2]`（同 libc `memcpy`，没有 `destMax` 规则）。
* 它是 lower 生成的内部调用，不是业务函数：`ModelFile` 的 `defaults: false` 时也保留（同调试内建函数的 `^oobsum_debug_`）；`Dump=models` 照常列出。

## 3. 对召回与误报的影响

**召回**：结构体整体赋值的目的 / 源两侧按 `sizeof` 判（原来只记 `not_in_tu`、目的一侧漏掉）。

## 4. 实现位置

`core/Models.cpp`（默认表、`defaults: false` 时保留）。

## 5. 测试

* 单测 `Models.DefaultKinds`（`__oobsum_copy` 是访问模型）、`Models.MergeYamlWithoutDefaults`（清空后仍在）。
* lit `struct-copy.test`：循环里按个数拷贝结构体数组到较小的目的 → POSSIBLE（原来 `not_in_tu`）；单个结构体赋值到偏移后的位置越界 → DEFINITE；拷贝进足够大的 → SAFE。
  `model-file.test`（示例文件 = 内置表）不变。
* `run.py csa`：LV3-04 从"级别不同"变成"通过"；不回退。

## 6. 调试

`Dump=models`、`Dump=summary`（调用点 `model: access`）。

## 7. 对主规格的修订

无（按 06- A-06 实现）。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
