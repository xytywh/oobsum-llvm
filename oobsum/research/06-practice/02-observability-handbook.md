# 可观测点手册：Infer 跑一次，到底能看到什么

> 回答的问题：**"执行流程每一步有哪些可观察的打印和输出？capture.db / results.db 里是什么？
> 怎么用 SQL 查？out 文件夹里有哪些需要的信息？"**
>
> 思路是**先看得见，再谈借鉴**。下面每一节都给出：这一步产生什么 → 怎么看 → 看到的长什么样。
> 所有输出都来自 `06-practice/demo-project/`（三文件工程），可直接复现。

---

## 0. 全流程与观测点总览

```
 阶段              命令                      落盘产物                    怎么看
 ─────────────────────────────────────────────────────────────────────────────────
 ① 捕获 capture    infer capture             infer-out/capture.db       SQL / --store-textual
    Clang AST → SIL IR                       ├ source_files 表           infer debug --procedures-cfg
                                             └ procedures 表（CFG）
 ─────────────────────────────────────────────────────────────────────────────────
 ② 分析 analyze    infer analyze             infer-out/results.db       ★ infer debug --procedures-summary
    过程内不动点 + 按需跨过程                  └ specs 表（17 个摘要列）      SQL 查哪些 payload 非空
                                             infer-out/stats/           stats.txt 计数器
                                             infer-out/logs             --debug 时逐节点 HTML
 ─────────────────────────────────────────────────────────────────────────────────
 ③ 报告 report     infer report              infer-out/report.json      jq / python
                                             infer-out/report.txt       直接读
                                             differential/*.json        infer reportdiff
```

---

## 1. 捕获阶段：`capture.db`

### 1.1 表结构（真实 `sqlite_master`）

```sql
CREATE TABLE source_files
    ( source_file TEXT PRIMARY KEY      -- 形如 'Rsrc/access.c'，前缀 R = 相对 project root
    , type_environment BLOB NOT NULL    -- 该文件的类型环境（Tenv），OCaml Marshal
    , integer_type_widths BLOB          -- int/long 等的位宽（跟目标平台走）
    , procedure_names BLOB NOT NULL     -- 本文件定义了哪些过程
    , freshly_captured INT NOT NULL     -- 本轮是否新捕获（增量分析用）
    , textual TEXT );                   -- 仅当 --store-textual 时非空：可读的 SIL 文本

CREATE TABLE procedures
    ( proc_uid TEXT PRIMARY KEY         -- 过程唯一 id
    , proc_attributes BLOB NOT NULL     -- 形参、返回类型、位置、是否 defined、changed 标志…
    , cfg BLOB                          -- ★ 这个过程的控制流图
    , callees BLOB NOT NULL );          -- 静态可见的被调过程名
```

**关键认知：`capture.db` 里没有"文件的 CFG"，只有"过程的 CFG"。**
文件在 IR 层只是一个把过程分组的标签，这正是 §4.3 说的
`Cfg.t = Procdesc.t Procname.Hash.t`——分析阶段能跨文件，根源就在这里。

### 1.2 能直接 SQL 查的东西

```sql
-- 捕获了哪些文件、本轮是否新捕获
SELECT source_file, freshly_captured FROM source_files;

-- 捕获了多少个过程 / CFG 多大（找超大函数）
SELECT proc_uid, length(cfg) AS cfg_bytes, length(callees) AS callees_bytes
FROM procedures ORDER BY cfg_bytes DESC LIMIT 20;

-- capture 成功率自检：compile_commands 有 N 条，这里应当接近 N
SELECT count(*) FROM source_files;
```

实测输出：

```
('Rsrc/access.c', 1)  ('Rsrc/main.c', 1)  ('Rsrc/middle.c', 1)

buf_get      attrs=174   cfg=507    callees=21
main         attrs=180   cfg=515    callees=38
mid_fetch    attrs=176   cfg=544    callees=36
```

### 1.3 ★ 把 IR 变成人能读的文本（最有价值的一个观测点）

BLOB 是 OCaml Marshal，SQL 读不出内容。但 Infer 提供了两条通道：

```bash
# 通道 A：capture 时存进 capture.db 的 textual 列
infer capture --store-textual --compilation-database compile_commands.json
python3 -c "
import sqlite3;c=sqlite3.connect('infer-out/capture.db')
print(c.execute(\"select textual from source_files where source_file like '%access%'\").fetchone()[0])"

# 通道 B：导出成 .sil 文件（更方便 diff / grep）
infer capture --store-textual --compilation-database compile_commands.json
infer debug --export-textual sil-out
ls sil-out          # access.sil  main.sil  middle.sil  manifest.json
```

`access.c` 的真实输出：

