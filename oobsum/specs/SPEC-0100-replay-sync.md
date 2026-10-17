# SPEC-0100 `replay.py` 同步 SPEC-0095（实参偏移区间）与 SPEC-0099（`nn:p<k>`）

状态：已实现
来源：`engine_check.py` 把引擎算出的局部事实交给 `replay.py` 推到底，`replay.py` 是 `apply::Classify` / `BuildMap` 的参考实现（代码注释"同 replay.py 的 …"）。
SPEC-0095、SPEC-0099 只改了 C++：`replay.py` 不认 `off_lo` / `off_hi` / `off_bound`，也不认 `nn:p<k>`（不换、不改名，会被当成调用方自己的同名符号）。
金标准用例里没有这两种写法，所以 `engine_check` 仍是 14/16；以后用例一多，两边就会分叉。

## 2. 设计

照 C++ 逐条镜像：

* `build_map`：`nn:p<k>` 换成实参的说法（常数 0 → 0、非 0 → 1；对象是局部 / 全局 / 字符串 → 1；`*p<j>` → `nn:p<j>`；其余不换）。
* `subst` 的 poison、`is_iface`：`p<k>` 的正则前面允许 `nn:`。
* `Env`：指针形参给 `nn:p<k>` 的类型范围 `[0, 1]`。
* `classify`：Req 的 `have` 正好是 `ext(pK) + c`、第 K 个实参带 `off_hi` 时，`have` 加 `off_hi`、末端加 `[off_lo, off_hi]`（没有 `off_lo` → 最小末端 0），
  标签并入 `off_bound`（取更差的，顺序同 C++ 的 `Tag`）。`parent_call` 把 `off_*` 带给外层对象。

## 3. 对召回与误报的影响

无（参考实现与引擎一致；金标准不受影响）。

## 4. 实现位置

`oobsum/cases/summaries/replay.py`（工具，不是用例源码）。

## 5. 测试

* `replay.py`：16 个用例 0 处不一致（金标准没有这两种写法）；`Apply.AllCasesMatchReplay` 照旧通过。
* lit `replay-sync.test`：小脚本直接调 `replay.classify`，与引擎（`Propagate.ArgOffsetRange` / `NonNullMap`）判得一样：
  偏移区间 → L2（剩余 16、末端 19）、类型范围 → 记账、传 `NULL` → safe、本地缓冲区 → L1、调用方形参 → 往上传、守卫 `nn:p1 >= 1`。
* `engine_check` 14/16 不变。

## 6. 调试

`replay.py` 的逐调用点输出。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 以后改 `apply::Classify` / `BuildMap` 时同一个提交里改 `replay.py`（写进 STATUS 的注意事项）。
