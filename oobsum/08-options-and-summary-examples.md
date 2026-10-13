# 方案怎么比、摘要长什么样：按维度拆开的备选方案，以及 16 个用例的真实摘要

> 调研包 v3.13 · 2026-10-13 · 给专家讨论用。**Part A** 回答"到底有哪些方案、怎么比"；**Part B** 回答"每个函数的摘要具体是什么、多层越界是怎么一步步判出来的"。
>
> 依据：`05-` §5（旧的 A–G）、`06-spec.md`（规格）、`cases/summaries/`：16 个代表用例、**104 份函数摘要 JSON**，
> 以及回放器 `replay.py`（读 JSON、在每个调用点重做代换与判定、对照用例头的 `@expect`）——**16 个用例 32 个入口全部与期望一致，0 处不一致**【实测数据】。
> 用例本身没有改动。

---

## Part A　方案怎么比：先拆成五个彼此独立的问题

### A.0 旧的 A–G 为什么看着不正交

`05-` §5 列的 A–G 是七个"方案"，但它们回答的不是同一个问题：A 讲生成顺序和在哪判定，B、C 只讲调度方向，D 讲数据结构，
E、G 讲判定手段，F 只讲消费方。把它们放在一张表里比，相当于把"用什么发动机"和"装在哪辆车上"并列。

**一个完整的方案必须同时回答五个问题**：摘要里存什么、怎么生成、按什么顺序生成、在哪里消费和判定、跨文件怎么跑。
每个问题有几个选项，一个方案就是"每个问题各选一个"。下面先把五个问题和选项列清楚（A.1），再把旧的 A–G 放回格子里（A.2），
最后拼成几个完整方案来比（A.3）。

### A.1 五个问题与选项（★ 为推荐）

**Q1　摘要里存什么（内容）**

| 选项 | 做法 | 代表 | 好处 | 代价 |
|---|---|---|---|---|
| ① 只存前置条件 Req | 被调者对实参的要求（"需 `4*ulType + 4 <= 16`"） | Infer BufferOverrun | 简单 | 大小信息只能从调用方往下给。分配器在下、访问也在下的 **V 形**（LV4-01）、getter、校验函数做不了 |
| **② Req + 固定词表的效果 Eff** ★ | 再加：返回 / 出参 / 字段 / 全局的新对象与大小、取值范围、条件效应（`ret == 0 ⇒ id <= 16`） | 本设计 | V 形、getter、校验函数、上下文结构体字段（LV4-01、LV4-06、LV5-01）都能串 | 词表之外的效果会漏算（经别的指针改了字段） |
| ③ 只存依赖关系 | 哪些入参会流到哪个访问，不带数值 | SDG 的 summary edge、CodeQL 数据流 | 便宜、近似完备，适合预筛和切片 | 判不了越界，只能给候选 |
| ④ 析取 / 路径敏感摘要 | 每条路径一份条件 | Infer Pulse、CSA 的状态 | 分支相关的精度最高（LV3-06 按制式） | 条件数随分支指数增长——就是要躲开的路径爆炸 |

**Q2　摘要怎么生成（事实从哪来）**

| 选项 | 做法 | 好处 | 代价 |
|---|---|---|---|
| **① 自研抽象解释，跑在自己的 IR（FIR）上** ★ | 对每个函数做区间 + 符号界的不动点计算 | 不依赖 ExprEngine；任何函数都产出摘要；能单测、能离线 | 引擎本体要自己写（`06-` §9.1 估 `core/` 约 7 000 行 C++） |
| ② 收割 CSA ExprEngine 的探索结果 | 复用 CSA 的符号执行，把路径上的事实汇总成摘要 | 复用现成能力 | ExprEngine 放弃的地方（路径爆炸、循环到顶）**没有事实**——立项要解决的问题原样继承（`01-` §1） |
| ③ 导出事实 + Datalog / 规则推导 | 调用、赋值、访问、守卫导出成关系表，用规则算 | 规则可读、易增量 | 区间、widening、线性代换写在规则里很别扭，数值精度低 |
| ④ LLM 生成或补全 | 读函数体或头文件注释，写摘要 / 模型 | 能覆盖没有函数体的库、业务契约（LV9-04、Lv10） | 不可证明、不稳定 → 只能当"候选事实"，人工确认后才参与 L1 |

**Q3　按什么顺序生成（调度）**

| 选项 | 做法 | 好处 | 代价 |
|---|---|---|---|
| **① 自底向上全部算** ★ | 调用图强连通分量的逆拓扑序，每个函数一次 | 简单、确定、与层数无关 | 会算很多用不上的摘要（成本仍是线性） |
| ② 按需 | 从访问点往调用者追，结果缓存 | 只算相关的；天然适合增量 | 调度与缓存失效复杂 |
| ③ 自顶向下传上下文 | 每个函数合并所有调用点的实参事实（入口事实） | 最简单 | 调用点一多就合并失真（16 和 64 合成 `[16, 64]`）；要上下文敏感就得克隆，又回到爆炸 |

**Q4　在哪里消费摘要、在哪里判定**

| 选项 | 做法 | 好处 | 代价 |
|---|---|---|---|
| **① 生成引擎自己在调用点代换、判定** ★ | Req 代入实参，在汇合点判 L1 / L2（Part B 就是它） | 不依赖 ExprEngine，与层数无关 | 非路径敏感：`if (f) i = 20; if (!f) a[i];` 会报 L2，现有规则判得更准 |
| ② CSA 路径敏感探索时，在调用点用摘要代替 inline | `evalCall` / `checkPreCall` 读摘要；你们的 `SummaryApplyChecker` 就是这个方向 | 链顶有路径约束：误报更少、告警带路径 | 链顶自己路径爆炸时调用点根本走不到，又回到老问题 → 适合做**第二道确认** |
| ③ 在摘要库上用查询判定 | SQL / Datalog 查 `reqs`、`effs`、`opens` | 给人和 AI 用的查询接口、统计 | 数值推理弱，只适合找候选 |
| ④ LLM 读切片 + 摘要研判 | 把未判定的条件和跨函数切片交给 LLM | 能判业务语义（Lv10） | 按条计费、不可复现 → 只用于账本里的候选 |

**Q5　跨文件怎么跑（编排）**

| 选项 | 做法 | 好处 | 代价 |
|---|---|---|---|
| ① 多轮 TU 扫描 + 工作表 | 每轮只跑"被调者已就绪"的 TU（`01-` §3.11 方案 A） | 复用 bxen 全量模式 | 轮数取决于调用链跨文件的次数；同一个 TU 可能被解析多次 |
| **② 一遍导出 IR → 离线全局求解 → 一遍出告警** ★ | `06-` §7 的方案 B | 固定两遍；跨 TU 与同 TU 完全一样处理 | 要序列化 IR；求解进程要装下全量 FIR |
| ③ CSA CTU | 跨 TU 按需加载对方的 AST 再 inline | 现成 | 本质还是 inline，深度与路径爆炸问题不变 |
| ④ 全程序值流图 | 不按函数存摘要，在全局图上传播 | 全局、字段、异链天然统一 | 要全程序指针分析，重、难增量 |

（落盘格式 JSON 还是 SQLite 是实现细节，不影响能力，见 `06-` D10。）

### A.2 旧的 A–G 落在哪一格

| 旧编号 | 它实际回答的是 | 在上表里 |
|---|---|---|
| A 自底向上：条件 + 效果 | 内容、顺序、判定位置 | Q1②、Q3①、Q4① |
| B 自顶向下：入口事实 | 顺序 | Q3③ |
| C 按需反向查询 | 顺序 | Q3② |
| D 全程序值流图 | 编排（连带生成方式） | Q5④ |
| E 事实导出 + 规则查询 | 生成与判定 | Q2③、Q4③ |
| F CSA 消费摘要 | 判定位置 | Q4② |
| G 切片 + LLM 研判 | 判定（连带生成） | Q4④、Q2④ |

### A.3 拼成完整方案

| 方案 | Q1 内容 | Q2 生成 | Q3 顺序 | Q4 判定 | Q5 跨文件 | 擅长 | 最弱的地方 | 年内 |
|---|---|---|---|---|---|---|---|---|
| **S1 推荐**（= `06-` 规格） | ② | ① | ① | ① | ② | Lv1–Lv5 多层、V 形、字段；与层数无关 | 非路径敏感：分支相关的精度不如 inline | ✅ |
| S2 CSA 为主 | ① 或 ② | ② | ① | ② | ③ | 路径精度高；复用现有 checker 与 CTU | 生成依赖 ExprEngine，路径爆炸处没有摘要 | 只能延长现状 |
| S3 S1 + CSA 确认 | ② | ① | ① | ① + ② | ② | 在 S1 之上给 L2 降误报、补路径解释 | 多跑一次 CSA | P1 |
| S4 S1 的按需版 | ② | ① | ② | ① | ② | 大工程、增量 | 调度与缓存失效复杂 | 2027 |
| S5 事实库 + 规则 | ③ | ③ | ① | ③ | ② | 候选、统计、给 AI 的查询 | 数值弱，不能单独判越界 | 作 S1 的附属（查询库） |
| S6 LLM 主导 | （④ 补模型 / 规格） | ④ | —— | ④ | —— | 业务契约、没有函数体的库 | 不可证明 | 只用于账本候选 |

全程序值流图（Q5④）的成本与增量问题年内都过不去，不单列成方案。

### A.4 推荐与演进

* **年内走 S1。** 三条决定性的理由：Q2② 会继承路径爆炸（立项要解决的就是它）；Q1① 串不起 V 形与字段
  （承诺场景里 LV4-01、LV5-01、LV5-06 都是）；Q5② 的遍数不依赖调用图形态。
* **P1：在 S1 上加 Q4②，即 S3。** 对 L2 做一次 CSA 路径确认，降误报、告警带路径；可以在你们现有的 `SummaryApplyChecker` 上演进，
  它要读的就是 Part B 这种 JSON。
* **2027：** Q3②（按需 + 增量，S4）；Q2④ + Q4④（AI 补模型 / 规格、研判账本里的候选）；Q1③（依赖摘要）作大函数预筛。
* **不选：** Q1④（析取摘要，路径爆炸换个地方出现）；Q3③（合并失真）；Q5③（还是 inline）。

### A.5 讨论时值得请专家拍板的几件事

1. **Q4：L2 要不要先经 CSA 二次确认再报？**（即 S3 是否提前到年内。）取决于你们对 L2 误报的容忍度。
2. **Q1 的效果词表**：字段只跟踪"经形参两次解引用以内"，全局变量的**值**年内不跟踪（只用全局数组的**大小**），是否够用？
3. **判定口径**（B.2）：L2 只认代码给出的上界，类型范围不报；**入口函数的形参被访问侧的校验框住时直接判 L2**（LV3-03、LV4-06）是否接受？
4. **Q5②**：一遍导出全量 FIR 的磁盘占用与求解内存（探针 R3 会给出全量的函数数与规模）。
5. **账本**（`opens`）作为 AI + 程序分析的输入：Lv7–Lv10 的漏报都会在这里留下带原因的记录（B.1 的格式）。

---

## Part B　摘要实样：每个函数的摘要是什么，多层越界怎么判出来

### B.1 一份摘要文件长什么样

**一个函数一份 JSON。** 全量运行时落在 `WorkDir/summaries/<00..ff>.jsonl`（每行一份），`oobsum.py show <函数名>` 把一份打印成下面的样子；
本包的 104 份样例在 `cases/summaries/<用例>/<函数名>.json`，格式定义在 `cases/summaries/summary.schema.json`（JSON Schema，104 份全部校验通过）。

**顶层字段**

| 字段 | 含义 | 例 |
|---|---|---|
| `fn`、`key`、`loc` | 函数名、跨 TU 唯一的键（`06-` D-01…D-08）、定义位置 | `"F:Ue_InitTbl"` |
| `params` | 形参 `"名字: 类型"`；表达式里的 `p0`、`p1`… 按下标对应它们 | `["pstTbl: UE_S*", "ulNum: UINT32"]` |
| `fields` | 摘要用到的字段符号：偏移、大小、类型（按目标 ABI） | `"p0->ulBufLen": {"off": 4, "size": 4}` |
| `status` | `analyzed` / `partial:原因` / `skipped:原因`——"没分析"和"分析了没问题"分得开 | |
| `reqs` | **前置条件**：调用方必须满足、否则本函数（或它调用的函数）里会越界 | 见下 |
| `effs` | **效果**：对调用方有用的输出（新对象及大小、返回值范围、条件效应、写了哪个字段） | `{"kind": "ret_new", "size": "p0"}` |
| `calls` | **调用点记录**：本函数的每个结论是从哪个调用点、哪次代换来的——调试与追溯就看它 | 见下 |
| `reps` | **在本函数判定的越界**（汇合点在这里） | `{"level": "L2", "depth": 3, ...}` |
| `opens` | **账本**：判不了的地方和原因（漏报都在这里留痕） | `{"reason": "pointer_stepping", ...}` |

**一条 Req**（以 `Ue_InitTbl` 为例：`ulIdx` 在循环里被换成了 `[0, ulNum − 1]`；`//` 后面是这里加的说明，文件里没有）

```jsonc
{
  "id": "R1", "kind": "W", "bytes": "4", "obj": "*pstTbl",
  "have": "ext(p0)",                         // 从 pstTbl 起对象还剩多少字节
  "end": ["4", "16*p1 - 12"],                // 访问末端（偏移 + 字节数）的 [下界, 上界]
  "start": "0", "bound": "loop",             // 上界来自循环界——只有代码给出的上界才能判 L2
  "guard": ["p1 >= 1"],                      // 循环至少执行一次才有这次访问
  "access": {"fn": "Ue_InitOne", "at": "lv4_01_wrapper_alloc_spec.c:52", "code": "pstTbl[ulIdx].ulState = 0"},
  "path": ["Ue_InitTbl@lv4_01_wrapper_alloc_spec.c:59", "Ue_InitOne@lv4_01_wrapper_alloc_spec.c:52"],
  "depth": 1, "eff_depth": 0, "blind": false,
  "text": "ulNum >= 1 ⇒ W4 @ *pstTbl，末端 [4, 16*ulNum - 12]；需 16*ulNum - 12 <= ext(pstTbl)"
}
```

安全条件统一读作 **`end` 的上界 `<=` `have`**；`text` 是用形参名渲染的可读形式（派生字段，读入时忽略）。

**表达式写法**（都是字符串，人能直接读，C++ 侧用一个小解析器读）

| 写法 | 含义 |
|---|---|
| `16*p1 - 12` | 形参的线性式（整数系数） |
| `ext(p0)`、`ext(p0->pucBuf)` | 指针还剩多少字节（形参、形参的字段） |
| `p0->ulBufLen` | 字段在函数入口时的值（具体偏移见 `fields`） |
| `g:g_x` | 全局在入口时的值 |
| `div(p1, 32)`、`mod(e, n)`、`wrap32(p1 - 1)`、`mul(a, b)` | 不透明项：除以常数、取模、无符号回绕、两个变量相乘——代换成常数后才求值 |
| `p1 <= 31`、`div(p1 + 31, 32) >= 1` | 守卫 / 条件 |

**效果的种类**：`ret_new`（返回新对象，`size`、`maybe_null`、`eff_depth`）、`ret_ptr`、`ret_int`（返回值范围）、`ret_cond`（`ret == c ⇒ …`，校验函数）、
`out_new` / `out_int`（出参）、`field`（写了形参指向的结构体的哪个字段）、`global`、`invalidate`。

**调用点记录**（`calls[]`）：`site`、`callee`、`args`（整数实参写表达式；指针实参写 `{"ptr": 原文, "have": 还剩多少字节, "alias"?: 就是调用方的哪个形参, "fields"?: 栈帧里记着的字段值}`）、
`locals`（实参里用到的本函数局部在调用点的区间）、`guards`（调用点处成立的守卫，比如条件效应收窄出来的）、`results`（被调者每条 Req / Eff 在这里的结局：
`up` 上传成本函数的哪条、`safe`、`L1` / `L2` 对应哪条判定、`open` 记账原因）。**这一节是调试的主要入口**：任何一条告警或漏报，都能顺着 `calls` 一层层找到是哪一次代换得出的。

**判定**（`reps[]`）：`level`、`reported`（是否真的报——层数过滤后）、`site`（告警位置：汇合点的调用表达式）、`access`、`object`、`have`、`end`、`bound`、
`depth` / `eff_depth`、`chain`（逐层 trace）、`msg`（英文告警文本）、`fp`（指纹）。

**账本**（`opens[]`）：`reason`（`06-` D-27 的枚举）、`at`、`detail`；入口处到此为止的 Req 带 `req` 指回去。

### B.2 调用点上怎么判（结合字段看）

1. **取被调者的摘要**（`calls[].callee`）。
2. **建代换表**：整数形参 `p_i` ↦ `args[i]`；`ext(p_i)` ↦ `args[i].have`；字段 `p_i->f` ↦ `args[i].fields.f`，或经 `alias` 改写成调用方自己的字段符号。
3. **代换并化简**：`end`、`have`、`guard` 全部代进来，同类项先抵消；`locals` 里的局部换成区间，同时生成"区间非空"的守卫（循环至少执行一次）。
4. **判定**：

| 代换后的情况 | 结论 |
|---|---|
| 守卫为假 | **安全**：这次调用走不到访问（LV2-09 TN 传 0） |
| `end` 上界 `<=` `have` | **安全** |
| `end` 下界 `>` `have` | **L1**：所有取值都越界 |
| 上界 `>` `have` `>=` 下界，上界来自代码（校验、掩码、循环界、分支常量、返回值范围、守卫） | **L2** |
| 同上，但上界只来自类型范围 / widening | **记账**，不报（LV10-04 的 `UINT16`） |
| 还含调用方的接口符号，`have` 已知，且这些符号都被**代码给出的界**框住（访问侧校验带上来的守卫、调用点的守卫） | 按界判 安全 / L1 / L2（LV3-03、LV4-06） |
| 还含调用方的接口符号，其余情况 | **上传**成调用方的 Req（`depth + 1`，`path` 前面加上本调用点） |
| 含调用方局部且没有界 | **记账** |
| 到入口还没判完 | **记账**（`entry_unproven`；只有类型范围的记 `type_range_only`） |

5. **应用效果**：`ret_new` / `ret_int` / `ret_cond` / `field` 写进调用方在这个调用点之后的状态（LV4-01 的 `new(512)`、LV4-06 的条件效应、LV5-01 的 `ulUsed`）。

**报不报**：`max(depth, eff_depth) > InlineDepth`（默认 2）才报，≤ 2 层归现有规则（`06-` F-20）。16 个用例的判定全部是 3 层，都报。

**比 `06-` 评审稿更细的三处**（已同步进 `06-` v3.13）：
* **securec 的真实语义**【文档：libboundscheck 源码】：`memcpy_s` 在 `destMax == 0` 或 `destMax > SECUREC_MEM_MAX_LEN`（默认 `0x7fffffff`）时直接返回、不写；
  `count > destMax` 时先把 `dest` 的 `destMax` 字节清零再返回。所以**目的一侧写 `destMax` 字节**，**源一侧只有 `count <= destMax` 时才读**（源那条带守卫），
  **可能回绕的 `destMax`**（`128 − ulOff`）生成守卫 `ulOff <= 128`（LV2-03 TN）。
* **守卫界判定**：上表倒数第三行（`06-` F-12b）。
* **被覆盖的访问只留最强的**：同一对象、条件恒更弱的访问并进强的那条，记在 `absorbed` 里（LV1-02 的 `pszName[0]`、`pszName[1]`）（`06-` A-44）。

### B.3 16 个用例逐个看

每级至少一个；承诺、尽力、记账三种结局都有。每个用例给：要点、代码、**每个函数的摘要一览**（一行一个函数）、关键函数的**完整 JSON**、回放过程。
其余 51 个用例的摘要形式完全相同，由引擎在 WP4 起逐个产出、与用例头的"摘要"块对照后冻结成快照（`06-` T-06）。

