# SPEC-0002 用例回归尺子：`cases/run.py csa`（`@expect` ↔ 标签的对照口径、召回不回退检查）

状态：已实现（S1）
来源：`STATUS.md` S1、`HANDOFF.md` §5 第 1 条（"先有尺子再写引擎"）；06- T-03（对照口径）、§9.3（"语料：`cases/run.py csa`"）；
SPEC-0001 §3 的"召回不回退原则"（"S1 的 `cases/run.py csa` 会对比标签变化"）；用户要求"任何提高精度的改动，都不能让原来报的变成不报，
除非证明了安全——用回归用例守住"。

## 1. 要解决什么

1. 引擎每接上一块，都要能一条命令看到 67 个用例（134 个入口）按 `@expect` 的对照结果。
2. 06- T-03 的口径是按"只报 L1 / L2、拿不准不报"写的；D16（SPEC-0001）改成召回优先后，`记账` 的入口应报 `SUSPECT`，
   `-` 的入口报 `SUSPECT` 也是允许的——口径要跟着改。
3. 召回不回退要有机器检查：与一份入库的基线比，原来报出的判定消失了（且没有证明安全）就失败。

## 2. 设计

### 2.1 怎么跑

```bash
python3 oobsum/cases/run.py csa                          # 全部用例，默认选项，对照基线 oobsum/cases/csa-baseline.json
python3 oobsum/cases/run.py csa --only lv1,LV2-03        # 只跑一部分（级别名或用例编号）；不与基线外的用例比
python3 oobsum/cases/run.py csa --config InlineDepth=0   # 额外的 -analyzer-config rb.enhance.ipa.OobSummary:<k=v>（可重复）
python3 oobsum/cases/run.py csa --save                   # 有意改变时：把这次结果写成新基线（不能与 --only 同用）
```

| 选项 | 默认 | 说明 |
|---|---|---|
| `--clang` | `<仓库>/cmake-build-release/bin/clang` | 用 `clang -cc1 -analyze`（同 `cc1-run.sh`），不经 driver |
| `--triple` | `armebv7-linux-gnueabi` | 公司"4 字节指针 + 大端"的本地近似（CLAUDE.md §4） |
| `--cases` | `oobsum/cases` | 用例根目录（下面是 `lv1`…`lv10`）；lit 用例指向自己的 `Inputs/` |
| `--out` | `<仓库>/cmake-build-release/oobsum-csa` | 每个用例一个子目录：`decisions/*.jsonl`（`WorkDir`）、`stderr.txt`；另有 `csa-result.json` |
| `--baseline` | `<--cases>/csa-baseline.json` | 基线；文件不存在时只提示、不比 |
| `--config k=v` / `--cc1-arg=X` | —— | 追加 checker 选项 / 任意 cc1 参数（测试用，如 `--cc1-arg=-DX`） |

每个用例单元（单文件，或跨 TU 用例目录里的每个 `.c`）跑一次 cc1，`-analyzer-checker=rb.enhance.ipa.OobSummary`，
`WorkDir=<out>/<用例编号>`，其余 checker 选项取默认（即交付默认：`Policy=recall`、`InlineDepth=2`、`Phase=inline`）。
cc1 失败、或该 TU 没写出判定日志的 TU 头 → 记为**运行错误**（退出码 1）。

### 2.2 判定归属

一条判定属于入口 `E`，当且仅当它的 `fn` 是 `E`（汇合点函数是该入口，同 T-03）。`fn` 以后是函数键（D-01…D-08），取最后一个 `:` 之后、
`#` 之前的部分当函数名。用例里不属于任何入口的**报出**判定，计入"其它报出"（逐条列出，进基线）。

入口的**实际**：报出的判定里最强的标签（DEFINITE > POSSIBLE > SUSPECT > SAFE）带原因；没有报出的，取日志里最强的标签并写上
`not_emitted_because`；日志里一条都没有 → `未判`。

### 2.3 对照口径（修订 06- T-03）

| `pa` | 实际 | TP 的对照 | TN 的对照 |
|---|---|---|---|
| `L1` | 报出 DEFINITE | **通过** | （没有这种组合） |
| `L2` | 报出 POSSIBLE | **通过** | |
| `L1` / `L2` | 报出其它标签（L2 报成 DEFINITE、L1 报成 POSSIBLE、报成 SUSPECT） | **级别不同**（单独列出，不算通过，同 T-03） | |
| `L1` / `L2` | 没有报出 | **漏报**；跨 TU 用例在 `Phase=inline` 下记 **需跨TU**（不算失败，等 solve） | |
| `记账` | 报出 SUSPECT | **通过**（D16：证明不了就报 SUSPECT） | **通过+S**（允许，交复核） |
| `记账` | 报出 DEFINITE / POSSIBLE | **超出预期**（列出） | **误报** |
| `记账` | 没有报出 | **漏报**（真实缺陷一条记录都没报出来，召回缺口）；跨 TU 用例同样记 **需跨TU** | **通过** |
| `-` | 没有报出（含 `未判`、只有 SAFE） | **通过** | **通过** |
| `-` | 报出 SUSPECT | **通过+S** | **通过+S** |
| `-` | 报出 DEFINITE / POSSIBLE | **超出预期** | **误报** |

* 同一入口报出多条时，实际一栏写条数（`DEFINITE×2`）；对照只看最强的那条。T-03 的"恰有一条"改为"最强的那条级别相同"，
  多条另行列出（以后 A-44 覆盖合并做完再收紧）。
