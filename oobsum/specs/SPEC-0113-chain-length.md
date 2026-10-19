# SPEC-0113 链长用例生成与检查（06- T-04、G1）；本函数里直接访问封装分配器返回的对象时带上 eff_depth

状态：已实现
来源：计划 P0-8 / WP6：`cases/gen_chain.py` 生成 4 类模板 × N ∈ {2, 3, 5, 10, 17, 20}（06- T-04）；G1 的口径"模板 1、2、4 在 N = 3…20 全报、N = 2 不报"。
之前只有性能探针 `devenv/perf-chain.py`，没有验收用的链长用例。

## 2. 设计

* `oobsum/cases/gen_chain.py --out D [--check] [--clang C] [--triple T]`：
  * 模板 1 形参透传：`F0(p, i) { p[i] = 0; }`、`F_k(p, i) { F_{k-1}(p, i); }`；入口 `a[16]`，TP 传 16、TN 传 15。
  * 模板 2 偏移累加：每层 `p + 1`；入口 `a[32]`，下标按累加的偏移算到恰好越 1 字节 / 恰好不越。
  * 模板 3 封装分配器链：`A0(n) { return malloc(n); }`、`A_k(n) { return A_{k-1}(n); }`；入口 `p = A_{N-1}(16)`，TP 写 `p[16]`、TN 写 `p[15]`。
  * 模板 4 跨 TU 链：模板 1 每个函数一个文件（`oobsum_run.py local` 三阶段）。
  * N 是入口到访问点的调用边数（模板 3 是经过的 `ret_new` 层数）。`--check`：逐个跑（模板 1–3 单 TU 默认选项），TP 要有 DEFINITE 判定、N ≥ 3 时报出、
    N = 2 时不报（`inline_depth`）；TN 不报 DEFINITE / POSSIBLE。
* 引擎修正（模板 3 暴露）：本函数里直接访问封装分配器返回的堆对象时，Req 的 `eff_depth` 原来是 0（只有作实参交给被调者时才带 `PtrArg.effDepth`），
  入口上判 DEFINITE 后被层数过滤（F-20 按 `max(depth, eff_depth)`）。改为 `RawAccess` 记地址的 `effDepth`（堆对象），`ToReq` 写进 Req。

## 3. 对召回与误报的影响

只多报：经三层及以上封装的分配器拿到的对象在本函数里越界，原来交给现有规则（不报），现在报。

## 4. 实现位置

`oobsum/cases/gen_chain.py`；`core/Interp.cpp`（`RawAccess::effDepth`、`Access`、`ToReq`）。

## 5. 测试

lit `chain-length.test`：24 个链全部符合（约 1.5 秒）。`run.py csa`、`engine_check` 与原来相同。

## 6. 调试

`--check` 每行一个链的结论；生成的源文件与 WorkDir 在 `--out` 下。

## 7. 对主规格的修订

无（T-04 的实现）。

## 8. 公司侧验证

可选：`gen_chain.py --out` 生成后用 bxen 跑（单 TU 的模板 1–3），结论应与本地相同。

## 9. 待定 / 以后

无。