```
.source_language = "C" @?

type Buf = {len: int; data: int[]}

define buf_get(b: *Buf, i: int) : int {
  #node_0: @[3:1]
      jmp node_1 @[3:1]
  #node_1: @[3:37]
      n0:*Buf = load &b            @[3:44]
      n1:int  = load &i            @[3:52]
      n2:int  = load n0.Buf.data[n1] @[3:44]     ← ★ 这就是 b->data[i]
      jmp node_2 @[3:44]
  #node_2: @[3:37]
      ret n2 @[3:37]
} @[3:1]
```

把它和报告 §4.1 的 `Sil.instr` 五个构造子对上：
`load &b` 是 `Load {e = Lvar b}`；`load n0.Buf.data[n1]` 是
`Load {e = Lindex (Lfield (Var n0, data), Var n1)}`。
**想搞清楚"某个 C 写法会被降级成什么 IR"，看这个比读源码快得多**（报告 §14.4 定位
函数指针漏报的根因，用的就是这招）。

### 1.4 CFG / 调用图的图形化

```bash
# 单个过程的 CFG（Graphviz dot）
infer debug --procedures --procedures-cfg --procedures-filter "mid_fetch" --select all
# → infer-out/captured/middle.c.<hash>/mid_fetch.<hash>.dot

# 整个工程的语法调用图
infer debug --procedures --procedures-call-graph --select all
# → infer-out/syntactic_dependency_graph.dot

dot -Tpng infer-out/syntactic_dependency_graph.dot -o cg.png
```

### 1.5 过程属性

```bash
infer debug --procedures --procedures-name --procedures-attributes \
            --procedures-callees --procedures-filter "mid_fetch" --select all
```

看得到形参、返回类型、`loc`、`is_defined`，以及静态 callees。

---

## 2. 分析阶段：`results.db`

### 2.1 表结构

```sql
CREATE TABLE specs
    ( proc_uid TEXT PRIMARY KEY
    , proc_name BLOB NOT NULL
    , report_summary BLOB NOT NULL     -- 该过程产生的告警
    , summary_metadata BLOB            -- ★ 含 dependencies（谁读了谁的摘要）
    , AnnotMap BLOB, BufferOverrunAnalysis BLOB, BufferOverrunChecker BLOB,
      ConfigImpactAnalysis BLOB, Cost BLOB, DisjunctiveDemo BLOB,
      StaticConstructorStallChecker BLOB, LabResourceLeaks BLOB, LithoRequiredProps BLOB,
      Pulse BLOB, Purity BLOB, RacerD BLOB, ScopeLeakage BLOB, SIOF BLOB,
      Lineage BLOB, LineageShape BLOB, Starvation BLOB );

CREATE TABLE issue_logs  ( checker, source_file, issue_log BLOB );   -- 文件级分析的告警
CREATE TABLE procedures  ( ... );                                     -- 与 capture.db 同构，通常为空
```

**21 列 = 4 个固定列 + 17 个 payload 列**，每个 payload 列对应一种分析
（装了 MiniOobChecker 的自建二进制会变成 22 列，新列 `MiniOob` 由
`PayloadId.database_fields` 自动生成）。

### 2.2 ★ 最常用的一条命令：把摘要打印成人能读的形式

```bash
infer debug --procedures --procedures-name --procedures-summary \
            --procedures-summary-skip-empty --select all
```

> **注意**：不是 `--dump-json-summaries`。那个只导出 Pulse，
> BufferOverrun 的摘要在它的 JSON 里根本不出现——这是很容易走弯路的一个点。

真实输出（demo-project 的 `mid_fetch`）：

```
int mid_fetch(Buf* b, int k)
ERRORS:
BufferOverrunAnalysis: StackLocs: { } MemPure:
{ return -> (b->data[*] (modeled_range:10), _|_, { }),
  b->data[*] -> M(b->data[*] (modeled_range:10), _|_, { }) } Alias:
{ ret=_=b->data[*], ... }

BufferOverrunChecker: Safety conditions:
                      { k + 12 < 10 at src/access.c:3:44 reachable when { },
                        (k + 12):signed32 at src/middle.c:6:46 reachable when { } }
summary_loads= buf_get                            ← ★ 依赖边
recursion_edges= { }
other_proc_names=
type environments from sources= include/buf.h     ← ★ tenv 依赖（增量失效用得到）
```

逐字段读法：