| 用例 | 级别 | 结局 | 看什么 |
|---|---|---|---|
| LV1-01 | Lv1 | L1 | 对象在底层、下标从上面来：一条 Req 原样上传三层 |
| LV1-02 | Lv1 | L1 | 对象从上面来：`have = ext(p0)`；被覆盖的访问 `absorbed` |
| LV1-07 | Lv1 跨 TU | L1 | 三个文件；摘要与单文件相同，只多 `tu` |
| LV2-03 | Lv2 | L1 | 偏移在中间层加 4；`memcpy_s` 的 securec 语义与回绕守卫；入口记账 |
| LV2-05 | Lv2 | L2 | 强转不换对象；`div(ulBits, 32)` 不透明项（Infer 在 TN 上误报的地方） |
| LV2-09 | Lv2 | L1 | `wrap32(p1 − 1)`；守卫让 TN 的调用点走不到访问 |
| LV3-03 | Lv3 | L2 | 条件带守卫；入口形参被访问侧的校验框住 → 守卫界判定 |
| LV3-07 | Lv3 | L2 | 先化简、后代区间：安全写法里 `ulOff` 抵消；源指针记账 |
| LV4-01 | Lv4 | L2 | V 形：`ret_new` 往上三层、Req 往上三层；`eff_depth` |
| LV4-06 | Lv4 尽力 | L2 | 条件效应 `ret_cond` 沿返回值上传，在链顶变成守卫 |
| LV5-01 | Lv5 | L1 | 字段符号 `p0->ulBufLen`、`ext(p0->pucBuf)`、`fields` 表、`alias`、栈帧字段 |
| LV6-01 | Lv6 尽力 | L1 | `extern` 不完整数组：大小来自 solve 的全局大小表 |
| LV7-04 | Lv7 | 记账 | 指针步进 widening 到 ∞：不生成 Req，只记 `pointer_stepping` |
| LV8-01 | Lv8 尽力 | L2 | 数据驱动的步长：用校验给出的关系而不是区间 |
| LV9-04 | Lv9 | 记账 | 没有函数体的驱动接口：`unknown_callee`，补模型后变 L1 |
| LV10-04 | Lv10 | 记账 | 类型范围不算界：入口 `type_range_only`，留给 AI 研判 |

### B.4 自己跑

```bash
cd 08-summary2-design/cases/summaries
python3 replay.py                 # 16 个用例全部回放：每个调用点重做代换与判定，核对 JSON 与 @expect
python3 replay.py LV4-01 -v       # 打印一个用例的每一步
python3 replay.py --md ../../08-options-and-summary-examples.md   # 重新生成本文件的 B.5
```

改任何一份 JSON（比如把某条 Req 的 `end` 改掉），回放会指出是哪个调用点对不上——这也是 C++ 引擎接上以后的对照方法。

### B.5 逐用例

<!-- WALKTHROUGH -->

### LV1-01 统计计数：枚举结束标记 BUTT 被当成下标逐层传下去

最基本的形态：数组在最底层（大小当场已知），下标一路从形参传上来。摘要里只有一条 Req，`have` 是常数 16、末端是形参的线性式；它原样上传三层，到常量实参处变成 `20 <= 16` 不成立 → L1。两处 `LOG_Print` 和 `ulCellId` 从头到尾不出现在任何摘要里。

**代码**（`lv1/lv1_01_stat_enum_butt.c`；只保留函数体，头注释略）

```c
typedef enum {
    CELL_STAT_SETUP_SUCC,
    CELL_STAT_SETUP_FAIL,
    CELL_STAT_REL,
    CELL_STAT_HO,
    CELL_STAT_BUTT
} CELL_STAT_E;

static UINT32 g_aulCellStat[CELL_STAT_BUTT];

static void Stat_Inc(UINT32 ulType)
{
    g_aulCellStat[ulType]++;                                  /* ★1 */
}

static void Cell_UpdateStat(UINT32 ulType)
{
    LOG_Print(3, "update stat %u", ulType);
    Stat_Inc(ulType);
}

static void Cell_RecordFail(UINT32 ulCellId, UINT32 ulCause)
{
    LOG_Print(2, "cell %u setup fail", ulCellId);
    Cell_UpdateStat(ulCause);
}

void Cell_OnSetupTimeout_TP(UINT32 ulCellId)
{
    Cell_RecordFail(ulCellId, CELL_STAT_BUTT);
}

void Cell_OnSetupTimeout_TN(UINT32 ulCellId)
{
    Cell_RecordFail(ulCellId, CELL_STAT_SETUP_FAIL);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV1-01/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Stat_Inc` | **R1** W4 @ g_aulCellStat，末端 4*ulType + 4；需 4*ulType + 4 <= 16 | — | — |
| `Cell_UpdateStat` | **R1** W4 @ g_aulCellStat，末端 4*ulType + 4；需 4*ulType + 4 <= 16（depth 1） | — | — |
| `Cell_RecordFail` | **R1** W4 @ g_aulCellStat，末端 4*ulCause + 4；需 4*ulCause + 4 <= 16（depth 2） | — | — |
| `Cell_OnSetupTimeout_TN` | — | — | — |
| `Cell_OnSetupTimeout_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Stat_Inc.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Stat_Inc",
  "key": "F@lv1_01_stat_enum_butt.c:Stat_Inc",
  "loc": "lv1_01_stat_enum_butt.c:35",
  "params": ["ulType: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "g_aulCellStat",
      "have": "16",
      "end": ["4*p0 + 4", "4*p0 + 4"],
      "start": "4*p0",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Stat_Inc", "at": "lv1_01_stat_enum_butt.c:37", "code": "g_aulCellStat[ulType]++"},
      "path": ["Stat_Inc@lv1_01_stat_enum_butt.c:37"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "absorbed": ["R4 @ g_aulCellStat + 4*ulType（同一位置的读，条件相同）"],
      "text": "W4 @ g_aulCellStat，末端 4*ulType + 4；需 4*ulType + 4 <= 16"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Cell_OnSetupTimeout_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cell_OnSetupTimeout_TP",
  "key": "F:Cell_OnSetupTimeout_TP",
  "loc": "lv1_01_stat_enum_butt.c:52",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv1_01_stat_enum_butt.c:54",
      "callee": "Cell_RecordFail",
      "args": ["p0", "4"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}],
      "note": "CELL_STAT_BUTT = 4"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "lv1_01_stat_enum_butt.c:54",
      "access": "Stat_Inc@lv1_01_stat_enum_butt.c:37",
      "object": "g_aulCellStat",
      "have": "16",
      "end": ["20", "20"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Cell_OnSetupTimeout_TP@lv1_01_stat_enum_butt.c:54",
        "Cell_RecordFail@lv1_01_stat_enum_butt.c:49",
        "Cell_UpdateStat@lv1_01_stat_enum_butt.c:43",
        "Stat_Inc@lv1_01_stat_enum_butt.c:37"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 20 of 'g_aulCellStat' (16 bytes) is accessed",
      "fp": "0e286fe42ea23461"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV1-01 -v`）

```text
- Cell_UpdateStat @ lv1_01_stat_enum_butt.c:42  调用 LOG_Print(3, …, p0)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- Cell_UpdateStat @ lv1_01_stat_enum_butt.c:43  调用 Stat_Inc(p0)
    · Stat_Inc.R1：末端 4*p0 + 4，对象 16 → up → 本函数 R1
- Cell_RecordFail @ lv1_01_stat_enum_butt.c:48  调用 LOG_Print(2, …, p0)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- Cell_RecordFail @ lv1_01_stat_enum_butt.c:49  调用 Cell_UpdateStat(p1)
    · Cell_UpdateStat.R1：末端 4*p1 + 4，对象 16 → up → 本函数 R1
- Cell_OnSetupTimeout_TN @ lv1_01_stat_enum_butt.c:59  调用 Cell_RecordFail(p0, 1)
    · Cell_RecordFail.R1：末端 8，对象 16 → safe（最大末端 8 ≤ 16）
- Cell_OnSetupTimeout_TP @ lv1_01_stat_enum_butt.c:54  调用 Cell_RecordFail(p0, 4)
    · Cell_RecordFail.R1：末端 20，对象 16 → L1（最小末端 20 > 16，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Cell_OnSetupTimeout_TP pa=L1 → 回放 L1 ✓
- 期望 TN Cell_OnSetupTimeout_TN pa=- → 回放 - ✓
```

### LV1-02 名字缓冲区逐层下传，最底层按"长度"位置写结尾符

和 LV1-01 反过来：访问的偏移是常数，**对象大小是符号** `ext(p0)`，等链顶给出实参数组才知道。中间层自己也写了 `pszName[0]`、`pszName[1]`，它们被更强的条件覆盖，记在 `absorbed` 里不单列。

**代码**（`lv1/lv1_02_name_terminator.c`；只保留函数体，头注释略）

```c
#define UE_NAME_LEN 16

static void Name_Terminate(CHAR *pszName)
{
    pszName[UE_NAME_LEN] = '\0';                              /* ★1 */
}

static void Name_Format(CHAR *pszName, UINT32 ulUeId)
{
    pszName[0] = 'U';
    pszName[1] = (CHAR)('0' + (ulUeId % 10));
    Name_Terminate(pszName);
}

static void Ue_BuildName(CHAR *pszName, UINT32 ulUeId)
{
    Name_Format(pszName, ulUeId);
}

void Ue_ShowInfo_TP(UINT32 ulUeId)
{
    CHAR szName[UE_NAME_LEN];
    Ue_BuildName(szName, ulUeId);
    LOG_Print(3, "ue %s", szName);
}

void Ue_ShowInfo_TN(UINT32 ulUeId)
{
    CHAR szName[UE_NAME_LEN + 1];
    Ue_BuildName(szName, ulUeId);
    LOG_Print(3, "ue %s", szName);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV1-02/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Name_Terminate` | **R1** W1 @ *pszName，末端 17；需 17 <= ext(pszName) | — | — |
| `Name_Format` | **R1** W1 @ *pszName，末端 17；需 17 <= ext(pszName)（depth 1） | — | — |
| `Ue_BuildName` | **R1** W1 @ *pszName，末端 17；需 17 <= ext(pszName)（depth 2） | — | — |
| `Ue_ShowInfo_TN` | — | — | — |
| `Ue_ShowInfo_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Name_Terminate.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Name_Terminate",
  "key": "F@lv1_02_name_terminator.c:Name_Terminate",
  "loc": "lv1_02_name_terminator.c:27",
  "params": ["pszName: CHAR*"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "1",
      "obj": "*pszName",
      "have": "ext(p0)",
      "end": ["17", "17"],
      "start": "16",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Name_Terminate", "at": "lv1_02_name_terminator.c:29", "code": "pszName[UE_NAME_LEN] = '\\0'"},
      "path": ["Name_Terminate@lv1_02_name_terminator.c:29"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "text": "W1 @ *pszName，末端 17；需 17 <= ext(pszName)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Name_Format.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Name_Format",
  "key": "F@lv1_02_name_terminator.c:Name_Format",
  "loc": "lv1_02_name_terminator.c:32",
  "params": ["pszName: CHAR*", "ulUeId: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "1",
      "obj": "*pszName",
      "have": "ext(p0)",
      "end": ["17", "17"],
      "start": "16",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Name_Terminate", "at": "lv1_02_name_terminator.c:29", "code": "pszName[UE_NAME_LEN] = '\\0'"},
      "path": ["Name_Format@lv1_02_name_terminator.c:36", "Name_Terminate@lv1_02_name_terminator.c:29"],
      "depth": 1,
      "eff_depth": 0,
      "blind": false,
      "absorbed": [
        "W1 @ pszName + 0（lv1_02_name_terminator.c:34，末端 1）",
        "W1 @ pszName + 1（lv1_02_name_terminator.c:35，末端 2）"
      ],
      "text": "W1 @ *pszName，末端 17；需 17 <= ext(pszName)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv1_02_name_terminator.c:36",
      "callee": "Name_Terminate",
      "args": [{"ptr": "pszName", "have": "ext(p0)", "alias": "p0"}],
      "results": [{"req": "R1", "outcome": "up", "as": "R1"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_ShowInfo_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_ShowInfo_TP",
  "key": "F:Ue_ShowInfo_TP",
  "loc": "lv1_02_name_terminator.c:44",
  "params": ["ulUeId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv1_02_name_terminator.c:47",
      "callee": "Ue_BuildName",
      "args": [{"ptr": "szName", "have": "16"}, "p0"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}],
      "note": "CHAR szName[UE_NAME_LEN]，16 字节"
    },
    {
      "site": "lv1_02_name_terminator.c:48",
      "callee": "LOG_Print",
      "model": "pure",
      "args": ["3", null, null],
      "results": [{"note": "LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件"}]
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "lv1_02_name_terminator.c:47",
      "access": "Name_Terminate@lv1_02_name_terminator.c:29",
      "object": "szName",
      "have": "16",
      "end": ["17", "17"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Ue_ShowInfo_TP@lv1_02_name_terminator.c:47",
        "Ue_BuildName@lv1_02_name_terminator.c:41",
        "Name_Format@lv1_02_name_terminator.c:36",
        "Name_Terminate@lv1_02_name_terminator.c:29"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 17 of 'szName' (16 bytes) is accessed",
      "fp": "da30a4888779f6eb"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV1-02 -v`）

```text
- Name_Format @ lv1_02_name_terminator.c:36  调用 Name_Terminate(pszName)
    · Name_Terminate.R1：末端 17，对象 ext(p0) → up → 本函数 R1
- Ue_BuildName @ lv1_02_name_terminator.c:41  调用 Name_Format(pszName, p1)
    · Name_Format.R1：末端 17，对象 ext(p0) → up → 本函数 R1
- Ue_ShowInfo_TN @ lv1_02_name_terminator.c:54  调用 Ue_BuildName(szName, p0)
    · Ue_BuildName.R1：末端 17，对象 17 → safe（最大末端 17 ≤ 17）
- Ue_ShowInfo_TN @ lv1_02_name_terminator.c:55  调用 LOG_Print(3, …, …)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- Ue_ShowInfo_TP @ lv1_02_name_terminator.c:47  调用 Ue_BuildName(szName, p0)
    · Ue_BuildName.R1：末端 17，对象 16 → L1（最小末端 17 > 16，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
- Ue_ShowInfo_TP @ lv1_02_name_terminator.c:48  调用 LOG_Print(3, …, …)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- 期望 TP Ue_ShowInfo_TP pa=L1 → 回放 L1 ✓
- 期望 TN Ue_ShowInfo_TN pa=- → 回放 - ✓
```

### LV1-07 跨文件：告警 ID 用了 BUTT，经三个文件三层到达标志数组

与 LV1-01 同形，但三层分在三个 `.c` 里。方案 B 下三个 TU 各自 `export` 出 FIR，`solve` 时全局调用图把它们接起来，摘要与单文件时**完全相同**，只多了 `tu` 字段；告警报在 `alm_top.c` 的调用点，trace 写进消息（D12）。

**代码**（`lv1/lv1_07_cross_tu/alm_top.c`；只保留函数体，头注释略）

```c
/* alm_low.c */
static UINT8 g_aucAlmFlag[ALM_ID_BUTT];

void Alm_SetFlag(UINT32 ulAlmId)
{
    g_aucAlmFlag[ulAlmId] = 1;                                /* ★1 */
}

void Alm_RaiseOne(UINT32 ulAlmId)
{
    Alm_SetFlag(ulAlmId);
}

/* alm_mid.c */
void Alm_Dispatch(UINT32 ulAlmId, UINT32 ulCellId)
{
    LOG_Print(2, "alarm %u on cell %u", ulAlmId, ulCellId);
    Alm_RaiseOne(ulAlmId);
}

/* alm_top.c */
void Alm_ReportAll_TP(UINT32 ulCellId) { Alm_Dispatch(ALM_ID_BUTT, ulCellId); }
void Alm_ReportAll_TN(UINT32 ulCellId) { Alm_Dispatch(ALM_ID_CLK_LOSS, ulCellId); }
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV1-07/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Alm_SetFlag` | **R1** W1 @ g_aucAlmFlag，末端 ulAlmId + 1；需 ulAlmId + 1 <= 3 | — | — |
| `Alm_RaiseOne` | **R1** W1 @ g_aucAlmFlag，末端 ulAlmId + 1；需 ulAlmId + 1 <= 3（depth 1） | — | — |
| `Alm_Dispatch` | **R1** W1 @ g_aucAlmFlag，末端 ulAlmId + 1；需 ulAlmId + 1 <= 3（depth 2） | — | — |
| `Alm_ReportAll_TN` | — | — | — |
| `Alm_ReportAll_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Alm_SetFlag.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Alm_SetFlag",
  "key": "F:Alm_SetFlag",
  "tu": "alm_low.c",
  "loc": "alm_low.c:5",
  "params": ["ulAlmId: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "1",
      "obj": "g_aucAlmFlag",
      "have": "3",
      "end": ["p0 + 1", "p0 + 1"],
      "start": "p0",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Alm_SetFlag", "at": "alm_low.c:7", "code": "g_aucAlmFlag[ulAlmId] = 1"},
      "path": ["Alm_SetFlag@alm_low.c:7"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "text": "W1 @ g_aucAlmFlag，末端 ulAlmId + 1；需 ulAlmId + 1 <= 3"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Alm_ReportAll_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Alm_ReportAll_TP",
  "key": "F:Alm_ReportAll_TP",
  "tu": "alm_top.c",
  "loc": "alm_top.c:22",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "alm_top.c:22",
      "callee": "Alm_Dispatch",
      "args": ["3", "p0"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}],
      "note": "ALM_ID_BUTT = 3"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "alm_top.c:22",
      "access": "Alm_SetFlag@alm_low.c:7",
      "object": "g_aucAlmFlag",
      "have": "3",
      "end": ["4", "4"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Alm_ReportAll_TP@alm_top.c:22",
        "Alm_Dispatch@alm_mid.c:6",
        "Alm_RaiseOne@alm_low.c:12",
        "Alm_SetFlag@alm_low.c:7"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 4 of 'g_aucAlmFlag' (3 bytes) is accessed [Alm_ReportAll_TP (alm_top.c:22) -> Alm_Dispatch (alm_mid.c:6) -> Alm_RaiseOne (alm_low.c:12) -> Alm_SetFlag (alm_low.c:7)]",
      "fp": "2ae2bab0c04cadb5"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV1-07 -v`）

```text
- Alm_RaiseOne @ alm_low.c:12  调用 Alm_SetFlag(p0)
    · Alm_SetFlag.R1：末端 p0 + 1，对象 3 → up → 本函数 R1
- Alm_Dispatch @ alm_mid.c:5  调用 LOG_Print(2, …, p0, p1)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- Alm_Dispatch @ alm_mid.c:6  调用 Alm_RaiseOne(p0)
    · Alm_RaiseOne.R1：末端 p0 + 1，对象 3 → up → 本函数 R1
- Alm_ReportAll_TN @ alm_top.c:23  调用 Alm_Dispatch(2, p0)
    · Alm_Dispatch.R1：末端 3，对象 3 → safe（最大末端 3 ≤ 3）
- Alm_ReportAll_TP @ alm_top.c:22  调用 Alm_Dispatch(3, p0)
    · Alm_Dispatch.R1：末端 4，对象 3 → L1（最小末端 4 > 3，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Alm_ReportAll_TP pa=L1 → 回放 L1 ✓
- 期望 TN Alm_ReportAll_TN pa=- → 回放 - ✓
```

### LV2-03 memcpy_s(pkt + off, 整包大小, …)：destMax 没扣偏移，偏移在中间层又加了头长

安全函数的条件按 securec 的真实语义生成：**写 destMax 字节**（`count > destMax` 时也会先把 destMax 字节清零），**读 count 字节只在 `count <= destMax` 时发生**（所以源那条带守卫）。偏移在中间层被加了 4，`ext(p + k) = ext(p) − k` 让它表现为末端 +4。TN 一侧的 `destMax = 128 − ulOff` 是可能回绕的无符号减法：securec 对 `destMax > SECUREC_MEM_MAX_LEN`（0x7fffffff）直接返回、不写，所以生成守卫 `ulOff <= 128`，在守卫下 `ulOff` 正好抵消。源缓冲区那条一路传到入口，变成入口的账本记录。

**代码**（`lv2/lv2_03_tlv_destmax_no_off.c`；只保留函数体，头注释略）