* 汇总行按对照计数，另给"其它报出"条数与 TN 上 `SUSPECT` 的条数（大模型复核的工作量）。
* **不按对照结果判失败**（引擎还没接上，全部漏报是基线）；以后检查点（G1：`--only lv1,lv2`）再加严格模式。

### 2.4 召回不回退检查

基线 `csa-baseline.json` 入库，与 `csa-result.json` 同形：

```json
{"schema": "oobsum-csa-result/1",
 "options": {"triple": "armebv7-linux-gnueabi", "config": [], "cc1": []},
 "entries": [{"case": "LV1-01", "kind": "TP", "entry": "Cell_OnSetupTimeout_TP", "pa": "L1", "actual": "未判", "verdict": "漏报"}, …],
 "emitted": [{"case": "LV1-01", "site": "lv1/lv1_01_stat_enum_butt.c:52:5", "kind": "W", "fn": "…", "label": "definite", "reason": ""}, …]}
```

`site` 写成相对 `--cases` 的路径。检查规则：**基线里每条报出的判定（按 `用例 + site + kind` 认），这次必须在同一处仍有报出的判定，
或者在同一处有 `SAFE` 判定（证明了安全）**；否则记为**回退**，退回码 1。标签变化（SUSPECT → DEFINITE 之类）、入口对照的变化只列出、不判失败。

**修订（引擎接上之后，判定会"换位置"）**：同一次访问在精度提高后可能报在别处（入口账本记在函数声明处、改判后在调用点；callee 里的 SUSPECT 改成汇合点的 DEFINITE）。
报出记录另带 `access`（调用链终点，即访问点 `文件:行`），下面任一成立也不算回退：

* 同一处有任意 kind 的 `SAFE`（账本判定没有 kind，SAFE 有，SPEC-0024）；
* 这次有报出的判定、调用链终点就是这次访问（改报在了汇合点，SPEC-0023）；
* 同一个函数里这次访问被证明了安全（SAFE 的调用链终点是它，SPEC-0025）。

老基线里没有 `access` 的记录，按 `site` 去掉列号当访问点。
`--only` 时只比这次跑了的用例。

基线只在有意改变时用 `--save` 更新，并在提交信息里说明变化（同 `alerts.json`、`@expect`）。

## 3. 对召回与误报的影响

只是测量工具，不改 checker 的行为。回退检查让"把报出变成不报出"必须伴随同一位置的 `SAFE` 判定，否则提交前就会失败——这是 SPEC-0001 §3
召回不回退原则的执行手段。口径修订（§2.3）把 `SUSPECT` 计为召回、而不是误报，与 D16 一致。

## 4. 实现位置

* `oobsum/cases/run.py`：新增 `csa` 子命令（`cmd_csa`、`_entry_actual`、`_verdict`、`_regressions`）；`units()` 接受用例根目录参数（其它子命令不变）。
* `oobsum/cases/csa-baseline.json`：基线（本提交时引擎未接入：134 个入口全部 `未判`、报出 0 条）。

## 5. 测试

* lit `clang/test/Analysis/oobsum/cases-csa.test`：`Inputs/cases/` 下的合成用例用 `oobsum_debug_emit()`（SPEC-0001 §6.3）驱动出各种实际，
  覆盖：通过、级别不同、漏报（含被 `inline_depth` 过滤的）、需跨TU、TP 记账通过、TN 通过+S、TN 误报、TP 超出预期、其它报出；
  `--save` 写基线后：同一位置 SUSPECT → SAFE（`--cc1-arg=-DPROVEN`）不算回退；`--config Policy=balanced` 让 SUSPECT 不报出 → 回退、退出码 1。
* 全量：`python3 oobsum/cases/run.py csa`，预期 67 个用例无运行错误，与基线无回退。

## 6. 调试

* `<out>/<用例编号>/decisions/*.jsonl` 就是 checker 的判定日志，可直接 `python3 oobsum/tools/decisions.py <out>/<用例编号>` 汇总。
* `<out>/<用例编号>/stderr.txt`：cc1 的全部输出（告警文本、编译错误）；要看中间数据就加 `--config Dump=…`（`+` 连接），输出也在这里。
* `csa-result.json`：完整的对照结果，可与基线 `diff`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| T-03 对照口径 | `-` / `记账`：没有任何汇合点为该入口的告警；L1 / L2：恰有一条且级别相同 | 按本 SPEC §2.3（SUSPECT 计为召回；`记账` 的 TP 要报出 SUSPECT；`-` / `记账` 的入口只禁止 DEFINITE / POSSIBLE） |
| §9.3 "语料" | `run.py csa` 驱动 `clang -cc1 -load` 走完 export → solve → report | 目前用本仓库构建的 clang（checker 在树内）跑 `Phase=inline`；三阶段接上后加 `--phase` |
| T-09 G2 | 期望为 `-` / `记账` 的入口**零告警** | **提议**：零 DEFINITE / POSSIBLE 告警；SUSPECT 条数单列（D16 的直接推论，**等用户点头**，见 STATUS） |

## 8. 公司侧验证

不需要：`run.py csa` 只在本地跑（公司侧的用例是 `oobsum/testcase/`，T-05）。

## 9. 待定 / 以后

* 严格模式（`--strict`：有漏报 / 误报 / 级别不同就失败），G1 时对 `--only lv1,lv2` 打开。
* 跨 TU：三阶段（export → solve → report）接上后加 `--phase`，`需跨TU` 消失。
* 入口的"多条报出"在 A-44 覆盖合并做完后收紧为"恰有一条"。
* `alerts.json` 是否补 SUSPECT 条目（replay.py 目前把 `记账` 当成不报），在 S8（T-06 对照）时另写 SPEC。
