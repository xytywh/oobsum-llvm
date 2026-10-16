# SPEC-0077 写效应 `global`：被调者写整数全局标量，调用方知道新值

状态：已实现
来源：SPEC-0064 的 `Callee_TP`：`SetLen(100)`（`g_ulLen = n`）之后再调 `Fill(a)`，调用方只知道 `g_ulLen` 被改过（`gvals` 写 `"?"`）→ SUSPECT `global_value`。
06- D-24 列了 `global`（P0 只失效），没有值。"先经设置函数改全局长度 / 个数，再调处理函数"是常见写法。

## 2. 设计

沿用 SPEC-0073 的写效应机制，候选格加上**整数全局标量**（`g:<名字>`，格 `(G<键>, 0, 大小)`）：

* 推导：每条返回路径上，全局格里是确切值 → 写了；格不在、进函数后没改过（`!Changed`）、也没调用过可能写任意全局的函数（`!called`）→ 没碰；其余不知道。
  规则同 `field`（相同值 → `value`；都是常数 → `range`，SPEC-0074；有路径没碰 → `may`）→ `{kind: "global", target: "g:<名字>", …}`。
* 调用点：按名字在本 TU 的全局表里找到它（找不到就不应用），新值按调用前的状态算（`CallerExpr`；`may` 时与调用前的值汇合），应用 `mods` 之后写进格。
  之后读它、或再调用别的函数时的 `gvals`（SPEC-0064）都用这个值。
* 调用点记录的 `gvals`（SPEC-0064）：全局的当前值是代码给出的区间时写成调用点局部 `_gv_<名字>`，区间放进 `locals`（同 SPEC-0069）；
  Interp 里代入被调者式子（`CallerExpr`）时区间照旧算"换不了"。
* 写效应把全局符号代进调用方之后，给 `Env` 登记它的类型（原来只在直接读全局时登记；否则 `g:g_ulIdx + 8` 的下界不知道，会多一条负偏移 SUSPECT）。
* schema：`eff.target` 的模式放宽到 `g:<名字>`。

## 3. 对召回与误报的影响

**召回**：经设置函数改全局之后的访问按新值判（`SetLen(100)` → `Fill` 判 POSSIBLE，原来记 `global_value`）。可靠：同 SPEC-0073。

## 4. 实现位置

`core/Interp.cpp`（`FieldSlots`、`RecordReturn`、`Effects`、`FieldWrites`、`GlobalsAt`、`RecordCall`、`CallerExpr`）、`summary.schema.json`。

## 5. 测试

* lit `global-at-call.test`：`Callee_TP` 从 `SUSPECT:global_value` 变成 POSSIBLE（末端 100，`gvals` 是 100）；新加对照 `Unknown_TP`（被调者写了不知道的值）
  守住 `global_value`。原来按"被调者改过全局 → 记账"写的三处按新结果更新：`mod-ref.test`、`callee-writes.test` → DEFINITE 100，`global-current.test` → POSSIBLE 24。
* lit `global-effect.test`：设置成常数 / 形参值 / 出错路径没写（`may`，调用前的 0 与 20 汇合成 `[0, 20]` → `_gv_` 局部 → POSSIBLE）；
  对照：写完又调用了不知道写什么的函数 → 不记，`global_value`。
* 单测 `Interp.GlobalEffect`。
* `run.py csa` 不回退；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`）、`Dump=state`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 / A-63 | `global` → P0 只失效调用方对该全局的缓存 | 被调者写确切值时带值（`value` / `range` / `may`），调用方写进格 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 全局符号按名字（不同 TU 同名 `static` 全局会混在一起，SPEC-0064 §9 的老问题）。