| 字段 | 含义 |
|---|---|
| `BufferOverrunAnalysis` | 抽象内存状态。每个值打印成 `(itv, powloc, arrayblk)` 三元组 |
| `BufferOverrunChecker` | **安全条件集合**，即"还没判定的 requires"。这是摘要的主体 |
| `at <file>:<line>:<col>` | 条件**原始**产生的位置（叶子函数里那次真实访问），不随代换改变 |
| `reachable when { … }` | 可达性条件（来自 `LatestPrune`） |
| `summary_loads` | 本过程分析时读取了谁的摘要 → **增量失效的依赖边** |
| `type environments from sources` | 用到了哪些文件的类型环境 → 也参与失效传播 |
| `ERRORS:` | 本过程产生的告警 |

### 2.3 能直接 SQL 查的东西

```sql
-- 有多少个过程产出了摘要
SELECT count(*) FROM specs;

-- 每个过程产出了哪些分析的摘要（非空列 = 该分析跑过且有结果）
SELECT proc_uid,
       BufferOverrunAnalysis IS NOT NULL AS bo_analysis,
       BufferOverrunChecker  IS NOT NULL AS bo_checker,
       Pulse                 IS NOT NULL AS pulse
FROM specs;

-- 摘要体积排行：找分析开销/状态膨胀最严重的过程
SELECT proc_uid,
       length(report_summary)             AS report_bytes,
       length(BufferOverrunChecker)       AS bo_bytes,
       length(Pulse)                      AS pulse_bytes
FROM specs ORDER BY coalesce(pulse_bytes,0)+coalesce(bo_bytes,0) DESC LIMIT 20;

-- 确认序列化格式：开头四字节应当是 8495a6be = OCaml Marshal 魔数
SELECT proc_uid, hex(substr(BufferOverrunChecker,1,4)) FROM specs LIMIT 3;
```

**BLOB 里的内容 SQL 读不出来**（OCaml Marshal），只能用 §2.2 的命令。
`sql/queries.sql` 里有可直接 `sqlite3 infer-out/results.db < queries.sql` 跑的版本。

### 2.4 统计计数器：`infer-out/stats/stats.txt`

这是**判断分析行为**最快的一张表（不用读摘要就能知道发生了什么）：

```
=== global counters ===
ondemand_procs_analyzed: 3            ← ★ 实际分析了几个过程（= 过程数说明每个恰好一次）
pulse_disjuncts_dropped: 0            ← ★ Pulse 主动丢弃了多少析取状态（>0 = 发生欠近似=漏报）
pulse_interrupted_loops: 0            ← ★ 有多少循环因超过 widen 阈值被中断
pulse_summaries_count: { }            ← 摘要析取数分布，形如 { 20: 2 } 表示 2 个过程顶到上限 20
pulse_unknown_calls: 0                ← 遇到多少次无法解析的调用
pulse_summaries_contradictions: 0
=== percents per function and specialization ===
percent of function where 0 disjuncts were returned: 0%
```

`stats.jsonl` 是同一批数据的逐条 JSON，适合喂进你们自己的看板。

**上真实代码库后最该盯的三个**：`ondemand_procs_analyzed`（覆盖率）、
`pulse_disjuncts_dropped`（漏报风险）、`pulse_interrupted_loops`（循环处理是否退化）。

### 2.5 逐节点调试视图（要看抽象状态怎么一步步变的，用这个）

```bash
infer run --bufferoverrun-only --debug -- gcc -c foo.c
# → infer-out/captured/<file>.<hash>/nodes/<proc>.<hash>_node<N>.html
```

每个 HTML 里有：该节点的指令、**PRE STATE**、`exec_instr` 前后状态、`JOIN` 的输入与结果、
`WIDEN(num_iters=k)` 的 prev/next/result、`Fixpoint reached.`。
报告 §9.2 那条 `i: 0 → [0,1] → [0,9] → [0,10] → 不动点` 的证据就是从这里挖出来的。

```bash
# 提取正文（HTML 里有大段 CSS/JS，直接 grep 会很难看）
python3 - <<'EOF'
import re,glob,html
for p in sorted(glob.glob('infer-out/captured/*/nodes/*_node*.html')):
    t=open(p,encoding='utf-8',errors='replace').read().split('</style>')[-1]
    t=re.sub(r'<script.*?</script>','',t,flags=re.S)
    t=html.unescape(re.sub(r'<[^>]+>','\n',t))
    t=re.sub(r'\n{2,}','\n',t).strip()
    if 'WIDEN' in t or 'JOIN' in t:
        print('#####',p); print(t[:1500])
EOF
```

其它调试开关：`--bo-debug 0..4`（BufferOverrun 细节级别，`>=3` 会打印 traces 和阈值；
`>=4` 还会把"符号条件只传不报"那条分支关掉）、`--trace-ondemand`（打印按需调度的进出）、
`--print-logs`（把 `infer-out/logs` 直接打到终端）。

---

## 3. 报告阶段

### 3.1 `report.json` 每条告警的字段（真实一条）

