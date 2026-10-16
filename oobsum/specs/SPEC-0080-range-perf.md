# SPEC-0080 性能：区间一遍求上下界、不透明项嵌套不超过 4 层（长调用链的指数爆炸）

状态：已实现
来源：合成大 TU 的性能探针（`F_i(pst, n)` 调 `F_{i-1}(pst, n − 1)` 的调用链，每层写字段、按返回值分支）：

| 链长 | 原来 | 本步之后 |
|---|---|---|
| 16 | 64 ms | 50 ms 左右 |
| 24 | 5 s | 63 ms |
| 40 | 超过 2 分钟 | 105 ms |
| 80 | — | 96 ms |
| 300 | — | 250 ms |
| 300（另加 2000 个全局） | 超过 10 分钟（停掉） | 724 ms |
| 300（另加 5000 个全局） | — | 1.2 s |

热点（perf）：`SymEnv::RangeOf`。每层把可能回绕的 `n − 1` 传下去，写效应与代换之后值里嵌套出 `wrap32(wrap32(… − 1) − 1)`；`Bound` 对 `wrap` 分别求内层的
下界与上界（两次递归），嵌套 N 层就是 2^N 次。另外原子里存着内层式子的文本，嵌套越深越长。

## 2. 设计

* `SymEnv::RangeBoth` / `BoundBoth`：一遍同时求下界与上界，每个原子只递归一次；`RangeOf` / `Bound` 改成取其中一边。结果与原来逐项相同
  （`wrap` 的标签都取上界的、`div` / `mul` 的方向、溢出即没有界，都照搬）。
* 按键 / 按名字找全局改成查表（只建一次）：原来 `ApplyMods`（`g:名字` 的 mods）、`LoadValue`、`Eval`（`&G…`）、写效应等每次都把模块里所有全局过一遍、拼字符串比较，
  头文件里声明了几千个 `extern` 全局时，每个调用点都是 O(全局个数)。写效应 `global` 的候选只取本函数直接引用的全局与被调者写效应的目标；调用点记录的
  `gvals` 只看状态里动过的全局（`called` / `clobbered` 时 `g:*` 已经兜住其余的）。结果不变。
* 不透明项（`div` / `mod` / `wrap` / `mul`）嵌套超过 4 层（同 06- A-13 定义式的"深度 ≤ 4"）→ 整式未知。未知就是"不知道"，可靠；
  金标准里最深只有 1 层，不受影响。`Atom::depth` 构造时算好。

## 3. 对召回与误报的影响

无（第一条结果完全相同；第二条只把超过 4 层的式子变成未知，会多记账、不会少报）。

## 4. 实现位置

`core/Range.{h,cpp}`、`core/LinExpr.{h,cpp}`、`core/Interp.cpp`（`GlobalOf` / `GlobalsNamed`、`FieldSlots` 缓存、`GlobalsAt`）、`oobsum/devenv/perf-chain.py`。

## 5. 测试

* 单测 `Range.RangeBothMatchesSides`（随机线性式与嵌套原子：`RangeBoth` 与两次 `RangeOf` 逐项相同）、`LinExpr.OpaqueDepthCap`。
* 性能：`oobsum/devenv/perf-chain.py`（合成 TU：G 个全局 + N 层链），数字见上表。compiler-rt 三阶段压测仍是 177 个 TU、1492 条判定、约 3.7 秒。
* lit、`run.py csa`、`replay.py`、`engine_check`、`Apply.AllCasesMatchReplay` / `lin-golden` 不回退。

## 6. 调试

无。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-13 | 定义式深度 ≤ 4 | 另：不透明项的嵌套也 ≤ 4 层，超过 → 未知 |

## 8. 公司侧验证

可选：业务全量里看单个 TU 的耗时分布（`Dump=stats` 与 bxen 的计时）。

## 9. 待定 / 以后

无。