```c
#define PKT_MAX_LEN 128
#define TLV_HDR_LEN 4

static void Tlv_WriteVal(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    (void)memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen);            /* ★1 */
}

static void Tlv_WriteVal_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    (void)memcpy_s(pucPkt + ulOff, PKT_MAX_LEN - ulOff, pucVal, ulValLen);
}

static void Tlv_Append(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    pucPkt[ulOff] = 0x10;                       /* type */
    Tlv_WriteVal(pucPkt, ulOff + TLV_HDR_LEN, pucVal, ulValLen);
}

static void Tlv_Append_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    pucPkt[ulOff] = 0x10;
    Tlv_WriteVal_OK(pucPkt, ulOff + TLV_HDR_LEN, pucVal, ulValLen);
}

static void Pkt_AddIe(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)    { Tlv_Append(pucPkt, ulOff, pucVal, ulValLen); }
static void Pkt_AddIe_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen) { Tlv_Append_OK(pucPkt, ulOff, pucVal, ulValLen); }

void Pkt_Build_TP(const UINT8 *pucVal)
{
    UINT8 aucPkt[PKT_MAX_LEN];
    Pkt_AddIe(aucPkt, 16, pucVal, 8);
}

void Pkt_Build_TN(const UINT8 *pucVal)
{
    UINT8 aucPkt[PKT_MAX_LEN];
    Pkt_AddIe_OK(aucPkt, 16, pucVal, 8);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV2-03/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Tlv_WriteVal` | **R1** W128 @ *pucPkt，末端 ulOff + 128；需 ulOff + 128 <= ext(pucPkt)<br>**R2** ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal) | — | — |
| `Tlv_WriteVal_OK` | **R1** ulOff <= 128 ⇒ W128 - ulOff @ *pucPkt，末端 128；需 128 <= ext(pucPkt)<br>**R2** ulOff + ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal) | — | — |
| `Tlv_Append` | **R1** W128 @ *pucPkt，末端 ulOff + 132；需 ulOff + 132 <= ext(pucPkt)（depth 1）<br>**R2** ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)（depth 1） | — | — |
| `Tlv_Append_OK` | **R1** ulOff <= 124 ⇒ W124 - ulOff @ *pucPkt，末端 128；需 128 <= ext(pucPkt)（depth 1）<br>**R2** ulOff + ulValLen <= 124 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)（depth 1）<br>**R3** W1 @ *pucPkt，末端 ulOff + 1；需 ulOff + 1 <= ext(pucPkt) | — | — |
| `Pkt_AddIe` | **R1** W128 @ *pucPkt，末端 ulOff + 132；需 ulOff + 132 <= ext(pucPkt)（depth 2）<br>**R2** ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)（depth 2） | — | — |
| `Pkt_AddIe_OK` | **R1** ulOff <= 124 ⇒ W124 - ulOff @ *pucPkt，末端 128；需 128 <= ext(pucPkt)（depth 2）<br>**R2** ulOff + ulValLen <= 124 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)（depth 2）<br>**R3** W1 @ *pucPkt，末端 ulOff + 1；需 ulOff + 1 <= ext(pucPkt)（depth 1） | — | — |
| `Pkt_Build_TN` | **R1** R8 @ *pucVal，末端 8；需 8 <= ext(pucVal)（depth 3） | — | 账本 `entry_unproven`（R1） |
| `Pkt_Build_TP` | **R1** R8 @ *pucVal，末端 8；需 8 <= ext(pucVal)（depth 3） | — | **RPT1 L1**（depth 3）<br>账本 `entry_unproven`（R1） |