| 字段 | 值 | 用途 |
|---|---|---|
| `bug_type` | `BUFFER_OVERRUN_L1` | 规则 id |
| `qualifier` | ``Offset: 17 Size: 10 by call to `mid_fetch`.`` | 人读描述 |
| `severity` / `category` | `ERROR` / `No_category` | 分级 |
| `file` / `line` / `column` | `src/main.c` / 5 / 12 | **报告位置**（不一定是根因位置） |
| `procedure` / `procedure_start_line` | `main` / 3 | 所在过程 |
| `bug_trace` | 8 个节点的数组 | ★ 跨过程路径，每项有 `level`/`filename`/`line_number`/`description` |
| `bug_trace_length` / `bug_trace_max_depth` | 8 / 2 | `max_depth=2` 说明跨了 2 层调用 |
| `key` | `main.c\|main\|BUFFER_OVERRUN_L1` | 粗粒度标识 |
| `node_key` | `7d8c45ed…` | 节点级哈希 |
| `hash` | `ab01f5c8…` | ★ **告警指纹，CI 去重/基线比对用这个** |

```bash
# 按规则统计
python3 -c "
import json,collections
d=json.load(open('infer-out/report.json'))
print(collections.Counter(x['bug_type'] for x in d).most_common())"

# 看一条告警的完整跨过程路径（根因在哪个文件一目了然）
python3 -c "
import json
x=json.load(open('infer-out/report.json'))[0]
for s in x['bug_trace']:
    print(f\"  L{s['level']} {s['filename']}:{s['line_number']}  {s['description']}\")"
```

### 3.2 重出报告 / 换格式 / 过滤

```bash
infer report                                  # 用已有 results.db 重新生成，不重新分析
infer report --issues-tests issues.tsv        # 机器友好的 tsv
infer report --report-console-limit 100
infer report --print-logs
```

### 3.3 版本间比对（CI 用）

```bash
infer reportdiff --report-previous base/report.json --report-current head/report.json
# → infer-out/differential/introduced.json / fixed.json / preexisting.json
```

### 3.4 `infer-out` 里其余文件

| 路径 | 是什么 | 要不要看 |
|---|---|---|
| `report.json` / `report.txt` | 最终告警 | ★ 要 |
| `capture.db` / `results.db` | 见上 | ★ 要 |
| `stats/stats.txt` / `stats.jsonl` | 计数器 | ★ 要 |
| `logs` | 分析日志（`--print-logs` 可直接打屏） | 排障时要 |
| `captured/<file>.<hash>/` | `--debug` 时的逐节点 HTML、`.dot` | 深入调试时要 |
| `.infer_runstate.json` | 本次运行的参数快照（复现实验时对参数很有用） | 偶尔要 |
| `differential/` | `reportdiff` 产物 | CI 要 |
| `costs-report.json` / `config-impact-report.json` | Cost / ConfigImpact checker 的专属报告 | 与越界无关 |
| `db.lock` / `procnames_locks` / `tmp/` | 运行期锁与临时文件 | 不用看 |

---

## 4. 一键把所有观测点 dump 出来

`06-practice/observe.sh`：

```bash
./observe.sh <infer-out 目录>
```

它会依次打印：capture 概况 → 过程与 CFG 大小 → 摘要（可读形式）→ 依赖边 →
payload 矩阵 → 统计计数器 → 报告与跨过程路径，输出到 `observation-report.txt`。

---

## 5. 从"看得见"到"能借鉴"——这些观测点分别对应我们方案的哪一块

| 观测到的东西 | 对应报告结论 | 对 CSA Summary 2.0 的启示 |
|---|---|---|
| `specs` 表 17 个 payload 列 | §5.2 Payload-per-Analysis | 摘要按分析分列存，不要一个大 schema |
| `BufferOverrunChecker: Safety conditions` | §12.1 证明义务 | **摘要主体应当是"还没判定的 requires"**，不是函数全部行为 |
| 条件里的 `at src/access.c:3:44` 不随代换改变 | §13.3 | 代换时保留原始访问位置，报告才能归因到根因 |
| `summary_loads` | §17 增量 | 在"读摘要"处埋点记依赖边，几乎零成本 |
| `type environments from sources` | §17.2 | 失效传播要算上类型依赖，不能只看调用图 |
| `pulse_disjuncts_dropped` | §11.1 | 析取域必须有丢弃策略，且要把丢弃量作为可观测指标暴露 |
| `WIDEN(num_iters=k)` 的 prev/next/result | §9.2 | 阈值 widening；把比较常量收进阈值集合 |
| `report.json` 的 `hash` / `bug_trace` | — | 告警指纹 + 跨过程路径是 CI 落地的基础设施，要早做 |
