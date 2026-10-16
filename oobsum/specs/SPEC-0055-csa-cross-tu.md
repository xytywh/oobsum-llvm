# SPEC-0055 `run.py csa`：多文件用例走 export → solve → report 三阶段

状态：已实现
来源：SPEC-0031 §9（"`run.py csa` 的跨 TU 用例改走 `local`"）；`run.py csa` 里两个多文件用例（LV1-07 年内承诺、LV6-01 尽力）一直显示"需跨TU"：
量尺对每个源文件单独跑 inline，跨文件的调用链判不出来。三阶段（SPEC-0028…0031）早已实现，S9 时验证过 LV1-07 / LV6-01 在三阶段下报 DEFINITE。只改测量。

## 2. 设计

* 用例单元有多个源文件、且 `--config` 里没有指定 `Phase=` 时：① 每个源文件 `Phase=export`；② 桩 TU（`<输出目录>/solve_stub/stub.c`）`Phase=solve`，
  加 `Dump=summary`（取调用图，SPEC-0047；`--config` 里有 `Dump=` 时不加）；③ 每个源文件 `Phase=report`（写判定日志）。`--config` 的其余键三个阶段都带。
* 判定日志、对照、回退检查与单文件相同；三阶段跑的用例不再标"需跨TU"（按真实结果对照：漏报就是漏报）。
* 单文件用例不变（inline）。

## 3. 对召回与误报的影响

无（测量）。

## 4. 实现位置

`oobsum/cases/run.py`（`cmd_csa`）。

## 5. 测试

* `run.py csa`：LV1-07、LV6-01 从"需跨TU"变成按真实结果对照（期望通过）；回退 0；基线有意更新。
* lit `cases-csa.test`（`--config Phase=report`）不变。

## 6. 调试

`cmake-build-release/oobsum-csa/<用例>/stderr.txt`（三阶段的命令与输出依次写在里面）、`<用例>/solved/`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0002 §2.3 | 多文件用例在 inline 下判不出来的记"需跨TU" | 多文件用例走三阶段，按真实结果对照；只有指定了 `Phase=` 时仍按单文件跑 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