<details open><summary><code>Tlv_WriteVal.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Tlv_WriteVal",
  "key": "F@lv2_03_tlv_destmax_no_off.c:Tlv_WriteVal",
  "loc": "lv2_03_tlv_destmax_no_off.c:29",
  "params": ["pucPkt: UINT8*", "ulOff: UINT32", "pucVal: UINT8*", "ulValLen: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "128",
      "obj": "*pucPkt",
      "have": "ext(p0)",
      "end": ["p1 + 128", "p1 + 128"],
      "start": "p1",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Tlv_WriteVal",
        "at": "lv2_03_tlv_destmax_no_off.c:31",
        "code": "memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen)"
      },
      "path": ["Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "memcpy_s 的目的：写 destMax = 128 字节",
      "text": "W128 @ *pucPkt，末端 ulOff + 128；需 ulOff + 128 <= ext(pucPkt)"
    },
    {
      "id": "R2",
      "kind": "R",
      "bytes": "p3",
      "obj": "*pucVal",
      "have": "ext(p2)",
      "end": ["p3", "p3"],
      "start": "0",
      "bound": "exact",
      "guard": ["p3 <= 128"],
      "access": {
        "fn": "Tlv_WriteVal",
        "at": "lv2_03_tlv_destmax_no_off.c:31",
        "code": "memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen)"
      },
      "path": ["Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "memcpy_s 的源：只有 count <= destMax 时才读",
      "text": "ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv2_03_tlv_destmax_no_off.c:31",
      "callee": "memcpy_s",
      "model": "access",
      "args": [{"ptr": "pucPkt + ulOff", "have": "ext(p0) - p1"}, "128", {"ptr": "pucVal", "have": "ext(p2)"}, "p3"],
      "results": [{"note": "访问类模型 memcpy_s → 生成本函数的 R1（目的）与 R2（源）"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Tlv_Append.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Tlv_Append",
  "key": "F@lv2_03_tlv_destmax_no_off.c:Tlv_Append",
  "loc": "lv2_03_tlv_destmax_no_off.c:39",
  "params": ["pucPkt: UINT8*", "ulOff: UINT32", "pucVal: UINT8*", "ulValLen: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "128",
      "obj": "*pucPkt",
      "have": "ext(p0)",
      "end": ["p1 + 132", "p1 + 132"],
      "start": "p1 + 4",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Tlv_WriteVal",
        "at": "lv2_03_tlv_destmax_no_off.c:31",
        "code": "memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen)"
      },
      "path": ["Tlv_Append@lv2_03_tlv_destmax_no_off.c:42", "Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"],
      "depth": 1,
      "eff_depth": 0,
      "blind": false,
      "absorbed": ["W1 @ pucPkt + ulOff（lv2_03_tlv_destmax_no_off.c:41，末端 ulOff + 1，恒弱于本条）"],
      "text": "W128 @ *pucPkt，末端 ulOff + 132；需 ulOff + 132 <= ext(pucPkt)"
    },
    {
      "id": "R2",
      "kind": "R",
      "bytes": "p3",
      "obj": "*pucVal",
      "have": "ext(p2)",
      "end": ["p3", "p3"],
      "start": "0",
      "bound": "exact",
      "guard": ["p3 <= 128"],
      "access": {
        "fn": "Tlv_WriteVal",
        "at": "lv2_03_tlv_destmax_no_off.c:31",
        "code": "memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen)"
      },
      "path": ["Tlv_Append@lv2_03_tlv_destmax_no_off.c:42", "Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"],
      "depth": 1,
      "eff_depth": 0,
      "blind": false,
      "text": "ulValLen <= 128 ⇒ RulValLen @ *pucVal，末端 ulValLen；需 ulValLen <= ext(pucVal)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv2_03_tlv_destmax_no_off.c:42",
      "callee": "Tlv_WriteVal",
      "args": [
        {"ptr": "pucPkt", "have": "ext(p0)", "alias": "p0"},
        "p1 + 4",
        {"ptr": "pucVal", "have": "ext(p2)", "alias": "p2"},
        "p3"
      ],
      "results": [{"req": "R1", "outcome": "up", "as": "R1"}, {"req": "R2", "outcome": "up", "as": "R2"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Pkt_Build_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Pkt_Build_TP",
  "key": "F:Pkt_Build_TP",
  "loc": "lv2_03_tlv_destmax_no_off.c:54",
  "params": ["pucVal: UINT8*"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "R",
      "bytes": "8",
      "obj": "*pucVal",
      "have": "ext(p0)",
      "end": ["8", "8"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Tlv_WriteVal",
        "at": "lv2_03_tlv_destmax_no_off.c:31",
        "code": "memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen)"
      },
      "path": [
        "Pkt_Build_TP@lv2_03_tlv_destmax_no_off.c:57",
        "Pkt_AddIe@lv2_03_tlv_destmax_no_off.c:51",
        "Tlv_Append@lv2_03_tlv_destmax_no_off.c:42",
        "Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"
      ],
      "depth": 3,
      "eff_depth": 0,
      "blind": false,
      "text": "R8 @ *pucVal，末端 8；需 8 <= ext(pucVal)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv2_03_tlv_destmax_no_off.c:57",
      "callee": "Pkt_AddIe",
      "args": [{"ptr": "aucPkt", "have": "128"}, "16", {"ptr": "pucVal", "have": "ext(p0)", "alias": "p0"}, "8"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}, {"req": "R2", "outcome": "up", "as": "R1"}],
      "note": "UINT8 aucPkt[PKT_MAX_LEN]，128 字节"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "lv2_03_tlv_destmax_no_off.c:57",
      "access": "Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31",
      "object": "aucPkt",
      "have": "128",
      "end": ["148", "148"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Pkt_Build_TP@lv2_03_tlv_destmax_no_off.c:57",
        "Pkt_AddIe@lv2_03_tlv_destmax_no_off.c:51",
        "Tlv_Append@lv2_03_tlv_destmax_no_off.c:42",
        "Tlv_WriteVal@lv2_03_tlv_destmax_no_off.c:31"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 148 of 'aucPkt' (128 bytes) is accessed",
      "fp": "dc4edf4e255065c9"
    }
  ],
  "opens": [
    {
      "reason": "entry_unproven",
      "at": "lv2_03_tlv_destmax_no_off.c:54",
      "detail": "入口形参 pucVal 指向的对象要求至少 8 字节；没有调用方能证明",
      "req": "R1"
    }
  ]
}
```
</details>

**回放**（`python3 replay.py LV2-03 -v`）

```text
- Tlv_WriteVal @ lv2_03_tlv_destmax_no_off.c:31  调用 memcpy_s(pucPkt + ulOff, 128, pucVal, p3)
    · 访问类模型 memcpy_s → 生成本函数的 R1（目的）与 R2（源）
- Tlv_WriteVal_OK @ lv2_03_tlv_destmax_no_off.c:36  调用 memcpy_s(pucPkt + ulOff, 128 - ulOff, pucVal, p3)
    · 访问类模型 memcpy_s → R1、R2
- Tlv_Append @ lv2_03_tlv_destmax_no_off.c:42  调用 Tlv_WriteVal(pucPkt, p1 + 4, pucVal, p3)
    · Tlv_WriteVal.R1：末端 p1 + 132，对象 ext(p0) → up → 本函数 R1
    · Tlv_WriteVal.R2：末端 p3，守卫 p3 <= 128，对象 ext(p2) → up → 本函数 R2
- Tlv_Append_OK @ lv2_03_tlv_destmax_no_off.c:48  调用 Tlv_WriteVal_OK(pucPkt, p1 + 4, pucVal, p3)
    · Tlv_WriteVal_OK.R1：末端 128，守卫 p1 <= 124，对象 ext(p0) → up → 本函数 R1
    · Tlv_WriteVal_OK.R2：末端 p3，守卫 p1 + p3 <= 124，对象 ext(p2) → up → 本函数 R2
- Pkt_AddIe @ lv2_03_tlv_destmax_no_off.c:51  调用 Tlv_Append(pucPkt, p1, pucVal, p3)
    · Tlv_Append.R1：末端 p1 + 132，对象 ext(p0) → up → 本函数 R1
    · Tlv_Append.R2：末端 p3，守卫 p3 <= 128，对象 ext(p2) → up → 本函数 R2
- Pkt_AddIe_OK @ lv2_03_tlv_destmax_no_off.c:52  调用 Tlv_Append_OK(pucPkt, p1, pucVal, p3)
    · Tlv_Append_OK.R1：末端 128，守卫 p1 <= 124，对象 ext(p0) → up → 本函数 R1
    · Tlv_Append_OK.R2：末端 p3，守卫 p1 + p3 <= 124，对象 ext(p2) → up → 本函数 R2
    · Tlv_Append_OK.R3：末端 p1 + 1，对象 ext(p0) → up → 本函数 R3
- Pkt_Build_TN @ lv2_03_tlv_destmax_no_off.c:63  调用 Pkt_AddIe_OK(aucPkt, 16, pucVal, 8)
    · Pkt_AddIe_OK.R1：末端 128，守卫在此成立，对象 128 → safe（最大末端 128 ≤ 128）
    · Pkt_AddIe_OK.R2：末端 8，守卫在此成立，对象 ext(p0) → up → 本函数 R1
    · Pkt_AddIe_OK.R3：末端 17，对象 128 → safe（最大末端 17 ≤ 128）
- Pkt_Build_TN 是入口（没有调用方）：R1 到此为止 → 记账 entry_unproven
- Pkt_Build_TP @ lv2_03_tlv_destmax_no_off.c:57  调用 Pkt_AddIe(aucPkt, 16, pucVal, 8)
    · Pkt_AddIe.R1：末端 148，对象 128 → L1（最小末端 148 > 128，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
    · Pkt_AddIe.R2：末端 8，守卫在此成立，对象 ext(p0) → up → 本函数 R1
- Pkt_Build_TP 是入口（没有调用方）：R1 到此为止 → 记账 entry_unproven
- 期望 TP Pkt_Build_TP pa=L1 → 回放 L1 ✓
- 期望 TN Pkt_Build_TN pa=- → 回放 - ✓
```

### LV2-05 字节型位图按 UINT32 清零，字数算多了一个

两个要点：① 强转 `(UINT32 *)pucMap` 不换对象，条件仍写在 `ext(pucMap)` 上，元素大小 4 已经折进字节偏移；② `ulBits / 32` 保留成不透明项 `div(ulBits, 32)`，代入常量 64 后变成 2 再判。Infer 在这里把除法丢成区间，TN 也误报了【实测数据】。末端是区间 `[4, 4*div(ulBits,32)+4]`，来源是循环界，所以只会是 L2。

**代码**（`lv2/lv2_05_bitmap_cast_words.c`；只保留函数体，头注释略）

```c
#define MAX_UE_NUM 64

static void Map_ClearWords(UINT8 *pucMap, UINT32 ulBits)
{
    UINT32 *pulMap = (UINT32 *)(void *)pucMap;
    UINT32 i;
    for (i = 0; i <= ulBits / 32; i++) {
        pulMap[i] = 0;                                        /* ★1 */
    }
}

static void Map_ClearWords_OK(UINT8 *pucMap, UINT32 ulBits)
{
    UINT32 *pulMap = (UINT32 *)(void *)pucMap;
    UINT32 i;
    for (i = 0; i < (ulBits + 31) / 32; i++) {
        pulMap[i] = 0;
    }
}

static void Ue_ResetMap(UINT8 *pucMap, UINT32 ulBits)       { Map_ClearWords(pucMap, ulBits); }
static void Ue_ResetMap_OK(UINT8 *pucMap, UINT32 ulBits)    { Map_ClearWords_OK(pucMap, ulBits); }
static void Cell_ClearUeMap(UINT8 *pucMap, UINT32 ulBits)    { Ue_ResetMap(pucMap, ulBits); }
static void Cell_ClearUeMap_OK(UINT8 *pucMap, UINT32 ulBits) { Ue_ResetMap_OK(pucMap, ulBits); }

void Cell_ResetUeMap_TP(void)
{
    UINT32 aulAlign[MAX_UE_NUM / 32];             /* 保证 4 字节对齐的 8 字节缓冲 */
    Cell_ClearUeMap((UINT8 *)aulAlign, MAX_UE_NUM);
}

void Cell_ResetUeMap_TN(void)
{
    UINT32 aulAlign[MAX_UE_NUM / 32];
    Cell_ClearUeMap_OK((UINT8 *)aulAlign, MAX_UE_NUM);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV2-05/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Map_ClearWords` | **R1** W4 @ *pucMap，末端 [4, 4*div(ulBits, 32) + 4]；需 4*div(ulBits, 32) + 4 <= ext(pucMap) | — | — |
| `Map_ClearWords_OK` | **R1** div(ulBits + 31, 32) >= 1 ⇒ W4 @ *pucMap，末端 [4, 4*div(ulBits + 31, 32)]；需 4*div(ulBits + 31, 32) <= ext(pucMap) | — | — |
| `Ue_ResetMap` | **R1** W4 @ *pucMap，末端 [4, 4*div(ulBits, 32) + 4]；需 4*div(ulBits, 32) + 4 <= ext(pucMap)（depth 1） | — | — |
| `Ue_ResetMap_OK` | **R1** div(ulBits + 31, 32) >= 1 ⇒ W4 @ *pucMap，末端 [4, 4*div(ulBits + 31, 32)]；需 4*div(ulBits + 31, 32) <= ext(pucMap)（depth 1） | — | — |
| `Cell_ClearUeMap` | **R1** W4 @ *pucMap，末端 [4, 4*div(ulBits, 32) + 4]；需 4*div(ulBits, 32) + 4 <= ext(pucMap)（depth 2） | — | — |
| `Cell_ClearUeMap_OK` | **R1** div(ulBits + 31, 32) >= 1 ⇒ W4 @ *pucMap，末端 [4, 4*div(ulBits + 31, 32)]；需 4*div(ulBits + 31, 32) <= ext(pucMap)（depth 2） | — | — |
| `Cell_ResetUeMap_TN` | — | — | — |
| `Cell_ResetUeMap_TP` | — | — | **RPT1 L2**（depth 3） |

<details open><summary><code>Map_ClearWords.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Map_ClearWords",
  "key": "F@lv2_05_bitmap_cast_words.c:Map_ClearWords",
  "loc": "lv2_05_bitmap_cast_words.c:28",
  "params": ["pucMap: UINT8*", "ulBits: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "*pucMap",
      "have": "ext(p0)",
      "end": ["4", "4*div(p1, 32) + 4"],
      "start": "0",
      "bound": "loop",
      "guard": [],
      "access": {"fn": "Map_ClearWords", "at": "lv2_05_bitmap_cast_words.c:33", "code": "pulMap[i] = 0"},
      "path": ["Map_ClearWords@lv2_05_bitmap_cast_words.c:33"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "i ∈ [0, ulBits / 32]（循环 i <= ulBits / 32）；pulMap 与 pucMap 同一基址",
      "text": "W4 @ *pucMap，末端 [4, 4*div(ulBits, 32) + 4]；需 4*div(ulBits, 32) + 4 <= ext(pucMap)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Map_ClearWords_OK.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Map_ClearWords_OK",
  "key": "F@lv2_05_bitmap_cast_words.c:Map_ClearWords_OK",
  "loc": "lv2_05_bitmap_cast_words.c:37",
  "params": ["pucMap: UINT8*", "ulBits: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "*pucMap",
      "have": "ext(p0)",
      "end": ["4", "4*div(p1 + 31, 32)"],
      "start": "0",
      "bound": "loop",
      "guard": ["div(p1 + 31, 32) >= 1"],
      "access": {"fn": "Map_ClearWords_OK", "at": "lv2_05_bitmap_cast_words.c:42", "code": "pulMap[i] = 0"},
      "path": ["Map_ClearWords_OK@lv2_05_bitmap_cast_words.c:42"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "i ∈ [0, (ulBits + 31) / 32 − 1]；循环体要执行，至少一次 → 守卫",
      "text": "div(ulBits + 31, 32) >= 1 ⇒ W4 @ *pucMap，末端 [4, 4*div(ulBits + 31, 32)]；需 4*div(ulBits + 31, 32) <= ext(pucMap)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Cell_ResetUeMap_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cell_ResetUeMap_TP",
  "key": "F:Cell_ResetUeMap_TP",
  "loc": "lv2_05_bitmap_cast_words.c:51",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv2_05_bitmap_cast_words.c:54",
      "callee": "Cell_ClearUeMap",
      "args": [{"ptr": "(UINT8 *)aulAlign", "have": "8"}, "64"],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}],
      "note": "UINT32 aulAlign[64 / 32]，8 字节"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv2_05_bitmap_cast_words.c:54",
      "access": "Map_ClearWords@lv2_05_bitmap_cast_words.c:33",
      "object": "aulAlign",
      "have": "8",
      "end": ["4", "12"],
      "bound": "loop",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Cell_ResetUeMap_TP@lv2_05_bitmap_cast_words.c:54",
        "Cell_ClearUeMap@lv2_05_bitmap_cast_words.c:48",
        "Ue_ResetMap@lv2_05_bitmap_cast_words.c:46",
        "Map_ClearWords@lv2_05_bitmap_cast_words.c:33"
      ],
      "msg": "Possible out-of-bound write through a 3-level call chain: up to byte 12 of 'aulAlign' (8 bytes) may be accessed; the bound comes from a loop bound in Map_ClearWords",
      "fp": "8fc2c2ebe1bd96e9"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV2-05 -v`）

```text
- Ue_ResetMap @ lv2_05_bitmap_cast_words.c:46  调用 Map_ClearWords(pucMap, p1)
    · Map_ClearWords.R1：末端 [4, 4*div(p1, 32) + 4]，对象 ext(p0) → up → 本函数 R1
- Ue_ResetMap_OK @ lv2_05_bitmap_cast_words.c:47  调用 Map_ClearWords_OK(pucMap, p1)
    · Map_ClearWords_OK.R1：末端 [4, 4*div(p1 + 31, 32)]，守卫 div(p1 + 31, 32) >= 1，对象 ext(p0) → up → 本函数 R1
- Cell_ClearUeMap @ lv2_05_bitmap_cast_words.c:48  调用 Ue_ResetMap(pucMap, p1)
    · Ue_ResetMap.R1：末端 [4, 4*div(p1, 32) + 4]，对象 ext(p0) → up → 本函数 R1
- Cell_ClearUeMap_OK @ lv2_05_bitmap_cast_words.c:49  调用 Ue_ResetMap_OK(pucMap, p1)
    · Ue_ResetMap_OK.R1：末端 [4, 4*div(p1 + 31, 32)]，守卫 div(p1 + 31, 32) >= 1，对象 ext(p0) → up → 本函数 R1
- Cell_ResetUeMap_TN @ lv2_05_bitmap_cast_words.c:60  调用 Cell_ClearUeMap_OK((UINT8 *)aulAlign, 64)
    · Cell_ClearUeMap_OK.R1：末端 [4, 8]，守卫在此成立，对象 8 → safe（最大末端 8 ≤ 8）
- Cell_ResetUeMap_TP @ lv2_05_bitmap_cast_words.c:54  调用 Cell_ClearUeMap((UINT8 *)aulAlign, 64)
    · Cell_ClearUeMap.R1：末端 [4, 12]，对象 8 → L2（最大末端 12 > 8（上界来自 loop），最小末端 4 不越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Cell_ResetUeMap_TP pa=L2 → 回放 L2 ✓
- 期望 TN Cell_ResetUeMap_TN pa=- → 回放 - ✓
```

### LV2-09 无符号下标减一回绕：pos 为 0 时取"前一个"

无符号减法 `ulPos − 1` 记成 `wrap32(p1 − 1)`：代入 0 得 4294967295，确定越界。Safe 版本先判 `ulPos == 0` 就返回，于是有守卫 `ulPos >= 1`，守卫下减法不会回绕，偏移就是普通的 `ulPos − 1`；TN 的调用点传 0，守卫为假 → 这次调用根本走不到访问。

**代码**（`lv2/lv2_09_unsigned_wrap.c`；只保留函数体，头注释略）

```c
#define HIST_LEN 16

static UINT8 Hist_GetPrev(const UINT8 *pucHist, UINT32 ulPos)
{
    return pucHist[ulPos - 1];                                /* ★1 */
}

static UINT8 Hist_GetPrevSafe(const UINT8 *pucHist, UINT32 ulPos)
{
    if (ulPos == 0) {
        return 0;
    }
    return pucHist[ulPos - 1];
}

static UINT32 Ue_CalcJitter(const UINT8 *pucHist, UINT32 ulPos)      { return (UINT32)Hist_GetPrev(pucHist, ulPos); }
static UINT32 Ue_CalcJitter_OK(const UINT8 *pucHist, UINT32 ulPos)   { return (UINT32)Hist_GetPrevSafe(pucHist, ulPos); }
static UINT32 Ue_UpdateJitter(const UINT8 *pucHist, UINT32 ulPos)    { return Ue_CalcJitter(pucHist, ulPos); }
static UINT32 Ue_UpdateJitter_OK(const UINT8 *pucHist, UINT32 ulPos) { return Ue_CalcJitter_OK(pucHist, ulPos); }

UINT32 Ue_CheckJitter_TP(void)
{
    UINT8 aucHist[HIST_LEN] = { 0 };
    return Ue_UpdateJitter(aucHist, 0);
}

UINT32 Ue_CheckJitter_TN(void)
{
    UINT8 aucHist[HIST_LEN] = { 0 };
    return Ue_UpdateJitter_OK(aucHist, 0);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV2-09/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Hist_GetPrev` | **R1** R1 @ *pucHist，末端 wrap32(ulPos - 1) + 1；需 wrap32(ulPos - 1) + 1 <= ext(pucHist) | — | — |
| `Hist_GetPrevSafe` | **R1** ulPos >= 1 ⇒ R1 @ *pucHist，末端 ulPos；需 ulPos <= ext(pucHist) | — | — |
| `Ue_CalcJitter` | **R1** R1 @ *pucHist，末端 wrap32(ulPos - 1) + 1；需 wrap32(ulPos - 1) + 1 <= ext(pucHist)（depth 1） | — | — |
| `Ue_CalcJitter_OK` | **R1** ulPos >= 1 ⇒ R1 @ *pucHist，末端 ulPos；需 ulPos <= ext(pucHist)（depth 1） | — | — |
| `Ue_UpdateJitter` | **R1** R1 @ *pucHist，末端 wrap32(ulPos - 1) + 1；需 wrap32(ulPos - 1) + 1 <= ext(pucHist)（depth 2） | — | — |
| `Ue_UpdateJitter_OK` | **R1** ulPos >= 1 ⇒ R1 @ *pucHist，末端 ulPos；需 ulPos <= ext(pucHist)（depth 2） | — | — |
| `Ue_CheckJitter_TN` | — | — | — |
| `Ue_CheckJitter_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Hist_GetPrev.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Hist_GetPrev",
  "key": "F@lv2_09_unsigned_wrap.c:Hist_GetPrev",
  "loc": "lv2_09_unsigned_wrap.c:27",
  "params": ["pucHist: UINT8*", "ulPos: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "R",
      "bytes": "1",
      "obj": "*pucHist",
      "have": "ext(p0)",
      "end": ["wrap32(p1 - 1) + 1", "wrap32(p1 - 1) + 1"],
      "start": "wrap32(p1 - 1)",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Hist_GetPrev", "at": "lv2_09_unsigned_wrap.c:29", "code": "pucHist[ulPos - 1]"},
      "path": ["Hist_GetPrev@lv2_09_unsigned_wrap.c:29"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "text": "R1 @ *pucHist，末端 wrap32(ulPos - 1) + 1；需 wrap32(ulPos - 1) + 1 <= ext(pucHist)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Hist_GetPrevSafe.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Hist_GetPrevSafe",
  "key": "F@lv2_09_unsigned_wrap.c:Hist_GetPrevSafe",
  "loc": "lv2_09_unsigned_wrap.c:32",
  "params": ["pucHist: UINT8*", "ulPos: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "R",
      "bytes": "1",
      "obj": "*pucHist",
      "have": "ext(p0)",
      "end": ["p1", "p1"],
      "start": "p1 - 1",
      "bound": "exact",
      "guard": ["p1 >= 1"],
      "access": {"fn": "Hist_GetPrevSafe", "at": "lv2_09_unsigned_wrap.c:37", "code": "pucHist[ulPos - 1]"},
      "path": ["Hist_GetPrevSafe@lv2_09_unsigned_wrap.c:37"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "if (ulPos == 0) return → 守卫 ulPos >= 1；守卫下 ulPos − 1 不回绕",
      "text": "ulPos >= 1 ⇒ R1 @ *pucHist，末端 ulPos；需 ulPos <= ext(pucHist)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_CheckJitter_TN.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_CheckJitter_TN",
  "key": "F:Ue_CheckJitter_TN",
  "loc": "lv2_09_unsigned_wrap.c:51",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv2_09_unsigned_wrap.c:54",
      "callee": "Ue_UpdateJitter_OK",
      "args": [{"ptr": "aucHist", "have": "16"}, "0"],
      "results": [{"req": "R1", "outcome": "safe"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_CheckJitter_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_CheckJitter_TP",
  "key": "F:Ue_CheckJitter_TP",
  "loc": "lv2_09_unsigned_wrap.c:45",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv2_09_unsigned_wrap.c:48",
      "callee": "Ue_UpdateJitter",
      "args": [{"ptr": "aucHist", "have": "16"}, "0"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}]
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "lv2_09_unsigned_wrap.c:48",
      "access": "Hist_GetPrev@lv2_09_unsigned_wrap.c:29",
      "object": "aucHist",
      "have": "16",
      "end": ["4294967296", "4294967296"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Ue_CheckJitter_TP@lv2_09_unsigned_wrap.c:48",
        "Ue_UpdateJitter@lv2_09_unsigned_wrap.c:42",
        "Ue_CalcJitter@lv2_09_unsigned_wrap.c:40",
        "Hist_GetPrev@lv2_09_unsigned_wrap.c:29"
      ],
      "msg": "Definite out-of-bound read through a 3-level call chain: up to byte 4294967296 of 'aucHist' (16 bytes) is accessed",
      "fp": "c5fa4651a3eb05ff"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV2-09 -v`）

```text
- Ue_CalcJitter @ lv2_09_unsigned_wrap.c:40  调用 Hist_GetPrev(pucHist, p1)
    · Hist_GetPrev.R1：末端 wrap32(p1 - 1) + 1，对象 ext(p0) → up → 本函数 R1
- Ue_CalcJitter_OK @ lv2_09_unsigned_wrap.c:41  调用 Hist_GetPrevSafe(pucHist, p1)
    · Hist_GetPrevSafe.R1：末端 p1，守卫 p1 >= 1，对象 ext(p0) → up → 本函数 R1
- Ue_UpdateJitter @ lv2_09_unsigned_wrap.c:42  调用 Ue_CalcJitter(pucHist, p1)
    · Ue_CalcJitter.R1：末端 wrap32(p1 - 1) + 1，对象 ext(p0) → up → 本函数 R1
- Ue_UpdateJitter_OK @ lv2_09_unsigned_wrap.c:43  调用 Ue_CalcJitter_OK(pucHist, p1)
    · Ue_CalcJitter_OK.R1：末端 p1，守卫 p1 >= 1，对象 ext(p0) → up → 本函数 R1
- Ue_CheckJitter_TN @ lv2_09_unsigned_wrap.c:54  调用 Ue_UpdateJitter_OK(aucHist, 0)
    · Ue_UpdateJitter_OK.R1：末端 0，守卫在此不成立，对象 16 → safe（守卫 0 >= 1 在此为假，访问走不到）
- Ue_CheckJitter_TP @ lv2_09_unsigned_wrap.c:48  调用 Ue_UpdateJitter(aucHist, 0)
    · Ue_UpdateJitter.R1：末端 4294967296，对象 16 → L1（最小末端 4294967296 > 16，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Ue_CheckJitter_TP pa=L1 → 回放 L1 ✓
- 期望 TN Ue_CheckJitter_TN pa=- → 回放 - ✓
```

### LV3-03 访问函数自己校验了，但校验的界比多层之上传进来的数组大

**条件带守卫**：`ulPortId <= 31 ⇒ 需 24*ulPortId + 4 <= ext(astPort)`。如果只留区间 `[0, 31]`，条件会变成 `748 <= ext`，常量 3 的调用点（TN）也会被判越界。到链顶对象已知（全局表 384 字节），而 `ulPortId` 是链顶自己的形参、只被守卫框在 `[0, 31]` —— 这是代码自己写出来的界，按 06- F-12 判 L2（最大末端 748、最小末端 4）。

**代码**（`lv3/lv3_03_callee_guard_wrong.c`；只保留函数体，头注释略）

```c
#define MAX_PORT_NUM     16
#define MAX_PORT_NUM_V2  32

typedef struct {
    UINT32 ulMode;
    UINT32 ulSpeed;
    UINT8  aucMac[16];
} PORT_S;

static PORT_S g_astBoardPort[MAX_PORT_NUM];

static void Port_SetMode(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode)
{
    if (ulPortId >= MAX_PORT_NUM_V2) {
        return;
    }
    astPort[ulPortId].ulMode = ulMode;                        /* ★1 */
}

static void Port_ApplyCfg(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode) { Port_SetMode(astPort, ulPortId, ulMode); }
static void Board_CfgPort(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode) { Port_ApplyCfg(astPort, ulPortId, ulMode); }

void Board_OnCfgMsg_TP(UINT32 ulPortId, UINT32 ulMode) { Board_CfgPort(g_astBoardPort, ulPortId, ulMode); }
void Board_InitDefault_TN(void)                        { Board_CfgPort(g_astBoardPort, 3, 0); }
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV3-03/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Port_SetMode` | **R1** ulPortId <= 31 ⇒ W4 @ *astPort，末端 24*ulPortId + 4；需 24*ulPortId + 4 <= ext(astPort) | — | — |
| `Port_ApplyCfg` | **R1** ulPortId <= 31 ⇒ W4 @ *astPort，末端 24*ulPortId + 4；需 24*ulPortId + 4 <= ext(astPort)（depth 1） | — | — |
| `Board_CfgPort` | **R1** ulPortId <= 31 ⇒ W4 @ *astPort，末端 24*ulPortId + 4；需 24*ulPortId + 4 <= ext(astPort)（depth 2） | — | — |
| `Board_InitDefault_TN` | — | — | — |
| `Board_OnCfgMsg_TP` | — | — | **RPT1 L2**（depth 3） |

<details open><summary><code>Port_SetMode.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Port_SetMode",
  "key": "F@lv3_03_callee_guard_wrong.c:Port_SetMode",
  "loc": "lv3_03_callee_guard_wrong.c:35",
  "params": ["astPort: PORT_S*", "ulPortId: UINT32", "ulMode: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "*astPort",
      "have": "ext(p0)",
      "end": ["24*p1 + 4", "24*p1 + 4"],
      "start": "24*p1",
      "bound": "exact",
      "guard": ["p1 <= 31"],
      "access": {
        "fn": "Port_SetMode",
        "at": "lv3_03_callee_guard_wrong.c:40",
        "code": "astPort[ulPortId].ulMode = ulMode"
      },
      "path": ["Port_SetMode@lv3_03_callee_guard_wrong.c:40"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "if (ulPortId >= 32) return → 守卫；PORT_S 24 字节，ulMode 在偏移 0",
      "text": "ulPortId <= 31 ⇒ W4 @ *astPort，末端 24*ulPortId + 4；需 24*ulPortId + 4 <= ext(astPort)"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Board_InitDefault_TN.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Board_InitDefault_TN",
  "key": "F:Board_InitDefault_TN",
  "loc": "lv3_03_callee_guard_wrong.c:47",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv3_03_callee_guard_wrong.c:47",
      "callee": "Board_CfgPort",
      "args": [{"ptr": "g_astBoardPort", "have": "384"}, "3", "0"],
      "results": [{"req": "R1", "outcome": "safe"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Board_OnCfgMsg_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Board_OnCfgMsg_TP",
  "key": "F:Board_OnCfgMsg_TP",
  "loc": "lv3_03_callee_guard_wrong.c:46",
  "params": ["ulPortId: UINT32", "ulMode: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv3_03_callee_guard_wrong.c:46",
      "callee": "Board_CfgPort",
      "args": [{"ptr": "g_astBoardPort", "have": "384"}, "p0", "p1"],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}],
      "note": "PORT_S g_astBoardPort[16]，384 字节"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv3_03_callee_guard_wrong.c:46",
      "access": "Port_SetMode@lv3_03_callee_guard_wrong.c:40",
      "object": "g_astBoardPort",
      "have": "384",
      "end": ["4", "748"],
      "bound": "guard",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Board_OnCfgMsg_TP@lv3_03_callee_guard_wrong.c:46",
        "Board_CfgPort@lv3_03_callee_guard_wrong.c:44",
        "Port_ApplyCfg@lv3_03_callee_guard_wrong.c:43",
        "Port_SetMode@lv3_03_callee_guard_wrong.c:40"
      ],
      "msg": "Possible out-of-bound write through a 3-level call chain: up to byte 748 of 'g_astBoardPort' (384 bytes) may be accessed; the bound comes from the check in Port_SetMode",
      "fp": "4d4e0d10fe4e1698"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV3-03 -v`）

```text
- Port_ApplyCfg @ lv3_03_callee_guard_wrong.c:43  调用 Port_SetMode(astPort, p1, p2)
    · Port_SetMode.R1：末端 24*p1 + 4，守卫 p1 <= 31，对象 ext(p0) → up → 本函数 R1
- Board_CfgPort @ lv3_03_callee_guard_wrong.c:44  调用 Port_ApplyCfg(astPort, p1, p2)
    · Port_ApplyCfg.R1：末端 24*p1 + 4，守卫 p1 <= 31，对象 ext(p0) → up → 本函数 R1
- Board_InitDefault_TN @ lv3_03_callee_guard_wrong.c:47  调用 Board_CfgPort(g_astBoardPort, 3, 0)
    · Board_CfgPort.R1：末端 76，守卫在此成立，对象 384 → safe（最大末端 76 ≤ 384）
- Board_OnCfgMsg_TP @ lv3_03_callee_guard_wrong.c:46  调用 Board_CfgPort(g_astBoardPort, p0, p1)
    · Board_CfgPort.R1：末端 24*p0 + 4，守卫 p0 <= 31，对象 384 → L2（guard 给出的范围内最大末端 748 > 384，最小末端 4 不越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Board_OnCfgMsg_TP pa=L2 → 回放 L2 ✓
- 期望 TN Board_InitDefault_TN pa=- → 回放 - ✓
```

### LV3-07 报文组装循环：按剩余长度拷贝（安全） vs. destMax 用整包长度（缺陷）

**先化简、后代区间**：安全写法里目的末端 = `ulOff + (ulPktLen − ulOff)`，`ulOff` 两边抵消，与它的循环区间无关，摘要就是 `ulPktLen <= ext(pucPkt)`。缺陷写法里末端 = `ulOff + ulPktLen`，`ulOff` 只能换成它的区间 `[0, ulPktLen]`（来自循环里的校验），末端是 `[ulPktLen, 2*ulPktLen]`。源指针 `astIe[i].pucVal` 来自数组元素的字段，P0 不跟踪，记账。

**代码**（`lv3/lv3_07_pkt_loop_destmax.c`；只保留函数体，头注释略）

```c
#define PKT_LEN 128

typedef struct {
    const UINT8 *pucVal;
    UINT32       ulLen;
} IE_S;

static UINT32 Pkt_PutIes_OK(UINT8 *pucPkt, UINT32 ulPktLen, const IE_S *astIe, UINT32 ulIeNum)
{
    UINT32 i;
    UINT32 ulOff = 0;
    for (i = 0; i < ulIeNum; i++) {
        if (ulOff + astIe[i].ulLen > ulPktLen) {
            break;
        }
        if (memcpy_s(pucPkt + ulOff, ulPktLen - ulOff, astIe[i].pucVal, astIe[i].ulLen) != EOK) {   /* ★1 */
            break;
        }
        ulOff += astIe[i].ulLen;
    }
    return ulOff;
}

static UINT32 Pkt_PutIes(UINT8 *pucPkt, UINT32 ulPktLen, const IE_S *astIe, UINT32 ulIeNum)
{
    UINT32 i;
    UINT32 ulOff = 0;
    for (i = 0; i < ulIeNum; i++) {
        if (ulOff + astIe[i].ulLen > ulPktLen) {
            break;
        }
        if (memcpy_s(pucPkt + ulOff, ulPktLen, astIe[i].pucVal, astIe[i].ulLen) != EOK) {          /* ★2 */
            break;
        }
        ulOff += astIe[i].ulLen;
    }
    return ulOff;
}

static UINT32 Body_PutIes(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)       { return Pkt_PutIes(p, n, a, k); }
static UINT32 Body_PutIes_OK(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)    { return Pkt_PutIes_OK(p, n, a, k); }
static UINT32 Msg_BuildBody(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)     { return Body_PutIes(p, n, a, k); }
static UINT32 Msg_BuildBody_OK(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)  { return Body_PutIes_OK(p, n, a, k); }

UINT32 Msg_Encode_TP(const IE_S *astIe, UINT32 ulIeNum)
{
    UINT8 aucPkt[PKT_LEN];
    return Msg_BuildBody(aucPkt, sizeof(aucPkt), astIe, ulIeNum);
}

UINT32 Msg_Encode_TN(const IE_S *astIe, UINT32 ulIeNum)
{
    UINT8 aucPkt[PKT_LEN];
    return Msg_BuildBody_OK(aucPkt, sizeof(aucPkt), astIe, ulIeNum);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV3-07/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Pkt_PutIes` | **R1** WulPktLen @ *pucPkt，末端 [ulPktLen, 2*ulPktLen]；需 2*ulPktLen <= ext(pucPkt) | — | 账本 `untracked_pointer` |
| `Pkt_PutIes_OK` | **R1** WulPktLen - ulOff @ *pucPkt，末端 ulPktLen；需 ulPktLen <= ext(pucPkt) | — | 账本 `untracked_pointer` |
| `Body_PutIes` | **R1** Wn @ *p，末端 [n, 2*n]；需 2*n <= ext(p)（depth 1） | — | — |
| `Body_PutIes_OK` | **R1** Wn - ulOff @ *p，末端 n；需 n <= ext(p)（depth 1） | — | — |
| `Msg_BuildBody` | **R1** Wn @ *p，末端 [n, 2*n]；需 2*n <= ext(p)（depth 2） | — | — |
| `Msg_BuildBody_OK` | **R1** Wn - ulOff @ *p，末端 n；需 n <= ext(p)（depth 2） | — | — |
| `Msg_Encode_TN` | — | — | — |
| `Msg_Encode_TP` | — | — | **RPT1 L2**（depth 3） |

<details open><summary><code>Pkt_PutIes.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Pkt_PutIes",
  "key": "F@lv3_07_pkt_loop_destmax.c:Pkt_PutIes",
  "loc": "lv3_07_pkt_loop_destmax.c:50",
  "params": ["pucPkt: UINT8*", "ulPktLen: UINT32", "astIe: IE_S*", "ulIeNum: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "p1",
      "obj": "*pucPkt",
      "have": "ext(p0)",
      "end": ["p1", "2*p1"],
      "start": "0",
      "bound": "check",
      "guard": [],
      "access": {
        "fn": "Pkt_PutIes",
        "at": "lv3_07_pkt_loop_destmax.c:58",
        "code": "memcpy_s(pucPkt + ulOff, ulPktLen, astIe[i].pucVal, astIe[i].ulLen)"
      },
      "path": ["Pkt_PutIes@lv3_07_pkt_loop_destmax.c:58"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "末端 ulOff + ulPktLen，ulOff ∈ [0, ulPktLen]（循环里的校验 ulOff + len <= ulPktLen）",
      "text": "WulPktLen @ *pucPkt，末端 [ulPktLen, 2*ulPktLen]；需 2*ulPktLen <= ext(pucPkt)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv3_07_pkt_loop_destmax.c:58",
      "callee": "memcpy_s",
      "model": "access",
      "args": [{"ptr": "pucPkt + ulOff", "have": "ext(p0) - ulOff"}, "p1", null, null],
      "locals": {"ulOff": {"range": ["0", "p1"], "bound": "check"}},
      "results": [{"note": "访问类模型 memcpy_s → R1；源 astIe[i].pucVal 记账"}]
    }
  ],
  "reps": [],
  "opens": [
    {
      "reason": "untracked_pointer",
      "at": "lv3_07_pkt_loop_destmax.c:58",
      "detail": "源指针 astIe[i].pucVal 来自数组元素的字段，P0 不跟踪它指向多大"
    }
  ]
}
```
</details>

<details open><summary><code>Pkt_PutIes_OK.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Pkt_PutIes_OK",
  "key": "F@lv3_07_pkt_loop_destmax.c:Pkt_PutIes_OK",
  "loc": "lv3_07_pkt_loop_destmax.c:34",
  "params": ["pucPkt: UINT8*", "ulPktLen: UINT32", "astIe: IE_S*", "ulIeNum: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "p1 - ulOff",
      "obj": "*pucPkt",
      "have": "ext(p0)",
      "end": ["p1", "p1"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Pkt_PutIes_OK",
        "at": "lv3_07_pkt_loop_destmax.c:42",
        "code": "memcpy_s(pucPkt + ulOff, ulPktLen - ulOff, astIe[i].pucVal, astIe[i].ulLen)"
      },
      "path": ["Pkt_PutIes_OK@lv3_07_pkt_loop_destmax.c:42"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "末端 ulOff + (ulPktLen − ulOff)：先抵消 ulOff 再代区间；校验 ulOff + len <= ulPktLen 保证减法不回绕",
      "text": "WulPktLen - ulOff @ *pucPkt，末端 ulPktLen；需 ulPktLen <= ext(pucPkt)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv3_07_pkt_loop_destmax.c:42",
      "callee": "memcpy_s",
      "model": "access",
      "args": [{"ptr": "pucPkt + ulOff", "have": "ext(p0) - ulOff"}, "p1 - ulOff", null, null],
      "locals": {"ulOff": {"range": ["0", "p1"], "bound": "check"}},
      "results": [{"note": "访问类模型 memcpy_s → R1；源 astIe[i].pucVal 记账"}]
    }
  ],
  "reps": [],
  "opens": [
    {
      "reason": "untracked_pointer",
      "at": "lv3_07_pkt_loop_destmax.c:42",
      "detail": "源指针 astIe[i].pucVal 来自数组元素的字段，P0 不跟踪它指向多大"
    }
  ]
}
```
</details>

<details open><summary><code>Msg_Encode_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Msg_Encode_TP",
  "key": "F:Msg_Encode_TP",
  "loc": "lv3_07_pkt_loop_destmax.c:71",
  "params": ["astIe: IE_S*", "ulIeNum: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv3_07_pkt_loop_destmax.c:74",
      "callee": "Msg_BuildBody",
      "args": [{"ptr": "aucPkt", "have": "128"}, "128", {"ptr": "astIe", "have": "ext(p0)", "alias": "p0"}, "p1"],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}],
      "note": "aucPkt 128 字节，ulPktLen = sizeof(aucPkt) = 128"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv3_07_pkt_loop_destmax.c:74",
      "access": "Pkt_PutIes@lv3_07_pkt_loop_destmax.c:58",
      "object": "aucPkt",
      "have": "128",
      "end": ["128", "256"],
      "bound": "check",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Msg_Encode_TP@lv3_07_pkt_loop_destmax.c:74",
        "Msg_BuildBody@lv3_07_pkt_loop_destmax.c:68",
        "Body_PutIes@lv3_07_pkt_loop_destmax.c:66",
        "Pkt_PutIes@lv3_07_pkt_loop_destmax.c:58"
      ],
      "msg": "Possible out-of-bound write through a 3-level call chain: up to byte 256 of 'aucPkt' (128 bytes) may be accessed; the bound comes from a check in Pkt_PutIes",
      "fp": "9c907dff54fea69f"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV3-07 -v`）

```text
- Pkt_PutIes @ lv3_07_pkt_loop_destmax.c:58  调用 memcpy_s(pucPkt + ulOff, p1, …, …)
    · 访问类模型 memcpy_s → R1；源 astIe[i].pucVal 记账
- Pkt_PutIes_OK @ lv3_07_pkt_loop_destmax.c:42  调用 memcpy_s(pucPkt + ulOff, p1 - ulOff, …, …)
    · 访问类模型 memcpy_s → R1；源 astIe[i].pucVal 记账
- Body_PutIes @ lv3_07_pkt_loop_destmax.c:66  调用 Pkt_PutIes(p, p1, a, p3)
    · Pkt_PutIes.R1：末端 [p1, 2*p1]，对象 ext(p0) → up → 本函数 R1
- Body_PutIes_OK @ lv3_07_pkt_loop_destmax.c:67  调用 Pkt_PutIes_OK(p, p1, a, p3)
    · Pkt_PutIes_OK.R1：末端 p1，对象 ext(p0) → up → 本函数 R1
- Msg_BuildBody @ lv3_07_pkt_loop_destmax.c:68  调用 Body_PutIes(p, p1, a, p3)
    · Body_PutIes.R1：末端 [p1, 2*p1]，对象 ext(p0) → up → 本函数 R1
- Msg_BuildBody_OK @ lv3_07_pkt_loop_destmax.c:69  调用 Body_PutIes_OK(p, p1, a, p3)
    · Body_PutIes_OK.R1：末端 p1，对象 ext(p0) → up → 本函数 R1
- Msg_Encode_TN @ lv3_07_pkt_loop_destmax.c:80  调用 Msg_BuildBody_OK(aucPkt, 128, astIe, p1)
    · Msg_BuildBody_OK.R1：末端 128，对象 128 → safe（最大末端 128 ≤ 128）
- Msg_Encode_TP @ lv3_07_pkt_loop_destmax.c:74  调用 Msg_BuildBody(aucPkt, 128, astIe, p1)
    · Msg_BuildBody.R1：末端 [128, 256]，对象 128 → L2（最大末端 256 > 128（上界来自 check），最小末端 128 不越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Msg_Encode_TP pa=L2 → 回放 L2 ✓
- 期望 TN Msg_Encode_TN pa=- → 回放 - ✓
```

### LV4-01 三层封装的分配器按老规格申请，初始化按新规格遍历

**V 形**：大小经 `ret_new` 效果往上走三层（`Pub_MemAlloc → Cell_MemAlloc → Ue_AllocTbl`，模型表只配了 `VOS_MemAlloc` 的字节数在第 3 个形参），访问条件经 Req 往上走三层（`Ue_InitOne → Ue_InitTbl → Ue_Setup`），在 `Ue_ModuleInit_TP` 相遇。循环把 `ulIdx` 换成 `[0, ulNum − 1]`，同时生成"循环至少执行一次"的守卫 `ulNum >= 1`。报告的层数取两侧的最大值：`depth = 3`、`eff_depth = 3`。

**代码**（`lv4/lv4_01_wrapper_alloc_spec.c`；只保留函数体，头注释略）

```c
/* common.h */
static inline void *Pub_MemAlloc(UINT32 ulSize)
{
    return VOS_MemAlloc(PID_CELL, 0, ulSize);
}

/* lv4_01_wrapper_alloc_spec.c */
#define MAX_UE_NUM     32
#define MAX_UE_NUM_EX  64

typedef struct {
    UINT32 ulState;
    UINT32 ulUeId;
    UINT32 ulTimer;
    UINT32 ulRsv;
} UE_S;

static void *Cell_MemAlloc(UINT32 ulSize)
{
    void *p = Pub_MemAlloc(ulSize);
    if (p == VOS_NULL_PTR) {
        LOG_Print(1, "cell alloc %u fail", ulSize);
    }
    return p;
}

static UE_S *Ue_AllocTbl(void)
{
    return (UE_S *)Cell_MemAlloc(MAX_UE_NUM * (UINT32)sizeof(UE_S));
}

static void Ue_InitOne(UE_S *pstTbl, UINT32 ulIdx)
{
    pstTbl[ulIdx].ulState = 0;                                /* ★1 */
}

static void Ue_InitTbl(UE_S *pstTbl, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        Ue_InitOne(pstTbl, i);
    }
}

static void Ue_Setup(UE_S *pstTbl)    { Ue_InitTbl(pstTbl, MAX_UE_NUM_EX); }
static void Ue_Setup_OK(UE_S *pstTbl) { Ue_InitTbl(pstTbl, MAX_UE_NUM); }

void Ue_ModuleInit_TP(void)
{
    UE_S *pstTbl = Ue_AllocTbl();
    if (pstTbl == VOS_NULL_PTR) {
        return;
    }
    Ue_Setup(pstTbl);
}

void Ue_ModuleInit_TN(void)
{
    UE_S *pstTbl = Ue_AllocTbl();
    if (pstTbl == VOS_NULL_PTR) {
        return;
    }
    Ue_Setup_OK(pstTbl);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV4-01/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Pub_MemAlloc` | — | ret = new(ulSize) 或 NULL | — |
| `Ue_InitOne` | **R1** W4 @ *pstTbl，末端 16*ulIdx + 4；需 16*ulIdx + 4 <= ext(pstTbl) | — | — |
| `Cell_MemAlloc` | — | ret = new(ulSize) 或 NULL | — |
| `Ue_InitTbl` | **R1** ulNum >= 1 ⇒ W4 @ *pstTbl，末端 [4, 16*ulNum - 12]；需 16*ulNum - 12 <= ext(pstTbl)（depth 1） | — | — |
| `Ue_AllocTbl` | — | ret = new(512) 或 NULL | — |
| `Ue_Setup` | **R1** W4 @ *pstTbl，末端 [4, 1012]；需 1012 <= ext(pstTbl)（depth 2） | — | — |
| `Ue_Setup_OK` | **R1** W4 @ *pstTbl，末端 [4, 500]；需 500 <= ext(pstTbl)（depth 2） | — | — |
| `Ue_ModuleInit_TN` | — | — | — |
| `Ue_ModuleInit_TP` | — | — | **RPT1 L2**（depth 3, eff_depth 3） |

<details open><summary><code>Pub_MemAlloc.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Pub_MemAlloc",
  "key": "F@common.h:Pub_MemAlloc#3fd1a0c2",
  "loc": "common.h:51",
  "params": ["ulSize: UINT32"],
  "status": "analyzed",
  "note": "头文件里的 static inline：每个 TU 一份，FIR 相同的只求解一次（D-08）",
  "reqs": [],
  "effs": [
    {
      "kind": "ret_new",
      "size": "p0",
      "maybe_null": true,
      "eff_depth": 1,
      "via": "VOS_MemAlloc（模型表 allocators：size = p2）",
      "text": "ret = new(ulSize) 或 NULL"
    }
  ],
  "calls": [
    {
      "site": "common.h:53",
      "callee": "VOS_MemAlloc",
      "model": "alloc",
      "args": ["PID_CELL", "0", "p0"],
      "results": [{"note": "分配器模型：返回 new(实参 2) 或 NULL → 本函数 ret_new(size = ulSize)"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_InitTbl.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_InitTbl",
  "key": "F@lv4_01_wrapper_alloc_spec.c:Ue_InitTbl",
  "loc": "lv4_01_wrapper_alloc_spec.c:55",
  "params": ["pstTbl: UE_S*", "ulNum: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "*pstTbl",
      "have": "ext(p0)",
      "end": ["4", "16*p1 - 12"],
      "start": "0",
      "bound": "loop",
      "guard": ["p1 >= 1"],
      "access": {"fn": "Ue_InitOne", "at": "lv4_01_wrapper_alloc_spec.c:52", "code": "pstTbl[ulIdx].ulState = 0"},
      "path": ["Ue_InitTbl@lv4_01_wrapper_alloc_spec.c:59", "Ue_InitOne@lv4_01_wrapper_alloc_spec.c:52"],
      "depth": 1,
      "eff_depth": 0,
      "blind": false,
      "text": "ulNum >= 1 ⇒ W4 @ *pstTbl，末端 [4, 16*ulNum - 12]；需 16*ulNum - 12 <= ext(pstTbl)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv4_01_wrapper_alloc_spec.c:59",
      "callee": "Ue_InitOne",
      "args": [{"ptr": "pstTbl", "have": "ext(p0)", "alias": "p0"}, "i"],
      "locals": {"i": {"range": ["0", "p1 - 1"], "bound": "loop"}},
      "results": [{"req": "R1", "outcome": "up", "as": "R1"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_AllocTbl.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_AllocTbl",
  "key": "F@lv4_01_wrapper_alloc_spec.c:Ue_AllocTbl",
  "loc": "lv4_01_wrapper_alloc_spec.c:45",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [
    {
      "kind": "ret_new",
      "size": "512",
      "maybe_null": true,
      "eff_depth": 3,
      "via": "Cell_MemAlloc",
      "text": "ret = new(512) 或 NULL"
    }
  ],
  "calls": [
    {
      "site": "lv4_01_wrapper_alloc_spec.c:47",
      "callee": "Cell_MemAlloc",
      "args": ["512"],
      "results": [{"eff": 0, "value": "new(512) | NULL"}],
      "note": "MAX_UE_NUM * sizeof(UE_S) = 32 * 16"
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ue_ModuleInit_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ue_ModuleInit_TP",
  "key": "F:Ue_ModuleInit_TP",
  "loc": "lv4_01_wrapper_alloc_spec.c:66",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv4_01_wrapper_alloc_spec.c:68",
      "callee": "Ue_AllocTbl",
      "args": [],
      "results": [{"eff": 0, "value": "new(512) | NULL"}],
      "note": "pstTbl := new(512) 或 NULL；随后 if (pstTbl == NULL) return 排除了 NULL"
    },
    {
      "site": "lv4_01_wrapper_alloc_spec.c:72",
      "callee": "Ue_Setup",
      "args": [{"ptr": "pstTbl", "have": "512", "eff_depth": 3}],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}]
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv4_01_wrapper_alloc_spec.c:72",
      "access": "Ue_InitOne@lv4_01_wrapper_alloc_spec.c:52",
      "object": "heap object allocated at lv4_01_wrapper_alloc_spec.c:68",
      "have": "512",
      "end": ["4", "1012"],
      "bound": "loop",
      "depth": 3,
      "eff_depth": 3,
      "chain": [
        "Ue_ModuleInit_TP@lv4_01_wrapper_alloc_spec.c:72",
        "Ue_Setup@lv4_01_wrapper_alloc_spec.c:63",
        "Ue_InitTbl@lv4_01_wrapper_alloc_spec.c:59",
        "Ue_InitOne@lv4_01_wrapper_alloc_spec.c:52"
      ],
      "msg": "Possible out-of-bound write through a 3-level call chain: up to byte 1012 of heap object allocated at lv4_01_wrapper_alloc_spec.c:68 (512 bytes) may be accessed; the bound comes from a loop bound in Ue_InitTbl",
      "fp": "684eb8aec5c6ebff"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV4-01 -v`）

```text
- Pub_MemAlloc @ common.h:53  调用 VOS_MemAlloc(PID_CELL, 0, p0)
    · 分配器模型：返回 new(实参 2) 或 NULL → 本函数 ret_new(size = ulSize)
- Cell_MemAlloc @ lv4_01_wrapper_alloc_spec.c:38  调用 Pub_MemAlloc(p0)
    · 效果 ret_new → new(p0) | NULL 
- Cell_MemAlloc @ lv4_01_wrapper_alloc_spec.c:40  调用 LOG_Print(1, …, p0)
    · LOG_Print 在模型表 pure 里：不失效任何被跟踪的内存，不产生条件
- Ue_InitTbl @ lv4_01_wrapper_alloc_spec.c:59  调用 Ue_InitOne(pstTbl, i)
    · Ue_InitOne.R1：末端 16*i + 4 → 局部换成区间 [4, 16*p1 - 12]，守卫 p1 >= 1，对象 ext(p0) → up → 本函数 R1
- Ue_AllocTbl @ lv4_01_wrapper_alloc_spec.c:47  调用 Cell_MemAlloc(512)
    · 效果 ret_new → new(512) | NULL 
- Ue_Setup @ lv4_01_wrapper_alloc_spec.c:63  调用 Ue_InitTbl(pstTbl, 64)
    · Ue_InitTbl.R1：末端 [4, 1012]，守卫在此成立，对象 ext(p0) → up → 本函数 R1
- Ue_Setup_OK @ lv4_01_wrapper_alloc_spec.c:64  调用 Ue_InitTbl(pstTbl, 32)
    · Ue_InitTbl.R1：末端 [4, 500]，守卫在此成立，对象 ext(p0) → up → 本函数 R1
- Ue_ModuleInit_TN @ lv4_01_wrapper_alloc_spec.c:77  调用 Ue_AllocTbl()
    · 效果 ret_new → new(512) | NULL 
- Ue_ModuleInit_TN @ lv4_01_wrapper_alloc_spec.c:81  调用 Ue_Setup_OK(pstTbl)
    · Ue_Setup_OK.R1：末端 [4, 500]，对象 512 → safe（最大末端 500 ≤ 512）
- Ue_ModuleInit_TP @ lv4_01_wrapper_alloc_spec.c:68  调用 Ue_AllocTbl()
    · 效果 ret_new → new(512) | NULL 
- Ue_ModuleInit_TP @ lv4_01_wrapper_alloc_spec.c:72  调用 Ue_Setup(pstTbl)
    · Ue_Setup.R1：末端 [4, 1012]，对象 512 → L2（最大末端 1012 > 512（上界来自 loop），最小末端 4 不越界） → RPT1（depth 3, eff_depth 3，报）
- 期望 TP Ue_ModuleInit_TP pa=L2 → 回放 L2 ✓
- 期望 TN Ue_ModuleInit_TN pa=- → 回放 - ✓
```

### LV4-06 校验封装在独立函数里（返回布尔），校验函数自己差了一

**条件效应**：`Cell_IsValidId` 的摘要不是 Req，而是 `ret == 1 ⇒ ulCellId <= 16`；`Cell_CheckPara` 把它沿返回值再包一层（`ret == 0 ⇒ ulCellId <= 16`）。链顶 `if (Cell_CheckPara(id) != VOS_OK) return;` 之后，这条效应变成调用点上的守卫 `ulCellId <= 16`（来源：校验）。访问侧的 Req `32*ulCellId + 32 <= 512` 代进来，对象已知、符号被校验框住 → L2。年内尽力项。

**代码**（`lv4/lv4_06_validator_func.c`；只保留函数体，头注释略）

```c
#define MAX_CELL_NUM 16

typedef struct {
    UINT32 ulState;
    UINT32 ulUeNum;
    UINT8  aucRsv[24];
} CELL_CTX_S;

static CELL_CTX_S g_astCellCtx[MAX_CELL_NUM];

static UINT32 Cell_IsValidId(UINT32 ulCellId)    { return (ulCellId <= MAX_CELL_NUM) ? VOS_TRUE : VOS_FALSE; }
static UINT32 Cell_IsValidId_OK(UINT32 ulCellId) { return (ulCellId < MAX_CELL_NUM) ? VOS_TRUE : VOS_FALSE; }

static UINT32 Cell_CheckPara(UINT32 ulCellId)
{
    if (Cell_IsValidId(ulCellId) == VOS_FALSE) {
        return VOS_ERR;
    }
    return VOS_OK;
}

static UINT32 Cell_CheckPara_OK(UINT32 ulCellId)
{
    if (Cell_IsValidId_OK(ulCellId) == VOS_FALSE) {
        return VOS_ERR;
    }
    return VOS_OK;
}

static void Ctx_Clear(UINT32 ulCellId)
{
    (void)memset_s(&g_astCellCtx[ulCellId], sizeof(CELL_CTX_S), 0, sizeof(CELL_CTX_S));   /* ★1 */
}

static void Cell_FreeCtx(UINT32 ulCellId) { Ctx_Clear(ulCellId); }
static void Cell_Delete(UINT32 ulCellId)  { Cell_FreeCtx(ulCellId); }

UINT32 Cell_OnDelReq_TP(UINT32 ulCellId)
{
    if (Cell_CheckPara(ulCellId) != VOS_OK) {
        return VOS_ERR;
    }
    Cell_Delete(ulCellId);
    return VOS_OK;
}

UINT32 Cell_OnDelReq_TN(UINT32 ulCellId)
{
    if (Cell_CheckPara_OK(ulCellId) != VOS_OK) {
        return VOS_ERR;
    }
    Cell_Delete(ulCellId);
    return VOS_OK;
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV4-06/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Cell_IsValidId` | — | ret == 1 ⇒ ulCellId <= 16<br>ret == 0 ⇒ ulCellId >= 17<br>ret ∈ [0, 1] | — |
| `Cell_IsValidId_OK` | — | ret == 1 ⇒ ulCellId <= 15<br>ret == 0 ⇒ ulCellId >= 16<br>ret ∈ [0, 1] | — |
| `Ctx_Clear` | **R1** W32 @ g_astCellCtx，末端 32*ulCellId + 32；需 32*ulCellId + 32 <= 512 | — | — |
| `Cell_CheckPara` | — | ret == 0 ⇒ ulCellId <= 16<br>ret == 1 ⇒ ulCellId >= 17<br>ret ∈ [0, 1] | — |
| `Cell_CheckPara_OK` | — | ret == 0 ⇒ ulCellId <= 15<br>ret == 1 ⇒ ulCellId >= 16<br>ret ∈ [0, 1] | — |
| `Cell_FreeCtx` | **R1** W32 @ g_astCellCtx，末端 32*ulCellId + 32；需 32*ulCellId + 32 <= 512（depth 1） | — | — |
| `Cell_Delete` | **R1** W32 @ g_astCellCtx，末端 32*ulCellId + 32；需 32*ulCellId + 32 <= 512（depth 2） | — | — |
| `Cell_OnDelReq_TN` | — | ret ∈ [0, 1] | — |
| `Cell_OnDelReq_TP` | — | ret ∈ [0, 1] | **RPT1 L2**（depth 3） |

<details open><summary><code>Cell_IsValidId.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cell_IsValidId",
  "key": "F@lv4_06_validator_func.c:Cell_IsValidId",
  "loc": "lv4_06_validator_func.c:33",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [
    {"kind": "ret_cond", "when": "ret == 1", "then": ["p0 <= 16"], "text": "ret == 1 ⇒ ulCellId <= 16"},
    {"kind": "ret_cond", "when": "ret == 0", "then": ["p0 >= 17"], "text": "ret == 0 ⇒ ulCellId >= 17"},
    {"kind": "ret_int", "range": ["0", "1"], "bound": "const", "text": "ret ∈ [0, 1]"}
  ],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Ctx_Clear.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Ctx_Clear",
  "key": "F@lv4_06_validator_func.c:Ctx_Clear",
  "loc": "lv4_06_validator_func.c:52",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "32",
      "obj": "g_astCellCtx",
      "have": "512",
      "end": ["32*p0 + 32", "32*p0 + 32"],
      "start": "32*p0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Ctx_Clear",
        "at": "lv4_06_validator_func.c:54",
        "code": "memset_s(&g_astCellCtx[ulCellId], sizeof(CELL_CTX_S), 0, sizeof(CELL_CTX_S))"
      },
      "path": ["Ctx_Clear@lv4_06_validator_func.c:54"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "CELL_CTX_S 32 字节 × 16 = 512",
      "text": "W32 @ g_astCellCtx，末端 32*ulCellId + 32；需 32*ulCellId + 32 <= 512"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv4_06_validator_func.c:54",
      "callee": "memset_s",
      "model": "access",
      "args": [{"ptr": "&g_astCellCtx[ulCellId]", "have": "512 - 32*p0"}, "32", "0", "32"],
      "results": [{"note": "访问类模型 memset_s → R1"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Cell_CheckPara.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cell_CheckPara",
  "key": "F@lv4_06_validator_func.c:Cell_CheckPara",
  "loc": "lv4_06_validator_func.c:36",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [
    {"kind": "ret_cond", "when": "ret == 0", "then": ["p0 <= 16"], "text": "ret == 0 ⇒ ulCellId <= 16"},
    {"kind": "ret_cond", "when": "ret == 1", "then": ["p0 >= 17"], "text": "ret == 1 ⇒ ulCellId >= 17"},
    {"kind": "ret_int", "range": ["0", "1"], "bound": "const", "text": "ret ∈ [0, 1]"}
  ],
  "calls": [
    {
      "site": "lv4_06_validator_func.c:38",
      "callee": "Cell_IsValidId",
      "args": ["p0"],
      "results": [{"eff": 0, "value": "ret == 1 ⇒ p0 <= 16"}, {"eff": 1, "value": "ret == 0 ⇒ p0 >= 17"}],
      "note": "== VOS_FALSE 那一侧 return VOS_ERR(1)，另一侧 return VOS_OK(0) → 本函数的两条 ret_cond"
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Cell_OnDelReq_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cell_OnDelReq_TP",
  "key": "F:Cell_OnDelReq_TP",
  "loc": "lv4_06_validator_func.c:60",
  "params": ["ulCellId: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [{"kind": "ret_int", "range": ["0", "1"], "bound": "const", "text": "ret ∈ [0, 1]"}],
  "calls": [
    {
      "site": "lv4_06_validator_func.c:62",
      "callee": "Cell_CheckPara",
      "args": ["p0"],
      "results": [{"eff": 0, "value": "ret == 0 ⇒ p0 <= 16"}, {"eff": 1, "value": "ret == 1 ⇒ p0 >= 17"}],
      "note": "分支 != VOS_OK 就 return；落到下面的一侧 ret == VOS_OK(0) → 守卫 ulCellId <= 16"
    },
    {
      "site": "lv4_06_validator_func.c:65",
      "callee": "Cell_Delete",
      "args": ["p0"],
      "guards": [{"cond": "p0 <= 16", "bound": "check", "from": "Cell_CheckPara 的条件效应 ret == 0 ⇒ ulCellId <= 16"}],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}]
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv4_06_validator_func.c:65",
      "access": "Ctx_Clear@lv4_06_validator_func.c:54",
      "object": "g_astCellCtx",
      "have": "512",
      "end": ["32", "544"],
      "bound": "check",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Cell_OnDelReq_TP@lv4_06_validator_func.c:65",
        "Cell_Delete@lv4_06_validator_func.c:58",
        "Cell_FreeCtx@lv4_06_validator_func.c:57",
        "Ctx_Clear@lv4_06_validator_func.c:54"
      ],
      "msg": "Possible out-of-bound write through a 3-level call chain: up to byte 544 of 'g_astCellCtx' (512 bytes) may be accessed; the bound comes from the check in Cell_IsValidId",
      "fp": "3de7d6e6da242026"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV4-06 -v`）

```text
- Ctx_Clear @ lv4_06_validator_func.c:54  调用 memset_s(&g_astCellCtx[ulCellId], 32, 0, 32)
    · 访问类模型 memset_s → R1
- Cell_CheckPara @ lv4_06_validator_func.c:38  调用 Cell_IsValidId(p0)
    · 效果 ret_cond → ret == 1 ⇒ p0 <= 16 
    · 效果 ret_cond → ret == 0 ⇒ p0 >= 17 
- Cell_CheckPara_OK @ lv4_06_validator_func.c:46  调用 Cell_IsValidId_OK(p0)
    · 效果 ret_cond → ret == 1 ⇒ p0 <= 15 
    · 效果 ret_cond → ret == 0 ⇒ p0 >= 16 
- Cell_FreeCtx @ lv4_06_validator_func.c:57  调用 Ctx_Clear(p0)
    · Ctx_Clear.R1：末端 32*p0 + 32，对象 512 → up → 本函数 R1
- Cell_Delete @ lv4_06_validator_func.c:58  调用 Cell_FreeCtx(p0)
    · Cell_FreeCtx.R1：末端 32*p0 + 32，对象 512 → up → 本函数 R1
- Cell_OnDelReq_TN @ lv4_06_validator_func.c:71  调用 Cell_CheckPara_OK(p0)
    · 效果 ret_cond → ret == 0 ⇒ p0 <= 15 
    · 效果 ret_cond → ret == 1 ⇒ p0 >= 16 
- Cell_OnDelReq_TN @ lv4_06_validator_func.c:74  调用 Cell_Delete(p0)
    · Cell_Delete.R1：末端 32*p0 + 32，对象 512 → safe（守卫蕴含 32*p0 + 32 ≤ 512）
- Cell_OnDelReq_TP @ lv4_06_validator_func.c:62  调用 Cell_CheckPara(p0)
    · 效果 ret_cond → ret == 0 ⇒ p0 <= 16 
    · 效果 ret_cond → ret == 1 ⇒ p0 >= 17 
- Cell_OnDelReq_TP @ lv4_06_validator_func.c:65  调用 Cell_Delete(p0)
    · Cell_Delete.R1：末端 32*p0 + 32，对象 512 → L2（check 给出的范围内最大末端 544 > 512，最小末端 32 不越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Cell_OnDelReq_TP pa=L2 → 回放 L2 ✓
- 期望 TN Cell_OnDelReq_TN pa=- → 回放 - ✓
```

### LV5-01 编码上下文在链顶填好（缓冲区 + 长度），长度用了新宏，缓冲区还是老大小

**字段符号**：基址和长度都是"形参指向的结构体的字段"——`ext(p0->pucBuf)`、`p0->ulBufLen`。`fields` 表给出每个字段的偏移、大小、类型（这里按目标 ABI ILP32 排布）。中间层把同一个指针原样往下传（`alias`），字段符号路径不变；链顶在自己的栈帧里记着 `stCtx.pucBuf = aucBuf`（128）、`stCtx.ulBufLen = 256`，代入后 `256 <= 128` 不成立 → L1。另外 `ulUsed` 被写成 `ulLen`，作为 `field` 效应上传。

**代码**（`lv5/lv5_01_ctx_filled_at_top.c`；只保留函数体，头注释略）

```c
#define ENC_BUF_LEN_OLD  128
#define ENC_BUF_LEN      256

typedef struct {
    UINT8  *pucBuf;
    UINT32  ulBufLen;
    UINT32  ulUsed;
} ENC_CTX_S;

static void Enc_PutBytes(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen)
{
    (void)memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen);   /* ★1 */
    pstCtx->ulUsed = ulLen;
}

static void Enc_PutIe(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen)  { Enc_PutBytes(pstCtx, pucIe, ulLen); }
static void Enc_Encode(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen) { Enc_PutIe(pstCtx, pucIe, ulLen); }

void Rrc_EncodeMsg_TP(void)
{
    UINT8     aucBuf[ENC_BUF_LEN_OLD];
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    stCtx.pucBuf   = aucBuf;
    stCtx.ulBufLen = ENC_BUF_LEN;
    stCtx.ulUsed   = 0;
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}

void Rrc_EncodeMsg_TN(void)
{
    UINT8     aucBuf[ENC_BUF_LEN_OLD];
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    stCtx.pucBuf   = aucBuf;
    stCtx.ulBufLen = sizeof(aucBuf);
    stCtx.ulUsed   = 0;
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV5-01/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Enc_PutBytes` | **R1** WpstCtx->ulBufLen @ *pstCtx->pucBuf，末端 pstCtx->ulBufLen；需 pstCtx->ulBufLen <= ext(pstCtx->pucBuf)<br>**R2** ulLen <= pstCtx->ulBufLen ⇒ RulLen @ *pucData，末端 ulLen；需 ulLen <= ext(pucData) | pstCtx->ulUsed := ulLen | — |
| `Enc_PutIe` | **R1** WpstCtx->ulBufLen @ *pstCtx->pucBuf，末端 pstCtx->ulBufLen；需 pstCtx->ulBufLen <= ext(pstCtx->pucBuf)（depth 1）<br>**R2** ulLen <= pstCtx->ulBufLen ⇒ RulLen @ *pucIe，末端 ulLen；需 ulLen <= ext(pucIe)（depth 1） | pstCtx->ulUsed := ulLen | — |
| `Enc_Encode` | **R1** WpstCtx->ulBufLen @ *pstCtx->pucBuf，末端 pstCtx->ulBufLen；需 pstCtx->ulBufLen <= ext(pstCtx->pucBuf)（depth 2）<br>**R2** ulLen <= pstCtx->ulBufLen ⇒ RulLen @ *pucIe，末端 ulLen；需 ulLen <= ext(pucIe)（depth 2） | pstCtx->ulUsed := ulLen | — |
| `Rrc_EncodeMsg_TN` | — | — | — |
| `Rrc_EncodeMsg_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Enc_PutBytes.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Enc_PutBytes",
  "key": "F@lv5_01_ctx_filled_at_top.c:Enc_PutBytes",
  "loc": "lv5_01_ctx_filled_at_top.c:34",
  "params": ["pstCtx: ENC_CTX_S*", "pucData: UINT8*", "ulLen: UINT32"],
  "fields": {
    "p0->pucBuf": {"off": 0, "size": 4, "type": "UINT8*"},
    "p0->ulBufLen": {"off": 4, "size": 4, "type": "UINT32"},
    "p0->ulUsed": {"off": 8, "size": 4, "type": "UINT32"}
  },
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "p0->ulBufLen",
      "obj": "*pstCtx->pucBuf",
      "have": "ext(p0->pucBuf)",
      "end": ["p0->ulBufLen", "p0->ulBufLen"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Enc_PutBytes",
        "at": "lv5_01_ctx_filled_at_top.c:36",
        "code": "memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen)"
      },
      "path": ["Enc_PutBytes@lv5_01_ctx_filled_at_top.c:36"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "text": "WpstCtx->ulBufLen @ *pstCtx->pucBuf，末端 pstCtx->ulBufLen；需 pstCtx->ulBufLen <= ext(pstCtx->pucBuf)"
    },
    {
      "id": "R2",
      "kind": "R",
      "bytes": "p2",
      "obj": "*pucData",
      "have": "ext(p1)",
      "end": ["p2", "p2"],
      "start": "0",
      "bound": "exact",
      "guard": ["p2 <= p0->ulBufLen"],
      "access": {
        "fn": "Enc_PutBytes",
        "at": "lv5_01_ctx_filled_at_top.c:36",
        "code": "memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen)"
      },
      "path": ["Enc_PutBytes@lv5_01_ctx_filled_at_top.c:36"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "count <= destMax 才读",
      "text": "ulLen <= pstCtx->ulBufLen ⇒ RulLen @ *pucData，末端 ulLen；需 ulLen <= ext(pucData)"
    }
  ],
  "effs": [{"kind": "field", "target": "p0->ulUsed", "value": "p2", "text": "pstCtx->ulUsed := ulLen"}],
  "calls": [
    {
      "site": "lv5_01_ctx_filled_at_top.c:36",
      "callee": "memcpy_s",
      "model": "access",
      "args": [
        {"ptr": "pstCtx->pucBuf", "have": "ext(p0->pucBuf)"},
        "p0->ulBufLen",
        {"ptr": "pucData", "have": "ext(p1)"},
        "p2"
      ],
      "results": [{"note": "访问类模型 memcpy_s → R1、R2"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Rrc_EncodeMsg_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Rrc_EncodeMsg_TP",
  "key": "F:Rrc_EncodeMsg_TP",
  "loc": "lv5_01_ctx_filled_at_top.c:43",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv5_01_ctx_filled_at_top.c:51",
      "callee": "Enc_Encode",
      "args": [
        {
          "ptr": "&stCtx",
          "have": "12",
          "fields": {"pucBuf": {"ptr": "aucBuf", "have": "128"}, "ulBufLen": "256"}
        },
        {"ptr": "aucIe", "have": "16"},
        "16"
      ],
      "results": [
        {"req": "R1", "outcome": "L1", "report": "RPT1"},
        {"req": "R2", "outcome": "safe"},
        {"eff": 0, "value": "p0->ulUsed := 16"}
      ],
      "note": "帧内字段：stCtx.pucBuf = aucBuf（128 字节），stCtx.ulBufLen = ENC_BUF_LEN = 256"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "lv5_01_ctx_filled_at_top.c:51",
      "access": "Enc_PutBytes@lv5_01_ctx_filled_at_top.c:36",
      "object": "aucBuf (via stCtx.pucBuf)",
      "have": "128",
      "end": ["256", "256"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Rrc_EncodeMsg_TP@lv5_01_ctx_filled_at_top.c:51",
        "Enc_Encode@lv5_01_ctx_filled_at_top.c:41",
        "Enc_PutIe@lv5_01_ctx_filled_at_top.c:40",
        "Enc_PutBytes@lv5_01_ctx_filled_at_top.c:36"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 256 of 'aucBuf' (128 bytes) is accessed",
      "fp": "fa2e9f1df5e880d9"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV5-01 -v`）

```text
- Enc_PutBytes @ lv5_01_ctx_filled_at_top.c:36  调用 memcpy_s(pstCtx->pucBuf, p0->ulBufLen, pucData, p2)
    · 访问类模型 memcpy_s → R1、R2
- Enc_PutIe @ lv5_01_ctx_filled_at_top.c:40  调用 Enc_PutBytes(pstCtx, pucIe, p2)
    · Enc_PutBytes.R1：末端 p0->ulBufLen，对象 ext(p0->pucBuf) → up → 本函数 R1
    · Enc_PutBytes.R2：末端 p2，守卫 p2 <= p0->ulBufLen，对象 ext(p1) → up → 本函数 R2
    · 效果 field → p0->ulUsed := p2 
- Enc_Encode @ lv5_01_ctx_filled_at_top.c:41  调用 Enc_PutIe(pstCtx, pucIe, p2)
    · Enc_PutIe.R1：末端 p0->ulBufLen，对象 ext(p0->pucBuf) → up → 本函数 R1
    · Enc_PutIe.R2：末端 p2，守卫 p2 <= p0->ulBufLen，对象 ext(p1) → up → 本函数 R2
    · 效果 field → p0->ulUsed := p2 
- Rrc_EncodeMsg_TN @ lv5_01_ctx_filled_at_top.c:62  调用 Enc_Encode(&stCtx, aucIe, 16)
    · Enc_Encode.R1：末端 128，对象 128 → safe（最大末端 128 ≤ 128）
    · Enc_Encode.R2：末端 16，守卫在此成立，对象 16 → safe（最大末端 16 ≤ 16）
    · 效果 field → p0->ulUsed := 16 
- Rrc_EncodeMsg_TP @ lv5_01_ctx_filled_at_top.c:51  调用 Enc_Encode(&stCtx, aucIe, 16)
    · Enc_Encode.R1：末端 256，对象 128 → L1（最小末端 256 > 128，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
    · Enc_Encode.R2：末端 16，守卫在此成立，对象 16 → safe（最大末端 16 ≤ 16）
    · 效果 field → p0->ulUsed := 16 
- 期望 TP Rrc_EncodeMsg_TP pa=L1 → 回放 L1 ✓
- 期望 TN Rrc_EncodeMsg_TN pa=- → 回放 - ✓
```

### LV6-01 跨文件：extern 不完整声明的全局统计表，下标常量从三层之上传入

使用文件里只有 `extern UINT32 g_aulPortStat[];`，看不到大小。方案 B 的 `solve` 先读全部 FIR 建**全局大小表**（`port_stat_def.c` 里定义为 16 项 = 64 字节），所以最底层的 Req 直接是 `have = 64`。没有这张表时 `have` 是未知，条件停住、记账 `global_value`——这就是它在 04- 里只是"尽力"的原因（依赖全工程都进了 cdb）。

**代码**（`lv6/lv6_01_extern_tbl/port_stat_use.c`；只保留函数体，头注释略）

```c
/* port_stat_def.c */
#define MAX_PORT_NUM 16

UINT32 g_aulPortStat[MAX_PORT_NUM];

/* port_stat_use.c */
#define MAX_PORT_NUM 16

extern UINT32 g_aulPortStat[];

static void Stat_PortInc(UINT32 ulPort)     { g_aulPortStat[ulPort]++; }   /* ★1 */
static void Port_CountDown(UINT32 ulPort)   { Stat_PortInc(ulPort); }
static void Port_HandleDown(UINT32 ulPort)  { Port_CountDown(ulPort); }

void Port_OnLinkDown_TP(void) { Port_HandleDown(MAX_PORT_NUM); }
void Port_OnLinkDown_TN(void) { Port_HandleDown(MAX_PORT_NUM - 1); }
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV6-01/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Stat_PortInc` | **R1** W4 @ g_aulPortStat，末端 4*ulPort + 4；需 4*ulPort + 4 <= 64 | — | — |
| `Port_CountDown` | **R1** W4 @ g_aulPortStat，末端 4*ulPort + 4；需 4*ulPort + 4 <= 64（depth 1） | — | — |
| `Port_HandleDown` | **R1** W4 @ g_aulPortStat，末端 4*ulPort + 4；需 4*ulPort + 4 <= 64（depth 2） | — | — |
| `Port_OnLinkDown_TN` | — | — | — |
| `Port_OnLinkDown_TP` | — | — | **RPT1 L1**（depth 3） |

<details open><summary><code>Stat_PortInc.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Stat_PortInc",
  "key": "F@port_stat_use.c:Stat_PortInc",
  "tu": "port_stat_use.c",
  "loc": "port_stat_use.c:26",
  "params": ["ulPort: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "4",
      "obj": "g_aulPortStat",
      "have": "64",
      "end": ["4*p0 + 4", "4*p0 + 4"],
      "start": "4*p0",
      "bound": "exact",
      "guard": [],
      "access": {"fn": "Stat_PortInc", "at": "port_stat_use.c:26", "code": "g_aulPortStat[ulPort]++"},
      "path": ["Stat_PortInc@port_stat_use.c:26"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "absorbed": ["R4（同一位置的读）"],
      "note": "have 来自全局大小表：G:g_aulPortStat 定义在 port_stat_def.c:5，UINT32[16] = 64 字节",
      "text": "W4 @ g_aulPortStat，末端 4*ulPort + 4；需 4*ulPort + 4 <= 64"
    }
  ],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Port_OnLinkDown_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Port_OnLinkDown_TP",
  "key": "F:Port_OnLinkDown_TP",
  "tu": "port_stat_use.c",
  "loc": "port_stat_use.c:30",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "port_stat_use.c:30",
      "callee": "Port_HandleDown",
      "args": ["16"],
      "results": [{"req": "R1", "outcome": "L1", "report": "RPT1"}],
      "note": "MAX_PORT_NUM = 16"
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L1",
      "reported": true,
      "site": "port_stat_use.c:30",
      "access": "Stat_PortInc@port_stat_use.c:26",
      "object": "g_aulPortStat",
      "have": "64",
      "end": ["68", "68"],
      "bound": "exact",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Port_OnLinkDown_TP@port_stat_use.c:30",
        "Port_HandleDown@port_stat_use.c:28",
        "Port_CountDown@port_stat_use.c:27",
        "Stat_PortInc@port_stat_use.c:26"
      ],
      "msg": "Definite out-of-bound write through a 3-level call chain: up to byte 68 of 'g_aulPortStat' (64 bytes) is accessed",
      "fp": "e6c2f36be69dcb7c"
    }
  ],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV6-01 -v`）

```text
- Port_CountDown @ port_stat_use.c:27  调用 Stat_PortInc(p0)
    · Stat_PortInc.R1：末端 4*p0 + 4，对象 64 → up → 本函数 R1
- Port_HandleDown @ port_stat_use.c:28  调用 Port_CountDown(p0)
    · Port_CountDown.R1：末端 4*p0 + 4，对象 64 → up → 本函数 R1
- Port_OnLinkDown_TN @ port_stat_use.c:31  调用 Port_HandleDown(15)
    · Port_HandleDown.R1：末端 64，对象 64 → safe（最大末端 64 ≤ 64）
- Port_OnLinkDown_TP @ port_stat_use.c:30  调用 Port_HandleDown(16)
    · Port_HandleDown.R1：末端 68，对象 64 → L1（最小末端 68 > 64，对所有取值都越界） → RPT1（depth 3, eff_depth 0，报）
- 期望 TP Port_OnLinkDown_TP pa=L1 → 回放 L1 ✓
- 期望 TN Port_OnLinkDown_TN pa=- → 回放 - ✓
```

### LV7-04 循环中指针步进拷贝：个数来自三层之上，比目的缓冲区大

**年内记账的样子**：`*pucOut++` 的偏移在循环头被 widening 成 `[0, +inf)`，和 `ulCnt` 的关系丢了。widening 出来的无穷上界不算代码给出的界，所以**不生成 Req**（否则 TN 也会报），只在 `opens` 里记一条 `pointer_stepping`。上层函数的摘要因此是空的，链顶不报。2027 记住"偏移 = 已迭代次数 ∈ [0, ulCnt − 1]"之后，这里会出现 `ulCnt >= 1 ⇒ 需 ulCnt <= ext(pucOut)`。

**代码**（`lv7/lv7_04_pointer_stepping.c`；只保留函数体，头注释略）

```c
static void Buf_CopyStep(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt)
{
    while (ulCnt-- > 0) {
        *pucOut++ = *pucIn++;                                 /* ★1 */
    }
}

static void Reg_Snapshot(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt)  { Buf_CopyStep(pucOut, pucIn, ulCnt); }
static void Om_CollectReg(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt) { Reg_Snapshot(pucOut, pucIn, ulCnt); }

void Om_DumpReg_TP(void)
{
    UINT8 aucIn[32] = { 0 };
    UINT8 aucOut[16];
    Om_CollectReg(aucOut, aucIn, sizeof(aucIn));
}

void Om_DumpReg_TN(void)
{
    UINT8 aucIn[32] = { 0 };
    UINT8 aucOut[16];
    Om_CollectReg(aucOut, aucIn, sizeof(aucOut));
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV7-04/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Buf_CopyStep` | — | — | 账本 `pointer_stepping` |
| `Reg_Snapshot` | — | — | — |
| `Om_CollectReg` | — | — | — |
| `Om_DumpReg_TN` | — | — | — |
| `Om_DumpReg_TP` | — | — | — |

<details open><summary><code>Buf_CopyStep.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Buf_CopyStep",
  "key": "F@lv7_04_pointer_stepping.c:Buf_CopyStep",
  "loc": "lv7_04_pointer_stepping.c:24",
  "params": ["pucOut: UINT8*", "pucIn: UINT8*", "ulCnt: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [],
  "reps": [],
  "opens": [
    {
      "reason": "pointer_stepping",
      "at": "lv7_04_pointer_stepping.c:27",
      "detail": "pucOut、pucIn 的偏移在循环里 widening 到 [0, +inf)，与 ulCnt 的关系丢失；不算代码给出的界，不生成条件"
    }
  ]
}
```
</details>

<details open><summary><code>Om_DumpReg_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Om_DumpReg_TP",
  "key": "F:Om_DumpReg_TP",
  "loc": "lv7_04_pointer_stepping.c:34",
  "params": [],
  "status": "analyzed",
  "reqs": [],
  "effs": [],
  "calls": [
    {
      "site": "lv7_04_pointer_stepping.c:38",
      "callee": "Om_CollectReg",
      "args": [{"ptr": "aucOut", "have": "16"}, {"ptr": "aucIn", "have": "32"}, "32"],
      "results": [{"note": "被调者没有条件 → 不报；漏报在 Buf_CopyStep 的账本里"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

**回放**（`python3 replay.py LV7-04 -v`）

```text
- Reg_Snapshot @ lv7_04_pointer_stepping.c:31  调用 Buf_CopyStep(pucOut, pucIn, p2)
    · 被调者的摘要里没有条件 → 本函数也没有
- Om_CollectReg @ lv7_04_pointer_stepping.c:32  调用 Reg_Snapshot(pucOut, pucIn, p2)
    · 被调者的摘要里没有条件 → 本函数也没有
- Om_DumpReg_TN @ lv7_04_pointer_stepping.c:45  调用 Om_CollectReg(aucOut, aucIn, 16)
    · 被调者没有条件 → 不报；漏报在 Buf_CopyStep 的账本里
- Om_DumpReg_TP @ lv7_04_pointer_stepping.c:38  调用 Om_CollectReg(aucOut, aucIn, 32)
    · 被调者没有条件 → 不报；漏报在 Buf_CopyStep 的账本里
- 期望 TP Om_DumpReg_TP pa=记账 → 回放 - ✓
- 期望 TN Om_DumpReg_TN pa=记账 → 回放 - ✓
```

### LV8-01 TLV 解析循环：每轮的校验漏算了 2 字节 TLV 头（读越界）

**数据驱动的步长也能做**：`ulLen` 读自缓冲区（无约束），`ulOff` 在循环里被 widening，两者的区间都没用；有用的是本轮的校验给出的**关系** `ulOff + ulLen <= ulTotal`（06- A-13 定义式 + A-14 守卫），访问末端 `ulOff + 2 + ulLen` 因此 `<= ulTotal + 2`。最小末端 2（第一轮、长度 0）。循环至少执行一次需要 `ulTotal >= 2`，作为守卫带着。读 `pucBuf[ulOff + 1]` 的那条被更强的条件覆盖。链顶自己的 `memcpy_s(aucMsg, 64, pucIn, 64)` 在源上要求入口形参至少 64 字节 —— 入口，记账。

**代码**（`lv8/lv8_01_tlv_relative_check.c`；只保留函数体，头注释略）

```c
static void Tlv_ParseAll(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
{
    UINT32 ulOff = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + ulLen > ulTotal) {                        /* 漏了 + 2 */
            break;
        }
        (void)memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen);   /* ★1 */
        ulOff += 2 + ulLen;
    }
}

static void Tlv_ParseAll_OK(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
{
    UINT32 ulOff = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + 2 + ulLen > ulTotal) {
            break;
        }
        (void)memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen);
        ulOff += 2 + ulLen;
    }
}

static void Msg_ParseBody(const UINT8 *p, UINT32 n, UINT8 *o)    { Tlv_ParseAll(p, n, o); }
static void Msg_ParseBody_OK(const UINT8 *p, UINT32 n, UINT8 *o) { Tlv_ParseAll_OK(p, n, o); }
static void Om_HandleCfg(const UINT8 *p, UINT32 n, UINT8 *o)     { Msg_ParseBody(p, n, o); }
static void Om_HandleCfg_OK(const UINT8 *p, UINT32 n, UINT8 *o)  { Msg_ParseBody_OK(p, n, o); }

void Om_OnCfgMsg_TP(const UINT8 *pucIn)
{
    UINT8 aucMsg[64];
    UINT8 aucOut[256];
    (void)memcpy_s(aucMsg, sizeof(aucMsg), pucIn, sizeof(aucMsg));
    Om_HandleCfg(aucMsg, sizeof(aucMsg), aucOut);
}

void Om_OnCfgMsg_TN(const UINT8 *pucIn)
{
    UINT8 aucMsg[64];
    UINT8 aucOut[256];
    (void)memcpy_s(aucMsg, sizeof(aucMsg), pucIn, sizeof(aucMsg));
    Om_HandleCfg_OK(aucMsg, sizeof(aucMsg), aucOut);
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV8-01/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Tlv_ParseAll` | **R1** ulTotal >= 2 ⇒ RulLen @ *pucBuf，末端 [2, ulTotal + 2]；需 ulTotal + 2 <= ext(pucBuf)<br>**R2** ulTotal >= 2 ⇒ W256 @ *pucOut，末端 256；需 256 <= ext(pucOut) | — | — |
| `Tlv_ParseAll_OK` | **R1** ulTotal >= 2 ⇒ RulLen @ *pucBuf，末端 [2, ulTotal]；需 ulTotal <= ext(pucBuf)<br>**R2** ulTotal >= 2 ⇒ W256 @ *pucOut，末端 256；需 256 <= ext(pucOut) | — | — |
| `Msg_ParseBody` | **R1** n >= 2 ⇒ RulLen @ *p，末端 [2, n + 2]；需 n + 2 <= ext(p)（depth 1）<br>**R2** n >= 2 ⇒ W256 @ *o，末端 256；需 256 <= ext(o)（depth 1） | — | — |
| `Msg_ParseBody_OK` | **R1** n >= 2 ⇒ RulLen @ *p，末端 [2, n]；需 n <= ext(p)（depth 1）<br>**R2** n >= 2 ⇒ W256 @ *o，末端 256；需 256 <= ext(o)（depth 1） | — | — |
| `Om_HandleCfg` | **R1** n >= 2 ⇒ RulLen @ *p，末端 [2, n + 2]；需 n + 2 <= ext(p)（depth 2）<br>**R2** n >= 2 ⇒ W256 @ *o，末端 256；需 256 <= ext(o)（depth 2） | — | — |
| `Om_HandleCfg_OK` | **R1** n >= 2 ⇒ RulLen @ *p，末端 [2, n]；需 n <= ext(p)（depth 2）<br>**R2** n >= 2 ⇒ W256 @ *o，末端 256；需 256 <= ext(o)（depth 2） | — | — |
| `Om_OnCfgMsg_TN` | **R1** R64 @ *pucIn，末端 64；需 64 <= ext(pucIn) | — | 账本 `entry_unproven`（R1） |
| `Om_OnCfgMsg_TP` | **R1** R64 @ *pucIn，末端 64；需 64 <= ext(pucIn) | — | **RPT1 L2**（depth 3）<br>账本 `entry_unproven`（R1） |

<details open><summary><code>Tlv_ParseAll.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Tlv_ParseAll",
  "key": "F@lv8_01_tlv_relative_check.c:Tlv_ParseAll",
  "loc": "lv8_01_tlv_relative_check.c:26",
  "params": ["pucBuf: UINT8*", "ulTotal: UINT32", "pucOut: UINT8*"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "R",
      "bytes": "ulLen",
      "obj": "*pucBuf",
      "have": "ext(p0)",
      "end": ["2", "p1 + 2"],
      "start": "2",
      "bound": "check",
      "guard": ["p1 >= 2"],
      "access": {
        "fn": "Tlv_ParseAll",
        "at": "lv8_01_tlv_relative_check.c:34",
        "code": "memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen)"
      },
      "path": ["Tlv_ParseAll@lv8_01_tlv_relative_check.c:34"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "absorbed": ["R1 @ pucBuf + ulOff + 1（lv8_01_tlv_relative_check.c:30，末端 <= ulTotal）"],
      "note": "末端 ulOff + 2 + ulLen；本轮校验给出 ulOff + ulLen <= ulTotal",
      "text": "ulTotal >= 2 ⇒ RulLen @ *pucBuf，末端 [2, ulTotal + 2]；需 ulTotal + 2 <= ext(pucBuf)"
    },
    {
      "id": "R2",
      "kind": "W",
      "bytes": "256",
      "obj": "*pucOut",
      "have": "ext(p2)",
      "end": ["256", "256"],
      "start": "0",
      "bound": "exact",
      "guard": ["p1 >= 2"],
      "access": {
        "fn": "Tlv_ParseAll",
        "at": "lv8_01_tlv_relative_check.c:34",
        "code": "memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen)"
      },
      "path": ["Tlv_ParseAll@lv8_01_tlv_relative_check.c:34"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "memcpy_s 的目的：destMax 256",
      "text": "ulTotal >= 2 ⇒ W256 @ *pucOut，末端 256；需 256 <= ext(pucOut)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv8_01_tlv_relative_check.c:34",
      "callee": "memcpy_s",
      "model": "access",
      "args": [
        {"ptr": "pucOut", "have": "ext(p2)"},
        "256",
        {"ptr": "pucBuf + ulOff + 2", "have": "ext(p0) - ulOff - 2"},
        "ulLen"
      ],
      "results": [{"note": "访问类模型 memcpy_s → R1（源）、R2（目的）"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Om_OnCfgMsg_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Om_OnCfgMsg_TP",
  "key": "F:Om_OnCfgMsg_TP",
  "loc": "lv8_01_tlv_relative_check.c:57",
  "params": ["pucIn: UINT8*"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "R",
      "bytes": "64",
      "obj": "*pucIn",
      "have": "ext(p0)",
      "end": ["64", "64"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Om_OnCfgMsg_TP",
        "at": "lv8_01_tlv_relative_check.c:61",
        "code": "memcpy_s(aucMsg, sizeof(aucMsg), pucIn, sizeof(aucMsg))"
      },
      "path": ["Om_OnCfgMsg_TP@lv8_01_tlv_relative_check.c:61"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "text": "R64 @ *pucIn，末端 64；需 64 <= ext(pucIn)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv8_01_tlv_relative_check.c:61",
      "callee": "memcpy_s",
      "model": "access",
      "args": [{"ptr": "aucMsg", "have": "64"}, "64", {"ptr": "pucIn", "have": "ext(p0)"}, "64"],
      "results": [{"note": "目的 64 <= 64 当场安全；源生成本函数的 R1（64 <= ext(pucIn)）"}]
    },
    {
      "site": "lv8_01_tlv_relative_check.c:62",
      "callee": "Om_HandleCfg",
      "args": [{"ptr": "aucMsg", "have": "64"}, "64", {"ptr": "aucOut", "have": "256"}],
      "results": [{"req": "R1", "outcome": "L2", "report": "RPT1"}, {"req": "R2", "outcome": "safe"}]
    }
  ],
  "reps": [
    {
      "id": "RPT1",
      "level": "L2",
      "reported": true,
      "site": "lv8_01_tlv_relative_check.c:62",
      "access": "Tlv_ParseAll@lv8_01_tlv_relative_check.c:34",
      "object": "aucMsg",
      "have": "64",
      "end": ["2", "66"],
      "bound": "check",
      "depth": 3,
      "eff_depth": 0,
      "chain": [
        "Om_OnCfgMsg_TP@lv8_01_tlv_relative_check.c:62",
        "Om_HandleCfg@lv8_01_tlv_relative_check.c:54",
        "Msg_ParseBody@lv8_01_tlv_relative_check.c:52",
        "Tlv_ParseAll@lv8_01_tlv_relative_check.c:34"
      ],
      "msg": "Possible out-of-bound read through a 3-level call chain: up to byte 66 of 'aucMsg' (64 bytes) may be accessed; the bound comes from a check in Tlv_ParseAll",
      "fp": "6e77b710003251cb"
    }
  ],
  "opens": [
    {
      "reason": "entry_unproven",
      "at": "lv8_01_tlv_relative_check.c:61",
      "detail": "入口形参 pucIn 要求至少 64 字节；没有调用方能证明",
      "req": "R1"
    }
  ]
}
```
</details>

**回放**（`python3 replay.py LV8-01 -v`）

```text
- Tlv_ParseAll @ lv8_01_tlv_relative_check.c:34  调用 memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen)
    · 访问类模型 memcpy_s → R1（源）、R2（目的）
- Tlv_ParseAll_OK @ lv8_01_tlv_relative_check.c:47  调用 memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen)
    · 访问类模型 memcpy_s → R1（源）、R2（目的）
- Msg_ParseBody @ lv8_01_tlv_relative_check.c:52  调用 Tlv_ParseAll(p, p1, o)
    · Tlv_ParseAll.R1：末端 [2, p1 + 2]，守卫 p1 >= 2，对象 ext(p0) → up → 本函数 R1
    · Tlv_ParseAll.R2：末端 256，守卫 p1 >= 2，对象 ext(p2) → up → 本函数 R2
- Msg_ParseBody_OK @ lv8_01_tlv_relative_check.c:53  调用 Tlv_ParseAll_OK(p, p1, o)
    · Tlv_ParseAll_OK.R1：末端 [2, p1]，守卫 p1 >= 2，对象 ext(p0) → up → 本函数 R1
    · Tlv_ParseAll_OK.R2：末端 256，守卫 p1 >= 2，对象 ext(p2) → up → 本函数 R2
- Om_HandleCfg @ lv8_01_tlv_relative_check.c:54  调用 Msg_ParseBody(p, p1, o)
    · Msg_ParseBody.R1：末端 [2, p1 + 2]，守卫 p1 >= 2，对象 ext(p0) → up → 本函数 R1
    · Msg_ParseBody.R2：末端 256，守卫 p1 >= 2，对象 ext(p2) → up → 本函数 R2
- Om_HandleCfg_OK @ lv8_01_tlv_relative_check.c:55  调用 Msg_ParseBody_OK(p, p1, o)
    · Msg_ParseBody_OK.R1：末端 [2, p1]，守卫 p1 >= 2，对象 ext(p0) → up → 本函数 R1
    · Msg_ParseBody_OK.R2：末端 256，守卫 p1 >= 2，对象 ext(p2) → up → 本函数 R2
- Om_OnCfgMsg_TN @ lv8_01_tlv_relative_check.c:69  调用 memcpy_s(aucMsg, 64, pucIn, 64)
    · 目的 64 <= 64 当场安全；源生成本函数的 R1（64 <= ext(pucIn)）
- Om_OnCfgMsg_TN @ lv8_01_tlv_relative_check.c:70  调用 Om_HandleCfg_OK(aucMsg, 64, aucOut)
    · Om_HandleCfg_OK.R1：末端 [2, 64]，守卫在此成立，对象 64 → safe（最大末端 64 ≤ 64）
    · Om_HandleCfg_OK.R2：末端 256，守卫在此成立，对象 256 → safe（最大末端 256 ≤ 256）
- Om_OnCfgMsg_TN 是入口（没有调用方）：R1 到此为止 → 记账 entry_unproven
- Om_OnCfgMsg_TP @ lv8_01_tlv_relative_check.c:61  调用 memcpy_s(aucMsg, 64, pucIn, 64)
    · 目的 64 <= 64 当场安全；源生成本函数的 R1（64 <= ext(pucIn)）
- Om_OnCfgMsg_TP @ lv8_01_tlv_relative_check.c:62  调用 Om_HandleCfg(aucMsg, 64, aucOut)
    · Om_HandleCfg.R1：末端 [2, 66]，守卫在此成立，对象 64 → L2（最大末端 66 > 64（上界来自 check），最小末端 2 不越界） → RPT1（depth 3, eff_depth 0，报）
    · Om_HandleCfg.R2：末端 256，守卫在此成立，对象 256 → safe（最大末端 256 ≤ 256）
- Om_OnCfgMsg_TP 是入口（没有调用方）：R1 到此为止 → 记账 entry_unproven
- 期望 TP Om_OnCfgMsg_TP pa=L2 → 回放 L2 ✓
- 期望 TN Om_OnCfgMsg_TN pa=- → 回放 - ✓
```

### LV9-04 BSP 驱动接口只有声明：按 ulLen 往 pucBuf 读寄存器，调用方缓冲区 16、长度 32

**断链的样子**：`Drv_ReadReg` 没有函数体、模型表里也没有。`pucBuf`、`ulLen` 流进了它，条件在 `Chip_ReadStatus` 这一层断掉，账本记 `unknown_callee`，上层摘要为空，链顶不报。给模型表补一行（来源可以是 AI 读头文件注释生成的草稿，人工确认后生效）：`{ name: Drv_ReadReg, conds: [ { base: p1, bytes: "p2", kind: W } ] }`，`Chip_ReadStatus` 就会有 `R1 需 ulLen <= ext(pucBuf)`，TP 变 L1。

**代码**（`lv9/lv9_04_external_driver.c`；只保留函数体，头注释略）

```c
/* BSP 库：从 ulRegBase 开始读 ulLen 字节到 pucBuf。pucBuf 至少 ulLen 字节，由调用者保证。 */
UINT32 Drv_ReadReg(UINT32 ulRegBase, UINT8 *pucBuf, UINT32 ulLen);

static UINT32 Chip_ReadStatus(UINT8 *pucBuf, UINT32 ulLen) { return Drv_ReadReg(0x40000000u, pucBuf, ulLen); }   /* ★1 */
static UINT32 Om_CollectChip(UINT8 *pucBuf, UINT32 ulLen)  { return Chip_ReadStatus(pucBuf, ulLen); }
static UINT32 Om_ReadAll(UINT8 *pucBuf, UINT32 ulLen)      { return Om_CollectChip(pucBuf, ulLen); }

UINT32 Om_ReadChip_TP(void)
{
    UINT8 aucStatus[16];
    return Om_ReadAll(aucStatus, 32);
}

UINT32 Om_ReadChip_TN(void)
{
    UINT8 aucStatus[16];
    return Om_ReadAll(aucStatus, sizeof(aucStatus));
}
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV9-04/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Chip_ReadStatus` | — | ret ∈ [0, 4294967295] | 账本 `unknown_callee` |
| `Om_CollectChip` | — | ret ∈ [0, 4294967295] | — |
| `Om_ReadAll` | — | ret ∈ [0, 4294967295] | — |
| `Om_ReadChip_TN` | — | — | — |
| `Om_ReadChip_TP` | — | — | — |

<details open><summary><code>Chip_ReadStatus.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Chip_ReadStatus",
  "key": "F@lv9_04_external_driver.c:Chip_ReadStatus",
  "loc": "lv9_04_external_driver.c:25",
  "params": ["pucBuf: UINT8*", "ulLen: UINT32"],
  "status": "analyzed",
  "reqs": [],
  "effs": [{"kind": "ret_int", "range": ["0", "4294967295"], "bound": "type", "text": "ret ∈ [0, 4294967295]"}],
  "calls": [
    {
      "site": "lv9_04_external_driver.c:25",
      "callee": "Drv_ReadReg",
      "model": "unknown",
      "args": ["0x40000000", {"ptr": "pucBuf", "have": "ext(p0)"}, "p1"],
      "results": [{"note": "无函数体、无模型：返回值未知；经指针实参可达的格失效；记账 unknown_callee"}]
    }
  ],
  "reps": [],
  "opens": [
    {
      "reason": "unknown_callee",
      "at": "lv9_04_external_driver.c:25",
      "detail": "Drv_ReadReg 只有声明（BSP 二进制库）；pucBuf / ulLen 流进了它。补模型后生成 R1：ulLen <= ext(pucBuf)"
    }
  ]
}
```
</details>

**回放**（`python3 replay.py LV9-04 -v`）

```text
- Chip_ReadStatus @ lv9_04_external_driver.c:25  调用 Drv_ReadReg(0x40000000, pucBuf, p1)
    · 无函数体、无模型：返回值未知；经指针实参可达的格失效；记账 unknown_callee
- Om_CollectChip @ lv9_04_external_driver.c:26  调用 Chip_ReadStatus(pucBuf, p1)
    · 被调者没有条件
- Om_ReadAll @ lv9_04_external_driver.c:27  调用 Om_CollectChip(pucBuf, p1)
    · 被调者没有条件
- Om_ReadChip_TN @ lv9_04_external_driver.c:38  调用 Om_ReadAll(aucStatus, 16)
    · 被调者没有条件 → 不报；漏报在 Chip_ReadStatus 的账本里
- Om_ReadChip_TP @ lv9_04_external_driver.c:32  调用 Om_ReadAll(aucStatus, 32)
    · 被调者没有条件 → 不报；漏报在 Chip_ReadStatus 的账本里
- 期望 TP Om_ReadChip_TP pa=记账 → 回放 - ✓
- 期望 TN Om_ReadChip_TN pa=记账 → 回放 - ✓
```

### LV10-04 外部消息里的长度一路下传到 memcpy_s，没有任何校验（TN：框架在注册时声明了最大长度）

**类型范围不算界**：链顶 `ulLen = pstMsg->usMsgLen`，只有 `UINT16` 的类型范围 `[0, 65535]`。对象都已知（`g_aucCfg` 256、`aucBody` 1024），但上界只来自类型，按 F-12 不报 L2；链顶是入口（消息处理函数），条件到此为止 → 记账 `type_range_only`。TP 与 TN 在代码里一模一样，区别在注册调用 `OM_RegMsg(…, 256)` 和平台的投递校验——那是 AI + 程序分析的活（候选研判）。

**代码**（`lv10/lv10_04_unchecked_ext_len.c`；只保留函数体，头注释略）

```c
#define CFG_LEN 256

typedef struct {
    UINT16 usMsgId;
    UINT16 usMsgLen;
    UINT8  aucBody[1024];
} OM_MSG_S;

void OM_RegMsg(UINT32 ulMsgId, void (*pfnEntry)(const OM_MSG_S *), UINT32 ulMaxLen);   /* 平台：投递前按 ulMaxLen 校验 */

static UINT8 g_aucCfg[CFG_LEN];

static void Cfg_Save(const UINT8 *pucBody, UINT32 ulLen)
{
    (void)memcpy_s(g_aucCfg, ulLen, pucBody, ulLen);         /* ★1 */
}

static void Cfg_Store(const UINT8 *p, UINT32 n)  { Cfg_Save(p, n); }
static void Om_SaveCfg(const UINT8 *p, UINT32 n) { Cfg_Store(p, n); }

void Om_OnSaveCfg_TP(const OM_MSG_S *pstMsg) { Om_SaveCfg(pstMsg->aucBody, pstMsg->usMsgLen); }
void Om_OnSaveCfg_TN(const OM_MSG_S *pstMsg) { Om_SaveCfg(pstMsg->aucBody, pstMsg->usMsgLen); }

void Om_ModInit(void) { OM_RegMsg(0x3001, Om_OnSaveCfg_TN, CFG_LEN); }
```

**摘要一览**（自底向上；每行一个函数，完整 JSON 在 `cases/summaries/LV10-04/<函数名>.json`）

| 函数 | 前置条件 Req | 效果 Eff | 判定 / 账本 |
|---|---|---|---|
| `Cfg_Save` | **R1** WulLen @ g_aucCfg，末端 ulLen；需 ulLen <= 256<br>**R2** RulLen @ *pucBody，末端 ulLen；需 ulLen <= ext(pucBody) | — | — |
| `Om_ModInit` | — | — | 账本 `unknown_callee` |
| `Cfg_Store` | **R1** Wn @ g_aucCfg，末端 n；需 n <= 256（depth 1）<br>**R2** Rn @ *p，末端 n；需 n <= ext(p)（depth 1） | — | — |
| `Om_SaveCfg` | **R1** Wn @ g_aucCfg，末端 n；需 n <= 256（depth 2）<br>**R2** Rn @ *p，末端 n；需 n <= ext(p)（depth 2） | — | — |
| `Om_OnSaveCfg_TN` | **R1** WpstMsg->usMsgLen @ g_aucCfg，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 256（depth 3）<br>**R2** RpstMsg->usMsgLen @ pstMsg->aucBody，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 1024（depth 3） | — | 账本 `type_range_only`（R1）<br>账本 `type_range_only`（R2） |
| `Om_OnSaveCfg_TP` | **R1** WpstMsg->usMsgLen @ g_aucCfg，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 256（depth 3）<br>**R2** RpstMsg->usMsgLen @ pstMsg->aucBody，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 1024（depth 3） | — | 账本 `type_range_only`（R1）<br>账本 `type_range_only`（R2） |

<details open><summary><code>Cfg_Save.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Cfg_Save",
  "key": "F@lv10_04_unchecked_ext_len.c:Cfg_Save",
  "loc": "lv10_04_unchecked_ext_len.c:34",
  "params": ["pucBody: UINT8*", "ulLen: UINT32"],
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "p1",
      "obj": "g_aucCfg",
      "have": "256",
      "end": ["p1", "p1"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Cfg_Save",
        "at": "lv10_04_unchecked_ext_len.c:36",
        "code": "memcpy_s(g_aucCfg, ulLen, pucBody, ulLen)"
      },
      "path": ["Cfg_Save@lv10_04_unchecked_ext_len.c:36"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "目的：destMax = ulLen",
      "text": "WulLen @ g_aucCfg，末端 ulLen；需 ulLen <= 256"
    },
    {
      "id": "R2",
      "kind": "R",
      "bytes": "p1",
      "obj": "*pucBody",
      "have": "ext(p0)",
      "end": ["p1", "p1"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Cfg_Save",
        "at": "lv10_04_unchecked_ext_len.c:36",
        "code": "memcpy_s(g_aucCfg, ulLen, pucBody, ulLen)"
      },
      "path": ["Cfg_Save@lv10_04_unchecked_ext_len.c:36"],
      "depth": 0,
      "eff_depth": 0,
      "blind": false,
      "note": "源：count = destMax，守卫 count <= destMax 恒真，已去掉",
      "text": "RulLen @ *pucBody，末端 ulLen；需 ulLen <= ext(pucBody)"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv10_04_unchecked_ext_len.c:36",
      "callee": "memcpy_s",
      "model": "access",
      "args": [{"ptr": "g_aucCfg", "have": "256"}, "p1", {"ptr": "pucBody", "have": "ext(p0)"}, "p1"],
      "results": [{"note": "访问类模型 memcpy_s → R1、R2"}]
    }
  ],
  "reps": [],
  "opens": []
}
```
</details>

<details open><summary><code>Om_OnSaveCfg_TP.json</code></summary>

```json
{
  "schema": "oobsum-summary/1",
  "fn": "Om_OnSaveCfg_TP",
  "key": "F:Om_OnSaveCfg_TP",
  "loc": "lv10_04_unchecked_ext_len.c:42",
  "params": ["pstMsg: OM_MSG_S*"],
  "fields": {"p0->usMsgLen": {"off": 2, "size": 2, "type": "UINT16"}},
  "status": "analyzed",
  "reqs": [
    {
      "id": "R1",
      "kind": "W",
      "bytes": "p0->usMsgLen",
      "obj": "g_aucCfg",
      "have": "256",
      "end": ["p0->usMsgLen", "p0->usMsgLen"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Cfg_Save",
        "at": "lv10_04_unchecked_ext_len.c:36",
        "code": "memcpy_s(g_aucCfg, ulLen, pucBody, ulLen)"
      },
      "path": [
        "Om_OnSaveCfg_TP@lv10_04_unchecked_ext_len.c:42",
        "Om_SaveCfg@lv10_04_unchecked_ext_len.c:40",
        "Cfg_Store@lv10_04_unchecked_ext_len.c:39",
        "Cfg_Save@lv10_04_unchecked_ext_len.c:36"
      ],
      "depth": 3,
      "eff_depth": 0,
      "blind": false,
      "text": "WpstMsg->usMsgLen @ g_aucCfg，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 256"
    },
    {
      "id": "R2",
      "kind": "R",
      "bytes": "p0->usMsgLen",
      "obj": "pstMsg->aucBody",
      "have": "1024",
      "end": ["p0->usMsgLen", "p0->usMsgLen"],
      "start": "0",
      "bound": "exact",
      "guard": [],
      "access": {
        "fn": "Cfg_Save",
        "at": "lv10_04_unchecked_ext_len.c:36",
        "code": "memcpy_s(g_aucCfg, ulLen, pucBody, ulLen)"
      },
      "path": [
        "Om_OnSaveCfg_TP@lv10_04_unchecked_ext_len.c:42",
        "Om_SaveCfg@lv10_04_unchecked_ext_len.c:40",
        "Cfg_Store@lv10_04_unchecked_ext_len.c:39",
        "Cfg_Save@lv10_04_unchecked_ext_len.c:36"
      ],
      "depth": 3,
      "eff_depth": 0,
      "blind": false,
      "text": "RpstMsg->usMsgLen @ pstMsg->aucBody，末端 pstMsg->usMsgLen；需 pstMsg->usMsgLen <= 1024"
    }
  ],
  "effs": [],
  "calls": [
    {
      "site": "lv10_04_unchecked_ext_len.c:42",
      "callee": "Om_SaveCfg",
      "args": [{"ptr": "pstMsg->aucBody", "have": "1024"}, "p0->usMsgLen"],
      "results": [{"req": "R1", "outcome": "up", "as": "R1"}, {"req": "R2", "outcome": "up", "as": "R2"}],
      "note": "aucBody 是成员数组，子对象 1024 字节；usMsgLen 只有 UINT16 的类型范围"
    }
  ],
  "reps": [],
  "opens": [
    {
      "reason": "type_range_only",
      "at": "lv10_04_unchecked_ext_len.c:42",
      "detail": "入口：usMsgLen 只有类型范围 [0, 65535]，最大会写到 65535 > 256；不是代码给出的界，不报 L2，留给 AI 研判",
      "req": "R1"
    },
    {
      "reason": "type_range_only",
      "at": "lv10_04_unchecked_ext_len.c:42",
      "detail": "同上：最大读到 65535 > 1024",
      "req": "R2"
    }
  ]
}
```
</details>

**回放**（`python3 replay.py LV10-04 -v`）

```text
- Cfg_Save @ lv10_04_unchecked_ext_len.c:36  调用 memcpy_s(g_aucCfg, p1, pucBody, p1)
    · 访问类模型 memcpy_s → R1、R2
- Om_ModInit @ lv10_04_unchecked_ext_len.c:45  调用 OM_RegMsg(0x3001, …, 256)
    · 外部函数、无模型：登记了 Om_OnSaveCfg_TN 与最大长度 256 —— 程序分析不理解，AI 研判时读它
- Cfg_Store @ lv10_04_unchecked_ext_len.c:39  调用 Cfg_Save(p, p1)
    · Cfg_Save.R1：末端 p1，对象 256 → up → 本函数 R1
    · Cfg_Save.R2：末端 p1，对象 ext(p0) → up → 本函数 R2
- Om_SaveCfg @ lv10_04_unchecked_ext_len.c:40  调用 Cfg_Store(p, p1)
    · Cfg_Store.R1：末端 p1，对象 256 → up → 本函数 R1
    · Cfg_Store.R2：末端 p1，对象 ext(p0) → up → 本函数 R2
- Om_OnSaveCfg_TN @ lv10_04_unchecked_ext_len.c:43  调用 Om_SaveCfg(pstMsg->aucBody, p0->usMsgLen)
    · Om_SaveCfg.R1：末端 p0->usMsgLen，对象 256 → up → 本函数 R1
    · Om_SaveCfg.R2：末端 p0->usMsgLen，对象 1024 → up → 本函数 R2
- Om_OnSaveCfg_TN 是入口（没有调用方）：R1 到此为止 → 记账 type_range_only
- Om_OnSaveCfg_TN 是入口（没有调用方）：R2 到此为止 → 记账 type_range_only
- Om_OnSaveCfg_TP @ lv10_04_unchecked_ext_len.c:42  调用 Om_SaveCfg(pstMsg->aucBody, p0->usMsgLen)
    · Om_SaveCfg.R1：末端 p0->usMsgLen，对象 256 → up → 本函数 R1
    · Om_SaveCfg.R2：末端 p0->usMsgLen，对象 1024 → up → 本函数 R2
- Om_OnSaveCfg_TP 是入口（没有调用方）：R1 到此为止 → 记账 type_range_only
- Om_OnSaveCfg_TP 是入口（没有调用方）：R2 到此为止 → 记账 type_range_only
- 期望 TP Om_OnSaveCfg_TP pa=记账 → 回放 - ✓
- 期望 TN Om_OnSaveCfg_TN pa=记账 → 回放 - ✓
```
