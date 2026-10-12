# 摘要怎么设计、怎么串联起来查多层越界：用例、摘要形式与备选方案

> 调研包 v3.11 · 2026-10-12 · 依据：本包已有实测 +【推断】（设计判断）；用例按"真实业务代码里概率最大的写法"构造，后续由使用方补真实业务用例。
>
> 配套：**67 个用例**在 `cases/`（每个文件头写了场景签名、期望、**每个函数的摘要**和**串联步骤**），一览表 `cases/INDEX.md`，
> 工具 `cases/run.py`（语法检查、生成一览、用 Infer 当标尺对照）。场景分级见 `04-scenario-tiers.md`，设计细节见 `01-architecture.md`。

---

## 0. 先回答你问的几件事

1. **摘要到底留什么、扔什么？** 不是逐个变量"挑"出来的。摘要回答一个**固定的问题**：
   "调用方给我什么样的输入，我（连同我调用的函数）里的内存访问才安全；我给调用方留下了哪些和内存有关的事实。"
   一个量出现在摘要里，当且仅当它经数据依赖、或经支配访问的校验，流进了某个访问的**基址、偏移、长度**，或者流进了某个**对外效果**。
   日志调用不出现，不是因为有一条"删日志"的规则，而是它什么都没流进去（§1.2 有一个 20 行函数只剩 2 条条件的例子）。
2. **摘要生成是不是可判定的？** 精确的摘要不可判定（Rice 定理）；我们算的是近似，**计算一定终止**（widening、迭代上限、全函数性）。
   所以问题不在"算不算得出"，在"算出来的有多准"。处理办法是三值：证明安全 / 证明越界（L1、L2）/ 判不了。
   **判不了就往上传，到入口还判不了就记账，绝不当成"安全"**——每一条丢失都有原因可查（§1.3）。
3. **能结合切片吗？** 能，而且摘要本质上就是"访问点的后向切片，投影到函数接口上，再用抽象域压缩"。
   有三种结合方式：隐式切片、先算依赖摘要的两段式、显式跨函数切片给 AI（§1.4）。
4. **摘要一定是单函数的吗？** 默认单函数，但不是唯一选择：递归按强连通分量算，薄转发函数可以折叠，
   必要时按调用上下文分开，模块边界只导出接口摘要（§1.5）。
5. **不跨函数能拿到什么、要跨多少层？** 局部数组加常量、成员类型给出的大小、函数自己校验过的访问，都不跨函数就定了——
   **真实代码里多数函数的条件集是空的**。跨多少层不预设：条件一直往上走，直到被判定、变得无用、或到达入口（§1.5）。
6. **风险在哪？** 不在终止性，也不在规模（每个函数只分析一次）。在三处：别名和未知调用导致**效果漏算**、**条件数量膨胀**、
   抽象域**表达不了的关系**。每一处都有账可查（§1.6）。
7. **怎么串联起来完成检测？** 调用点上做五步代换（§3），每一级的组合方式在 §4，67 个用例逐个写了。
8. **多个摘要读写检测的备选方案**：七种（§5）。推荐 **A（自底向上条件 + 效应）做主干**，C（按需查询）做调度优化，
   F（CSA 消费摘要）做告警确认与路径解释，E（事实查询）和 G（切片 + LLM）对接 AI。
9. **用例**：Lv1–Lv10 共 67 个（134 个入口），全部能编译；用 Infer 当标尺跑过：134 个入口里 104 个与期望一致，
   年内要报的 41 个（32 个承诺 + 9 个尽力）里 Infer 报出 13 个、级别全对，漏掉的 28 个正好落在安全函数、全局表、校验界、字段这几类（§6）。

---

## 1. 根本问题

### 1.1 摘要回答的是什么问题

对函数 `f`，它的**接口**是：形参、读到的全局、经形参能到达的结构体字段（深度有限）、返回值、出参。
摘要是四样东西：

| 部分 | 内容 | 例子 |
|---|---|---|
| **Req** 前置条件 | 一组关于接口的条件；调用方都满足时，`f` 和它调用的函数里的访问都不越界 | `需 8·ulNum ≤ ext(pstItem)` |
| **Eff** 效果 | 和内存安全有关的输出：新对象及其大小、返回值 / 出参 / 被写字段 / 被写全局的取值或关系、条件效果 | `ret = new(ulSize)`、`ret ∈ [0, 8]`、`ret ≠ 0 ⇒ ulId ≤ 16` |
| **Rep** 已判定 | 在 `f` 内部已经能判定的越界（不依赖接口），带 trace | `L1：需 17 ≤ ext(szName)=16` |
| **Open** 账本 | 没算进来的东西和原因 | `函数指针@Cap_Dispatch`、`未知函数 Drv_ReadReg`、`widening 丢关系` |

**关心的**：能成为访问的基址、偏移、长度、对象大小的量，以及约束它们的校验和返回值。
**不关心的**：缓冲区里装的数据（读出来只当一个无约束的符号）、不支配访问的控制流、和上面这些没有数据关系的一切。

"相关"有两个方向，难度不一样：

* **往回看（Req）**：哪些输入会影响我的访问——从访问点往回追，是个明确的问题。
* **往前看（Eff）**：我的哪些输出会影响别人的访问——自底向上算的时候不知道调用方要什么。两种做法：
  **固定词表**（总是算：返回 / 出参指针指向的新对象及大小、整数返回值 / 出参 / 被写字段 / 被写全局的范围）；
  **按需**（调用方有一条未决条件引用到某个输出时，才回头算那一项，结果缓存）。年内用固定词表，按需留作优化（§5 方案 C）。

### 1.2 "整个函数好像都相关"——相关性是算出来的

`cases/demo/relevance_demo.c`：

```c
UINT32 Ue_ProcRrcMsg(UE_CTX_S *pstUe, const UINT8 *pucMsg, UINT32 ulMsgLen)
{
    UINT32 ulIeLen;

    LOG_Print(3, "ue %u rrc len %u", pstUe->ulUeId, ulMsgLen);  /* 不进：什么都不流进访问 */
    pstUe->ulRxCnt++;                                            /* 进效应：整数字段写（固定词表） */
    if (pstUe->ulState != UE_STATE_CONNECTED) {                  /* 不进：守卫里没有被跟踪的量 */
        return VOS_ERR;
    }
    if (Timer_Restart(pstUe->ulTimerId) != VOS_OK) {             /* 不进：被调摘要没有条件 */
        Stat_Inc(STAT_TIMER_ERR);                                /* 不进：常量 2 代入"需 ≤ 3"，当场判安全 */
    }
    ulIeLen = pucMsg[1];                                         /* 进：读 pucMsg[1] → 需 2 ≤ ext(pucMsg) */
    if (ulIeLen + 2 > ulMsgLen) {                                /* 进：守卫，约束了 ulIeLen 与 ulMsgLen 的关系 */
        return VOS_ERR;
    }
    (void)memcpy_s(pstUe->aucLastIe, sizeof(pstUe->aucLastIe),   /* 目的：成员 32 字节、destMax 32 → 当场安全，不进 */
                   pucMsg + 2, ulIeLen);                         /* 源：末端 2 + ulIeLen ≤ ulMsgLen → 需 ulMsgLen ≤ ext(pucMsg) */
    return VOS_OK;
}
```

它的摘要：

```text
Ue_ProcRrcMsg(pstUe, pucMsg, ulMsgLen)
  req R1  R1 @ pucMsg + 1                           需 2 ≤ ext(pucMsg)
  req R2  R ulIeLen @ pucMsg + 2，末端 ≤ ulMsgLen     需 ulMsgLen ≤ ext(pucMsg)
          （ulIeLen 是消息内容，无约束；守卫 ulIeLen + 2 ≤ ulMsgLen 把它"消掉"了）
  eff     pstUe->ulRxCnt := pstUe->ulRxCnt + 1
```

20 行代码剩下 2 条条件和 1 条效果。决定去留的不是"这个变量重不重要"的判断，而是三件机械的事：

1. **数据依赖**由抽象解释自动追：没有流进访问的量，从来就没进过条件。
2. **控制依赖**只保留**支配访问、且提到被跟踪量**的条件，作为守卫（`ulIeLen + 2 > ulMsgLen`）；
   其余分支照常合并（`ulState` 那个判断）——这会丢掉一些相关性，但只会让结论变成"判不了"，不会变成错的。
3. **被调函数**按它们自己的摘要代入：`Stat_Inc` 的条件当场被常量满足，`Timer_Restart` 本来就没有条件。

你担心的"任何变量都可能影响访问"是对的，但它只通过三条通道：数据、控制、**内存（别名）**。
前两条上面已经处理了；第三条——经另一个指针改了我们跟踪的字段——才是真正的风险，见 §1.6。

### 1.3 可判定性，以及"拿不准怎么办"

* **精确摘要不可判定**：要精确知道"什么输入会越界"等价于判定程序性质，Rice 定理说不行。
* **近似摘要总能算出来**：抽象解释在循环头上 widening、有迭代上限、任何构造都有兜底（降成未知），所以**一定终止，一定产出合法摘要**
  （`01-` §3.4 的全函数性）。
* **所以真正的问题是精度**，处理原则：

| 情况 | 结论 | 去向 |
|---|---|---|
| 代入后条件对所有取值成立 | 安全 | 丢弃 |
| 代入后对所有取值都不成立 | **L1** | 报 |
| 取值范围的上界来自代码（校验、掩码、分支常量、循环界、返回值范围），且部分取值不成立 | **L2** | 报 |
| 还含有上层的符号 | StillOpen | 往上传 |
| 到入口还含符号 / 上界只来自类型范围 / 上界是 widening 出来的 ∞ / 含非线性项 | 判不了 | **不报，记账**（带原因） |

最后一行很关键：**类型范围（UINT16 的 65535）和 widening 的 ∞ 都不算"代码给出的界"**，否则每个消息长度字段、每个指针步进循环都会变成告警
（LV7-04、LV8-02 的 TN 就是专门守这一条的用例）。记账就是漏报账本：W10 上真实代码后按原因统计，风险是看得见的。

### 1.4 摘要能结合切片吗

能。摘要 = 访问点的后向切片 → 投影到接口 → 用抽象域压缩。三种结合方式：

| 方式 | 做法 | 用在哪 |
|---|---|---|
| **隐式切片**（现设计） | 抽象解释只跟踪"可能流进访问或效果"的量，不单独做切片 | 摘要引擎本身（年内） |
| **两段式：先依赖、后数值** | 第一段算**依赖摘要**：哪些入参 / 全局可能影响哪些访问 / 输出（图可达性，便宜、近似完备）；第二段只在这个切片上算数值摘要 | 预筛（"这个函数和越界有没有关系"）、大函数瘦身、2027 增量分析的失效依据 |
| **显式跨函数切片**（SDG，Horwitz–Reps–Binkley） | 系统依赖图里的 summary edge 本身就是"依赖摘要"；从访问点切出跨函数的切片 | 给 AI：Lv8–Lv10 把切片和未证明的条件一起交给 LLM 判 |

推荐：引擎用隐式切片；第一段依赖摘要作为可选预筛（P1，大函数多时提前）；显式切片作为 AI 接口（2027）。

### 1.5 摘要的单位，以及要跨多少层

**单位的备选**：

| 单位 | 什么时候用 | 代价 |
|---|---|---|
| **单函数**（默认） | 一切正常情况；符号代换让一份摘要适用于所有调用点（"上下文多态"） | —— |
| **强连通分量** | 递归（LV9-05）：分量内迭代到不动点 | 实现量中等，2027H1 |
| **折叠薄转发** | 只做一次转发的函数（`L1 → L2 → L3` 那种）可以不单独存摘要，直接拼接 | 省存储；trace 里仍保留这一层 |
| **按上下文分开** | 同一个函数在不同调用点的行为差别大、符号代换表达不了（比如按标志位走完全不同的分支） | 摘要数量膨胀，只对少数函数开 |
| **模块边界** | 模块内部一起分析，只对外导出接口摘要 | 适合库型模块；与 2027 的增量分析一起考虑 |

**不跨函数就能定的**：局部 / 静态 / 定义可见的全局数组配常量下标；成员类型给出的大小（`p->aucKey`）；
函数自己校验过的访问；安全函数的 destMax 等于目的成员大小。这些在函数内就判定了（安全或越界），不产生 Req。
**这是常态**：大多数函数的 Req 是空的，只有"信任调用方"的函数才导出条件。

**跨多少层**：不预设上限。条件一直往上走，直到：被判定（安全 / L1 / L2）；变成无用（含非线性项、类型范围、widening ∞）；或到入口（记账）。
层数只影响 trace 的长度，不影响计算量。工程上的限制在别处：每个函数的条件数（合并同基址同形状的条件，超过上限按严重度保留，其余记账）。

### 1.6 风险在哪、不在哪

| 风险 | 表现 | 怎么看得见 | 怎么控 |
|---|---|---|---|
| **别名导致效果漏算** | 经另一个指针改了我们跟踪的字段，摘要不知道 → 可能漏报，极少数误报 | 记"按类型失效"的次数 | 写 `T*` 时失效所有被跟踪的、类型为 `T` 的字段（按类型的保守规则）；真别名不在本期 |
| **未知函数的默认假设** | 乐观（假设不改被跟踪的内存）可能漏报；悲观（全部失效）会让很多条件判不了 | 记账"未知函数"及其调用次数 | 已知纯函数（日志、格式化）乐观；其余按参数类型失效；常用的进模型表 |
| **条件膨胀** | 一个函数的条件越传越多 | 每函数条件数、截断次数 | 同基址同形状合并（只留最强）；上限截断，按严重度保留 |
| **域表达不了的关系** | 非线性、跨循环、字段不变式 | 记账原因分类 | 判不了就不报；按账本决定补什么（2027 / AI） |
| **跨文件不一致** | 依赖的摘要已过期 | 指纹不一致 | 多轮扫描 + 指纹（`01-` §3.11） |

**不是风险的**：终止性（一定终止）、规模（每函数一次，层间相加；Infer 实测 200 层 0.76 秒）、确定性（同输入同输出）。

---

## 2. 摘要的形式

### 2.1 记法（用例文件头里用的就是这套）

| 记法 | 含义 |
|---|---|
| `ext(p)` | 从指针 `p` 往后还能访问的字节数（对象大小 − p 的偏移）。偏移下传时 `ext(p + k) = ext(p) − k` |
| `W n @ p + off` / `R n @ p + off` | 在 `p + off` 处写 / 读 `n` 字节。`off` 可以是常数、线性式、区间 `[a, b]` |
| `需 X ≤ ext(p)` | 这条访问安全的条件（末端不超过对象） |
| `守卫 ⇒ …` | 只在守卫成立时才有这次访问，例如 `ulPortId < 32 ⇒ 需 24·ulPortId + 4 ≤ ext(astPort)` |
| `p->f`、`g_x` | 字段、全局在**函数入口时**的值（作符号） |
| `ret = new(E)` | 返回一个新申请的对象，大小 `E`；`*pp = new(E)` 是出参；`p->f := E` 是字段写 |
| `ret ∈ [a, b]`、`ret ≠ 0 ⇒ x ≤ 16` | 返回值范围；条件效果（校验函数） |
| `⟨a·b⟩` | 保留下来的不透明项（两个变量相乘等），代入后变成常数才参与判定 |
| `(x − 1)ᵘ` | 无符号减法，可能回绕 |
| `path A → B → C` | 条件从访问点上传经过的函数（报告时就是 trace） |

### 2.2 落盘结构

```sql
-- 每个函数一行
CREATE TABLE summaries (
  fn_usr TEXT PRIMARY KEY,          -- 函数的 USR（跨 TU 唯一）
  tu_file TEXT, fingerprint TEXT,   -- 所在文件、内容指纹（失效判断）
  status TEXT,                      -- Analyzed / Partial(原因) / Skipped(原因)
  pending_callees TEXT              -- 还没有摘要的被调者（跨 TU 多轮用）
);
-- 前置条件，一条一行
CREATE TABLE reqs (
  fn_usr TEXT, rid INTEGER,
  kind TEXT,                         -- R / W
  base TEXT,                         -- 访问路径：param#0、param#0->pucBuf、global:g_x、new@site
  off_lo TEXT, off_hi TEXT,          -- BoundExpr 的序列化（线性项 + 不透明项）
  size TEXT, guard TEXT,             -- 访问字节数、守卫（线性约束的合取）
  access_loc TEXT, path TEXT,        -- 访问点位置、上传经过的函数
  depth INTEGER, csa_blind_spot INTEGER
);
-- 效果
CREATE TABLE effs (
  fn_usr TEXT, target TEXT,          -- ret / *param#1 / param#0->ulLen / global:g_num
  form TEXT,                         -- new / range / eq / cond / invalidate
  value TEXT, cond TEXT              -- 大小或取值、条件效果的前提
);
-- 账本：没算进来的东西
CREATE TABLE opens (fn_usr TEXT, reason TEXT, loc TEXT, detail TEXT);
-- 已判定的告警（指纹去重）
CREATE TABLE reports (fingerprint TEXT PRIMARY KEY, severity INTEGER, depth INTEGER,
                      csa_blind_spot INTEGER, access_loc TEXT, report_fn TEXT, trace BLOB);
```

同一份内容的 JSON 形态（给查询接口和 AI 用）：

```json
{ "fn": "Mac_Fill", "status": "Analyzed",
  "reqs": [ { "rid": 1, "kind": "W", "base": "param#0", "off": "56", "size": "1",
              "need": "57 <= ext(param#0)", "guard": null,
              "access": "lv2_08_layered_headers.c:31", "path": ["Mac_Fill", "Rlc_FillCrc"] } ],
  "effs": [], "opens": [] }
```

### 2.3 参照：Infer 对同一用例的真实摘要【实测数据】

LV2-08（协议栈逐层加头）在 Infer v1.3.0 上的摘要（`infer debug --procedures --procedures-summary`，节选）：

```text
void Rlc_FillCrc(unsigned char* pucRlc)
BufferOverrunChecker: Safety conditions:
  { pucRlc.offset + 50 < pucRlc.length at lv2_08_layered_headers.c:31:5 }

void Mac_Fill(unsigned char* pucMac)
BufferOverrunChecker: Safety conditions:
  { pucMac.offset + 6 + 50 < pucMac.length at lv2_08_layered_headers.c:31:5,
    pucMac.offset + 0 < pucMac.length at lv2_08_layered_headers.c:36:5 }
```

和我们的记法是同一个东西：`p.length − p.offset` 就是 `ext(p)`；`Mac_Fill` 的第一条是被调者的条件代换了偏移 `+6` 之后上传的。
区别在我们多做的部分：守卫、效果词表、账本、按字节归一的安全函数条件、全局与字段的大小（§6 的标尺结果说明这些正是 Infer 漏掉的地方）。

---

## 3. 怎么把摘要串起来

### 3.1 调用点上的五步

调用者 `c` 在调用点 `g(a0, a1, …)`：

1. **取摘要**：同 TU 的被调者没算过就先算（按需递归）；跨 TU 的查 SummaryDB，没有就记 `pending`，下一轮再来。
   函数指针先解析目标集合（年内只解析同函数内的常量赋值，S9.1），解析不了记账。
2. **建代换表**：`param#i ↦ 实参在调用点的抽象值`。指针实参是"基址 + 偏移"（`aucBuf + 8`、`new@site`、`c` 的形参）；
   整数实参是"线性式 + 区间"；字段路径 `param#0->f` 映射到实参指向的对象在 `c` 里记录的字段值。
3. **代换并化简**：把每条 Req 的 `ext(param#i)` 换成 `ext(实参基址) − 实参偏移`，把整数形参换成实参表达式；
   **同类项先抵消**（LV3-07 的 `ulOff`），守卫一并代换。
4. **用调用点的状态判定**：调用点处变量的区间（来自 `c` 自己的校验、循环、常量）代入，按 §1.3 的表分成
   安全 / L1 / L2 / 上传 / 记账。上传的条件改写成 `c` 的接口符号，`path` 前面加上 `c`。
5. **应用效果**：返回值、出参、被写字段、被写全局按 Eff 更新 `c` 的状态；条件效果在 `c` 随后的分支上收窄
   （`if (Check(id) != OK) return;` 之后 `id ≤ 16`，LV4-06）。

所有函数按调用图的强连通分量逆拓扑序处理（被调者先），一个函数只算一次；跨 TU 用多轮扫描 + 工作表（`01-` §3.11）。

### 3.2 一条完整的链：大小往上走、条件往下走（LV4-01）

```text
Pub_MemAlloc(ulSize)        eff ret = new(ulSize) ∨ NULL                     ← 配置给了 VOS_MemAlloc 的字节数下标
Cell_MemAlloc(ulSize)       eff ret = new(ulSize) ∨ NULL                     ← LOG_Print 不影响效果
Ue_AllocTbl()               eff ret = new(512) ∨ NULL                        ← 实参 32 × 16 代入

Ue_InitOne(pstTbl, ulIdx)   req W4 @ pstTbl + 16·ulIdx          需 16·ulIdx + 4 ≤ ext(pstTbl)
Ue_InitTbl(pstTbl, ulNum)   调用点 i ∈ [0, ulNum − 1]（循环 + 符号 narrowing）
                            req ulNum ≥ 1 ⇒ W4 @ pstTbl + 16·[0, ulNum−1]   需 16·ulNum − 12 ≤ ext(pstTbl)
Ue_Setup(pstTbl)            实参 ulNum = 64                     需 1012 ≤ ext(pstTbl)

Ue_ModuleInit_TP()          pstTbl = Ue_AllocTbl() → ext = 512（判空分支排除了 NULL）
                            → 1012 ≤ 512 不成立，i = 0 那次成立 → L2
                            trace：Ue_ModuleInit_TP → Ue_Setup → Ue_InitTbl → Ue_InitOne（大小来源：Ue_AllocTbl → Cell_MemAlloc → Pub_MemAlloc）
```

大小信息往上走三层（效果），访问条件往上走三层（前置条件），在链顶相遇——这就是"汇合点"。
任何一侧再加十层，过程完全一样，只是 trace 长一点。

---

## 4. 每一级：用例、摘要要素、怎么组合

> 每级给：这一级的摘要**多了什么要素**、用例表、一个代表用例的组合要点。全部 67 个用例的逐函数摘要与串联步骤在文件头里。
> 期望的写法：`pa` 是 12-15 交付（P0）时的程序分析结果，`later` 是 P1 / 2027 补上能力之后，`ai+pa` 是交给 AI + 程序分析之后；
> "记账"表示不报但留一条未证明 / 断链的记录。

### Lv1 一侧跨层 · 原样传递

**摘要要素**：Req 里只有"常量 ≤ ext(形参)"或"形参 ≤ 常量"；代换就是把形参换成实参。
**组合**：条件原样上传，到第一个知道大小（或知道下标值）的函数判定。汇合点不一定是入口（LV1-04 在第一个知道成员类型的函数）。

<!-- CASES:lv1 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV1-01 | 统计计数：枚举结束标记 BUTT 被当成下标逐层传下去 | S1.6  A1·M2·P0·V1·R1·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_01_stat_enum_butt.c` |
| LV1-02 | 名字缓冲区逐层下传，最底层按"长度"位置写结尾符 | S1.1  A1·M1·P1·V0·R1·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_02_name_terminator.c` |
| LV1-03 | 常量长度的堆内存逐层下传，最底层循环多写一个 | S1.2+S1.3  A1·M4·P1·V0·R1·X0·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv1/lv1_03_heap_const_loop.c` |
| LV1-04 | 结构体数组成员的地址逐层下传，底层循环用了新规格的宏 | S1.4  A1·M3·P1·V0·R1·X0·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv1/lv1_04_member_array_macro.c` |
| LV1-05 | 结构体扩展后，清零函数按扩展后的大小 memset_s，传进来的却是老结构体 | S1.7  A3·M1·P1·V0·R1·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_05_memset_ext_struct.c` |
| LV1-06 | 查表函数自己的常量表，下标从多层之上传入，中间层只是改名转存 | S1.5  A1·M1·P0·V1·R1·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_06_qci_table_local.c` |
| LV1-07 | 跨文件：告警 ID 用了 BUTT，经三个文件三层到达标志数组 | S1.6  A1·M2·P0·V1·R1·X0·K0·G0（跨 TU） | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_07_cross_tu` |
| LV1-08 | 拷贝模板：源缓冲区逐层下传，最底层按新长度读（读越界） | S1.7  A3·M2·P1·V0·R1·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv1/lv1_08_tmpl_src_read.c` |
<!-- /CASES:lv1 -->

### Lv2 两侧同链 · 线性变换 · 偏移 · 强转

**摘要要素**：线性式（`8·ulNum + 8`）、偏移累加（`ext(p + k) = ext(p) − k`）、按字节归一（强转只换元素大小）、
常量操作数的区间运算（`x & 64 ∈ [0, 64]`、`x / 32`）、无符号回绕标记。
**组合**：两侧形参在同一次代换里一起换掉；常数项随偏移逐层变化，条件形状不变（LV2-08 的 `+8 → +6 → +50`）。
LV2-04 说明变量长度的堆不需要知道具体值：`8n + 8 ≤ 8n` 两边相减恒为 −8。

<!-- CASES:lv2 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV2-01 | 把 sizeof(数组) 当成"个数"传下去，底层按个数循环写 UINT16 | S2.1+S2.2  A1·M1·P1·V1·R2·X1·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv2/lv2_01_sizeof_as_count.c` |
| LV2-02 | 跳过消息头后下传，长度没扣头长（读越界） | S2.3  A3·M1·P1·V1·R2·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv2/lv2_02_skip_header_len.c` |
| LV2-03 | memcpy_s(pkt + off, 整包大小, …)：destMax 没扣偏移，偏移在中间层又加了头长 | S2.4  A3·M1·P1·V1·R2·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv2/lv2_03_tlv_destmax_no_off.c` |
| LV2-04 | 按 n 个元素申请，结束标记写在第 n 个（申请与访问同源，不需要知道 n） | S2.5  A1·M5·P1·V1·R2·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv2/lv2_04_heap_n_items_end.c` |
| LV2-05 | 字节型位图按 UINT32 清零，字数算多了一个 | S2.6+S2.7  A1·M1·P1·V1·R2·X2+X3·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv2/lv2_05_bitmap_cast_words.c` |
| LV2-06 | 置位图：上层按扩容后的 UE 规格循环，下层按 idx / 32 定位字 | S2.7+S3.9  A1·M1·P1·V1·R2·X3·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv2/lv2_06_bitmap_set_loop.c` |
| LV2-07 | 哈希桶下标写成 key & BUCKET_NUM（应为 BUCKET_NUM − 1） | S2.7  A1·M1·P1·V5→X3·R2·X3·K0·G0 | 承诺 | pa=L2 | pa=- | `cases/lv2/lv2_07_hash_mask_wrong.c` |
| LV2-08 | 协议栈逐层加头：每层偏移都没问题，加起来超了帧长 | S2.3  A2·M1·P1·V0·R1·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv2/lv2_08_layered_headers.c` |
| LV2-09 | 无符号下标减一回绕：pos 为 0 时取"前一个" | S2.2  A1·M1·P1·V1·R2·X1·K0·G0/G1 | 承诺 | pa=L1 | pa=- | `cases/lv2/lv2_09_unsigned_wrap.c` |
<!-- /CASES:lv2 -->

### Lv3 校验、分支、典型循环

**摘要要素**：**守卫**（`ulPortId < 32 ⇒ …`）；循环变量的符号界（`i ∈ [0, ulNum − 1]`）；循环退出值（`i == ulLen`）；
**先化简再代区间**。
**组合**：调用方的校验不进被调者的摘要，而是在调用点把实参的区间收窄后再代入（LV3-01）；
被调者自己的校验进它自己的摘要成为守卫（LV3-03）——这样 TN 里常量 3 的调用点才不会误报。

<!-- CASES:lv3 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV3-01 | 消息入口校验写成 > MAX（应为 >=），小区号三层之后索引全局上下文表 | S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_01_check_gt_vs_ge.c` |
| LV3-02 | 校验用接入模块的规格宏，数组用上下文模块的老规格宏 | S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_02_check_wrong_macro.c` |
| LV3-03 | 访问函数自己校验了，但校验的界比多层之上传进来的数组大（TN 是常量调用点，不能误报） | S3.2  A1·M2·P1·V5→G1·R2·X1·K1·G1 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_03_callee_guard_wrong.c` |
| LV3-04 | 按源列表的个数循环拷贝，目的数组是老规格 | S3.4  A1·M1·P1·V5→G2·R2·X1·K2·G2 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_04_copy_by_src_count.c` |
| LV3-05 | 填满后补结尾符：循环退出时计数器等于长度 | S3.5  A1·M1·P1·V1·R2·X0·K2·G0 | 承诺 | pa=L1 | pa=- | `cases/lv3/lv3_05_pad_then_terminate.c` |
| LV3-06 | 按制式选长度：NR 的能力长度超过了缓冲区（分支合并后一支越界） | S3.3  A3·M1·P1·V1·R2·X0·K1·G0 | 尽力 | pa=L2 | pa=- | `cases/lv3/lv3_06_rat_branch_len.c` |
| LV3-07 | 报文组装循环：按剩余长度拷贝（安全） vs. destMax 用整包长度（缺陷） | S3.7+S3.8  A3·M1·P1·V1·R2·X1·K3·G1 | 承诺（TN）/ 尽力（TP） | pa=L2 | pa=- | `cases/lv3/lv3_07_pkt_loop_destmax.c` |
| LV3-08 | 调用链在循环里：上层遍历小区用了 <=，三层之下按小区号清上下文 | S3.9  A3·M2·P0·V1·R1·X1·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_08_caller_loop_le.c` |
| LV3-09 | 调度网格清零：行列宏写反（嵌套循环，各维界是常量） | S3.6  A1·M1·P1·V0·R1·X1·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_09_grid_row_col_swap.c` |
| LV3-10 | 校验之后又改了下标："取下一个位置"没再校验 | S3.1  A1·M2·P0·V5→G2·R1·X1·K1·G2 | 承诺 | pa=L2 | pa=- | `cases/lv3/lv3_10_modify_after_check.c` |
<!-- /CASES:lv3 -->

### Lv4 内存来自下层：分配器、出参、getter、校验函数

**摘要要素**：Eff 里的 `ret = new(E)`、`*pp = new(E)`、`ret ∈ [a, b]`、**条件效果** `ret = OK ⇒ id ≤ 16`（v3.11 新增 S4.6）。
**组合**：效果往上走到链顶，变成链顶状态里的对象大小 / 取值范围，再去满足往上走的前置条件（V 形，§3.2）。
LV4-07：分配器没函数体、也没配字节数下标时，大小是符号——**宁可不报**，不能当 0。

<!-- CASES:lv4 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV4-01 | 三层封装的分配器按老规格申请，初始化按新规格遍历 | S4.1  A1·M4·P2+P1·V0·R1·X1·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv4/lv4_01_wrapper_alloc_spec.c` |
| LV4-02 | 申请写成 sizeof(指针)，三层之下按结构体成员写 | S4.1  A3·M4·P2+P1·V0·R1·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv4/lv4_02_sizeof_pointer.c` |
| LV4-03 | 带头部的缓冲池分配器：调用方按"长度 + 结尾符"写 | S4.2  A1·M5·P2+P1·V1·R2·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv4/lv4_03_hdr_pool_alloc.c` |
| LV4-04 | 出参分配器申请报告缓冲，填充时按"报告长 + 尾长"清零 | S4.3  A3·M4·P2+P1·V0·R1·X1·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv4/lv4_04_outparam_alloc.c` |
| LV4-05 | 个数来自"取配置并截断"的 getter（两层），使用处循环多了一个等号 | S4.4  A1·M1·P1·V2·R3·X0·K2·G0 | 承诺 | pa=L2 | pa=- | `cases/lv4/lv4_05_getter_clamp_le.c` |
| LV4-06 | 校验封装在独立函数里（返回布尔），校验函数自己差了一 | S4.6（v3.11 新增）  A3·M2·P0·V5→G3·R1·X1·K1·G3 | 尽力 | pa=L2 | pa=- | `cases/lv4/lv4_06_validator_func.c` |
| LV4-07 | 分配器的函数体不在分析范围里，只出现在 config.ini / alloc_free_identity 结果里 | S4.5  A1·M5·P2+P1·V0·R1·X0·K0·G0 | 规则性处理 | pa=记账 · later=L1 | pa=- | `cases/lv4/lv4_07_opaque_allocator.c` |
<!-- /CASES:lv4 -->

### Lv5 字段中转、两侧异链汇合

**摘要要素**：字段级基址 `ext(pstCtx->pucBuf)`、字段值 `pstCtx->ulBufLen`；**帧内字段存储**（链顶记住 `stCtx.pucBuf = aucBuf`）；
**写效应**（`pstCtx->ulBufLen := ulLen + 4`，2027H1）。
**组合**：字段路径在每层只是换个形参名；到链顶时用帧内记录把字段换成具体对象与值（LV5-01）。
LV5-06 说明很多"菱形"其实用 Lv4 的返回值效应就够了，不需要字段能力。

<!-- CASES:lv5 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV5-01 | 编码上下文在链顶填好（缓冲区 + 长度），长度用了新宏，缓冲区还是老大小 | S5.1  A3·M1·P3·V3·R2·X0·K0·G0 | 承诺 | pa=L1 | pa=- | `cases/lv5/lv5_01_ctx_filled_at_top.c` |
| LV5-02 | 上下文由独立的 Init 函数填：长度多算了尾部，缓冲区按原长申请（写效应） | S5.2  A3·M5·P3+P2·V3·R3·X1·K0·G0 | 2027H1 | pa=记账 · later=L1 | pa=记账 · later=- | `cases/lv5/lv5_02_ctx_init_func.c` |
| LV5-03 | IE 长度沿另一条链（按类型查表、两层返回）得到，缓冲区沿主链下传 | S5.3  A3·M1·P1·V2·R3·X0·K1·G0 | 尽力 | pa=L2 | pa=- | `cases/lv5/lv5_03_len_from_sibling_chain.c` |
| LV5-04 | 中间层打开扩展字段时把长度加了，缓冲区没变 | S5.4  A3·M1·P3·V3·R2·X1·K0·G1 | 尽力（S5.4 里"本层先改字段再下传"的形态，用 S5.1 的帧内字段跟踪即可；被调者改写字段的形态见 LV5-02，2027H1） | pa=L1 | pa=- | `cases/lv5/lv5_04_field_changed_midway.c` |
| LV5-05 | 字段级申请（alloc_free_identity 里带 offset 的那类）：资源缓冲写尾越界 | S5.5  A1·M4·P3+P2·V0·R1·X0·K0·G0 | 尽力 | pa=L1 | pa=- | `cases/lv5/lv5_05_field_alloc_offset.c` |
| LV5-06 | 菱形：缓冲区从 A 链（取块，三层）、长度从 B 链（取配置，两层）分别回到链顶，再一起下传 | S4.1+S4.4 组合（R3）  A3·M4·P2+P1·V2·R3·X0·K0·G0 | 承诺（用 Lv4 的返回值效应就够） | pa=L1 | pa=- | `cases/lv5/lv5_06_diamond_buf_len.c` |
<!-- /CASES:lv5 -->

### Lv6 全局状态

**摘要要素**：全局的入口值作符号（`G`）；跨 TU 全局大小表；全局取值集合（所有写入点的并集）；全局指针的流不敏感指向；全局不变式。
**组合**：全局没有"层数"——条件里的全局符号一路不变，最后靠全局层面的事实判定（取值集合、不变式），而不是靠某个调用点。

<!-- CASES:lv6 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV6-01 | 跨文件：extern 不完整声明的全局统计表，下标常量从三层之上传入 | S6.1  A1·M2·P4·V1·R1·X0·K0·G0（跨 TU） | 尽力 | pa=L1 | pa=- | `cases/lv6/lv6_01_extern_tbl` |
| LV6-02 | 全局规格变量只在初始化函数里写常量（新规格 64），数组还是老规格 32 | S6.2  A1·M2·P0·V4·R1·X0·K2·G0 | 2027H1 | pa=记账 · later=L2 | pa=记账 · later=- | `cases/lv6/lv6_02_global_spec_len.c` |
| LV6-03 | 运行期申请的全局表：大小由 g_ulTblNum 决定，访问前按 > g_ulTblNum 校验（应为 >=） | S6.3  A1·M5·P4·V4·R1·X1·K1·G4 | 2027（程序分析验证 + AI 提不变式） | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv6/lv6_03_global_runtime_tbl.c` |
| LV6-04 | 端口数来自配置文件解析（运行期值），遍历全局端口表 | S6.4  A1·M2·P0·V4·R1·X0·K2·G0 | AI + 程序分析 | pa=记账 · ai+pa=L2 | pa=- | `cases/lv6/lv6_04_global_cfg_from_file.c` |
| LV6-05 | 全局指针在初始化时指向静态内存池的尾部 64 字节，三层之下按 64 写结尾 | S6.3 变体  A1·M2·P4·V1·R1·X1·K0·G0 | 2027H1 | pa=记账 · later=L1 | pa=记账 · later=- | `cases/lv6/lv6_05_global_ptr_into_pool.c` |
<!-- /CASES:lv6 -->

### Lv7 内存建模精度

**摘要要素**：柔性数组的大小 = 分配大小 − 成员偏移；逐维的界；字节缓冲按结构体解释时的字节偏移 + 成员边界；指针步进的"偏移 = 迭代次数"；字符串长度；
不透明乘积项 `⟨r·ulCol⟩`。
**组合**：年内这些形态都显式标成"不建模"并记账，**保证不误报**；能力补上之后，组合方式与 Lv2 相同。

<!-- CASES:lv7 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV7-01 | 柔性数组：按 n 项申请，封口时写第 n 项 | S7.1  A1·M6·P2+P1·V1·R2·X1·K0·G0 | 2027H1（计划 P1） | pa=记账 · later=L1 | pa=记账 · later=- | `cases/lv7/lv7_01_flexible_array.c` |
| LV7-02 | 二维数组行级越界：列下标多走一个，越进了下一行（整数组范围内） | S7.2  A1·M7·P0·V1·R1·X1·K2·G0 | 计划 P1（CSA 盲区 #18，1–2 层也报） | pa=- · later=L2 | pa=- | `cases/lv7/lv7_02_2d_row_overflow.c` |
| LV7-03 | 字节缓冲按消息结构体解释，三层之下写数据区——缓冲区本身比结构体短 | S7.3  A1·M1·P1·V1·R2·X4·K0·G0 | 2027H1 | pa=记账 · later=L1 | pa=记账 · later=- | `cases/lv7/lv7_03_struct_overlay.c` |
| LV7-04 | 循环中指针步进拷贝：个数来自三层之上，比目的缓冲区大 | S7.4  A2·M1·P1·V1·R2·X1·K3·G0 | 2027H1 | pa=记账 · later=L2 | pa=记账 · later=- | `cases/lv7/lv7_04_pointer_stepping.c` |
| LV7-05 | strcpy_s 的 destMax 用了名字的最大规格，目的缓冲区是老长度 | S7.5（destMax 那一半）  A4·M1·P1·V0·R1·X0·K0·G0 | 计划 P1（destMax 一半与 memcpy_s 相同；源串长度那一半 2027H2） | pa=- · later=L1 | pa=- | `cases/lv7/lv7_05_strcpy_destmax.c` |
| LV7-06 | 二维表按"行数 × 列数"申请，访问用变量步长线性化（两个变量相乘） | S7.6  A1·M5·P2+P1·V1·R2·X5·K2·G0 | 不承诺 | pa=记账 | pa=记账 | `cases/lv7/lv7_06_variable_stride.c` |
<!-- /CASES:lv7 -->

### Lv8 复杂循环

**摘要要素**：本轮守卫与本轮访问之间的线性关系（LV8-01，尽力）；跨轮、跨循环的关系（LV8-02，交给 AI 提不变式）；字段不变式（LV8-05）。
**组合**：能用"守卫蕴含条件"解决的就不需要循环不变式；需要不变式的，由 AI 提、程序分析在**所有写入点**验证——
报告落在写入点（修复点），比落在访问点更有用。

<!-- CASES:lv8 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV8-01 | TLV 解析循环：每轮的校验漏算了 2 字节 TLV 头（读越界） | S8.1（相对条件写法）  A3·M1·P1·V5·R2·X1·K3·G1 | 尽力 | pa=L2 | pa=- | `cases/lv8/lv8_01_tlv_relative_check.c` |
| LV8-02 | 两遍式 TLV：第一遍校验总长，第二遍按字节搬运时不再逐个校验 | S8.1（跨轮 / 跨循环关系）  A1·M1·P1·V5·R2·X1·K3·G4 | AI + 程序分析 | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv8/lv8_02_tlv_two_pass.c` |
| LV8-03 | 手写的字符串拷贝循环（while (*src)），链顶传了一个比目的缓冲区长的字面量 | S8.2+S7.5  A2·M1·P1·V1·R2·X1·K4·G0 | 2027H2（字符串长度抽象 + 指针步进） | pa=记账 · later=L2 | pa=记账 · later=- | `cases/lv8/lv8_03_handwritten_strcpy.c` |
| LV8-04 | 三维资源格（符号 × RB × 端口）按配置值线性化，嵌套循环多走了一个端口 | S8.3  A1·M5·P2+P1·V1·R2·X5·K4·G0 | 不承诺 | pa=记账 | pa=记账 | `cases/lv8/lv8_04_grid_3d_config.c` |
| LV8-05 | 环形缓冲区：尾指针回绕条件写成 > RING_LEN（应为 >=），访问在三层之下 | S8.1 变体（字段不变式）  A1·M2·P4·V4·R1·X0·K3·G4 | AI + 程序分析（也是 2027 字段级不变式的候选） | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv8/lv8_05_ring_tail_invariant.c` |
<!-- /CASES:lv8 -->

### Lv9 断链：函数指针、外部函数、递归、跨任务消息

**摘要要素**：调用点的目标集合（局部常量 / 字段级解析 / 外部注入）；模型表条目（可来自 AI，带来源标注）；强连通分量的不动点；
**消息边**（v3.11 新增 S9.6：发送点 ↔ 任务入口）。
**组合**：多目标时每个目标的摘要各自代入，各带"走这个目标"的守卫；目标集合来自外部时，进 SummaryDB 的外部调用边表，之后与普通调用无异。

<!-- CASES:lv9 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV9-01 | 同一函数里按制式选处理函数（函数指针取常量），NR 处理按"长度"位置写结尾 | S9.1  A1·M1·P5·V1·R2·X0·K1·G0 | 尽力（计划 P1） | pa=L2 | pa=- | `cases/lv9/lv9_01_local_fptr_select.c` |
| LV9-02 | 静态消息分发表（全局 const 结构体数组里的函数指针），其中一个处理函数写结尾越界 | S9.2  A1·M1·P5·V1·R2·X0·K1·G0 | 2027H1 | pa=记账 · later=L2 | pa=记账 · later=- | `cases/lv9/lv9_02_static_ops_table.c` |
| LV9-03 | 运行期注册的事件回调：注册在模块初始化里，触发在另一条链上 | S9.3  A1·M1·P5·V1·R2·X0·K1·G0 | AI + 程序分析（2027 的指向分析也能做一部分） | pa=记账 · ai+pa=L1 | pa=记账 · ai+pa=- | `cases/lv9/lv9_03_runtime_register.c` |
| LV9-04 | BSP 驱动接口只有声明：按 ulLen 往 pucBuf 读寄存器，调用方缓冲区 16、长度 32 | S9.4  A3·M1·P5·V1·R2·X0·K0·G0 | 模型表（程序分析） + AI 生成模型草稿 | pa=记账 · ai+pa=L1 | pa=记账 · ai+pa=- | `cases/lv9/lv9_04_external_driver.c` |
| LV9-05 | 递归遍历配置树，按深度写路径数组，深度保护差一 | S9.5  A1·M1·P1·V1·R2·X1·K5·G1 | 2027H1 | pa=记账 · later=L2 | pa=记账 · later=- | `cases/lv9/lv9_05_recursion_depth.c` |
| LV9-06 | 跨任务消息：发送方把"端口个数"填进了"端口号"字段，接收任务三层之后按端口号索引 | S9.6（v3.11 新增）  A1·M2·P5·V1·R1·X0·K0·G0 | AI + 程序分析 | pa=记账 · ai+pa=L1 | pa=记账 · ai+pa=- | `cases/lv9/lv9_06_intertask_msg.c` |
<!-- /CASES:lv9 -->

### Lv10 业务语义与隐式契约

**摘要要素**：外部注入的**契约**（`ext(pstPkt->pucExt) = pstPkt->ulExtLen`、注释里的前置条件、"个数 ≤ 容量"）。
**组合**：程序分析先把访问条件停在入口（记账），AI 提供契约后再判；字段不变式类的，在所有写入点验证，报在写入点。

<!-- CASES:lv10 -->
| 编号 | 用例 | 场景 / 签名 | 年内 | TP 期望 | TN 期望 | 文件 |
|---|---|---|---|---|---|---|
| LV10-01 | 报文结构体有两组"缓冲区 + 长度"，访问 ext 时用 dataLen 做了校验 | S10.1  A1·M8·P3·V3·R2·X0·K1·G4 | AI + 程序分析 | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv10/lv10_01_field_pairing.c` |
| LV10-02 | 平台 API 的"缓冲区 + 长度"约定只写在注释里，调用方传的长度大于缓冲区 | S10.2  A3·M1·P5·V0·R1·X0·K0·G4 | AI + 程序分析 | pa=记账 · ai+pa=L1 | pa=记账 · ai+pa=- | `cases/lv10/lv10_02_param_pairing_api.c` |
| LV10-03 | 对外接口注释承诺"端口号 0~31"，实现里的表只有 16 项且不校验 | S10.3  A1·M2·P0·V1·R1·X0·K0·G4 | AI + 程序分析 | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv10/lv10_03_doc_contract.c` |
| LV10-04 | 外部消息里的长度一路下传到 memcpy_s，没有任何校验（TN：框架在注册时声明了最大长度） | S10.4  A3·M2·P0·V5·R1·X0·K0·G0 | AI + 程序分析（候选研判） | pa=记账 · ai+pa=L2 | pa=记账 · ai+pa=- | `cases/lv10/lv10_04_unchecked_ext_len.c` |
| LV10-05 | "个数"字段与数组容量的对应关系：解码时直接从消息里取个数，没跟数组容量比 | S10.1 变体（个数 ↔ 数组）  A1·M3·P3·V3·R2·X1·K2·G4 | AI + 程序分析 | pa=记账 · ai+pa=L2 | pa=- | `cases/lv10/lv10_05_count_field_writer.c` |
<!-- /CASES:lv10 -->

---

## 5. 多个摘要怎么读写来做检测：七个备选方案

| 方案 | 写什么 | 怎么读 | 能做到哪几级 | 精度 | 成本 | 主要风险 |
|---|---|---|---|---|---|---|
| **A 自底向上：条件 + 效果**（现设计） | 每个函数一份 Req / Eff / Rep / Open | 调用点代换（§3.1） | Lv1–Lv5，扩展后 Lv6–Lv7 | 高（符号代换 = 上下文多态） | 每函数一次 | 效果词表不全、别名 |
| **B 自顶向下：入口事实传播** | 每个函数的"入口事实"：所有调用点实参的合并（对象大小、区间） | 函数内检查访问时直接用入口事实 | Lv1、部分 Lv2 | 低：调用点一多就合并失真（一处传 16、一处传 64 → [16, 64]） | 低 | 上下文敏感就要克隆，又回到爆炸；上游一变下游全重算 |
| **C 按需反向查询** | 不预先算全部；从访问点出发向调用者追问，结果缓存 | 同 A，但只对访问点相关的函数做 | 同 A | 同 A | 只算相关函数，大工程省 | 调度与缓存失效复杂 |
| **D 全程序值流图**（SVF 式） | 全程序的指针 / 整数值流图 | 在图上传播大小与区间 | 异链、全局、字段天然统一 | 中（图上合并） | 要全程序指向分析，重 | 规模与增量 |
| **E 事实导出 + 规则查询**（Datalog / CodeQL 式） | 调用、赋值、申请、访问、守卫导出成关系 | 递归规则算大小与违例 | 简单形态；适合找候选、给解释 | 数值推理弱（区间、widening 在规则里别扭） | 中 | 当主判定精度不够 |
| **F CSA 消费摘要** | A 的摘要 | ExprEngine 在深层调用点不 inline，改为检查 Req（用路径约束）、施加 Eff | 与 A 相同，外加链顶链底路径敏感 | 高，且告警有路径解释 | 中 | 只能看 CSA 走得到的路径，ExprEngine 自己的放弃探索仍在 |
| **G 切片 + LLM 研判** | 访问点的跨函数切片 + 未证明条件 | LLM 判、给理由 | Lv8–Lv10、候选研判 | 不可证明 | 高（按条计费） | 稳定性、可复现性 |

**推荐的组合**：

* **A 做主干**（年内）：唯一一个同时满足"层数无关、每函数一次、对多数调用点精确"的方案。
* **C 是 A 的调度优化**：规模吃紧时（W03 的全量空跑耗时会给出答案）提前，否则 2027 与增量一起做。
* **F 做确认与解释**（P1 的 `OobSummaryChecker`）：摘要通道判定、CSA 补路径；也是 L2 告警降误报的手段。
* **E 把摘要库暴露成查询接口**：SummaryDB 加上 trace 和账本，本身就是给 Agent 的"直接查询"层（与团队 AI + 程序分析方向对接）。
* **G 只用在 Lv8–Lv10 和候选研判**，事实注入走 `04-` §7 的四个接口。
* **B、D 不做主干**：B 的合并失真和重算代价、D 的成本与增量问题；但 D 的"全局取值集合、全局指针流不敏感指向"思想用在 Lv6。

**其它取舍的备选**（供讨论）：

| 取舍 | 备选 | 建议 |
|---|---|---|
| 条件的表示 | 纯区间 / 线性 + 不透明项 / 加守卫 / 有界析取（保留 k 个分支） | 线性 + 不透明项 + 守卫；析取留到要把 S3.3 这类 L2 升 L1 时 |
| 效果 | 固定词表 / 按需 | 年内固定词表；按需随方案 C |
| 未知函数 | 乐观 / 悲观 / 按参数类型失效 | 已知纯函数乐观，其余按类型失效，常用的进模型表 |
| 摘要单位 | 见 §1.5 | 单函数 + 强连通分量（2027）+ 折叠薄转发 |
| 什么时候报 | 只报 L1 / L1 + 代码界 L2 / 全报 | L1 + 代码界 L2；类型范围、widening ∞、非线性不报 |
| 在哪报 | 访问点 / 汇合点 / 写入点 | 汇合点判定、trace 指向访问点；不变式类报在写入点 |

---

## 6. 用例总览与 Infer 标尺【实测数据】

67 个用例、134 个入口（每个用例一对 TP / TN），全部过 `clang` / `gcc -fsyntax-only -Wall -Wextra`（正常与 `-DCASE_SHIM` 两遍）。

| 级别 | 用例数 | 年内要报（pa = L1 / L2） | 年内只记账或不报的 TP | 其中留给 later / AI |
|---|---|---|---|---|
<!-- CASES:stats -->
| Lv1 | 8 | 8 | 0 | 0 |
| Lv2 | 9 | 9 | 0 | 0 |
| Lv3 | 10 | 10 | 0 | 0 |
| Lv4 | 7 | 6 | 1 | 1 |
| Lv5 | 6 | 5 | 1 | 1 |
| Lv6 | 5 | 1 | 4 | 4 |
| Lv7 | 6 | 0 | 6 | 5 |
| Lv8 | 5 | 1 | 4 | 3 |
| Lv9 | 6 | 1 | 5 | 5 |
| Lv10 | 5 | 0 | 5 | 5 |
| **合计** | **67** | **41** | **26** | **24** |
<!-- /CASES:stats -->

用 Infer v1.3.0（`--bufferoverrun-only --headers`，`-DCASE_SHIM` 把分配器和安全函数换成 Infer 认识的 `malloc` / `memset`）跑同一批用例：

* **134 个入口里 104 个与期望一致**；年内要报的 41 个（32 个属于承诺场景、9 个属于尽力场景）里 **Infer 报出 13 个，级别全部一致**（L1 / L2 都对得上）。
* **Infer 漏掉的 28 个**，原因集中在四类，正好是本设计比 Infer 多做的部分：

| Infer 漏报的原因 | 个数 | 用例 |
|---|---|---|
| 安全函数在被调者里：`memset` / `memcpy` 的条件不会上传 | 13 | LV1-05、LV1-08、LV2-02、LV2-03、LV3-06、LV3-07、LV4-02、LV4-04、LV5-01、LV5-03、LV5-04、LV5-06、LV8-01 |
| 全局 / 静态数组 | 9 | LV1-01、LV1-06、LV1-07、LV3-01、LV3-02、LV3-08、LV3-10、LV4-06、LV6-01 |
| 入口无约束、靠校验 / 掩码给出上界 | 3 | LV2-07、LV3-03、LV3-04 |
| 成员数组地址下传、多层分配器配循环调用、局部函数指针 | 3 | LV1-04、LV4-01、LV9-01 |

* **Infer 误报 2 个 TN**（LV2-05、LV2-06）：都是"符号除以常量"（`(ulBits + 31) / 32`、`ulIdx / 32`）在 Infer 的符号界里丢了精度。
  本设计把除以常量保留成不透明项、代入后再算，这两个 TN 不会误报——这是 `01-` §3.3"宁可表示得出但算不动"的直接实据。

复现：

```bash
cd 08-summary2-design/cases
python3 run.py check --cc clang          # 语法检查（正常 + shim 两遍）
python3 run.py index                     # 生成 INDEX.md
python3 run.py infer --infer <infer-linux-x86_64-v1.3.0>/bin/infer
```

---

## 7. 对计划与场景分级的影响（已同步）

* **场景分级新增两个**（`04-` v3.11）：**S4.6 校验封装在独立函数**（尽力；真实代码里最普遍的校验写法，需要条件效果）、
  **S9.6 跨任务消息断链**（AI + 程序分析）。总数 56 → 58，年内承诺仍是 26 个，尽力 6 → 7。
* **用例语料提前就位**：原计划 W02–W03 写的两批语料已经在 `cases/` 里。W02–W03 改做：链长变体生成（3 / 5 / 10 / 17 / 20 层）、
  与你们补充的真实业务用例对照归级。
* **设计补充**（`01-` v3.11）：效果固定词表、条件效果、"类型范围 / widening ∞ / 非线性不报 L2"的口径、账本原因枚举。
* **工作量**：S4.6 列为尽力，不进 P0 总量；其余补充都在原 P0 模块范围内，P0 仍为 86–125 人日。

---

## 附录：全部用例的摘要与串联

> 由 `cases/run.py doc` 从各用例文件头抽取；代码本体见 `cases/` 下对应文件。

<!-- CASES:appendix -->
### Lv1

**LV1-01 统计计数：枚举结束标记 BUTT 被当成下标逐层传下去**（S1.6  A1·M2·P0·V1·R1·X0·K0·G0 · 承诺）

建立超时分支本该记 CELL_STAT_SETUP_FAIL，误传了 CELL_STAT_BUTT（枚举结束标记，只用来定数组长度）。下标穿过 3 层，在统计模块里直接索引全局计数数组。　期望：TP `Cell_OnSetupTimeout_TP` pa=L1；TN `Cell_OnSetupTimeout_TN` pa=-

```text
摘要（req = 调用方必须满足的安全条件；★ 是访问点）
  Stat_Inc(ulType)
    req R1  W4 @ g_aulCellStat + 4·ulType          需 4·ulType + 4 ≤ 16（即 ulType ≤ 3）   ★1
  Cell_UpdateStat(ulType)
    req R1  原样上传                                path Cell_UpdateStat → Stat_Inc
  Cell_RecordFail(ulCellId, ulCause)
    req R1  需 ulCause ≤ 3                          path Cell_RecordFail → Cell_UpdateStat → Stat_Inc
串联
  Cell_OnSetupTimeout_TP：ulCause = CELL_STAT_BUTT = 4 代入 → 4 ≤ 3 不成立 → L1，穿过 3 层
  Cell_OnSetupTimeout_TN：ulCause = 1 → 成立 → 丢弃
不在摘要里的：两处 LOG_Print、ulCellId —— 它们不流进任何访问的偏移、大小或基址。
```

**LV1-02 名字缓冲区逐层下传，最底层按"长度"位置写结尾符**（S1.1  A1·M1·P1·V0·R1·X0·K0·G0 · 承诺）

UE 名字的格式化分了三层，最底层统一在 pszName[UE_NAME_LEN] 写 '\0'，约定是"缓冲区要比名字长 1"。新加的调用方按 UE_NAME_LEN 定义了数组。　期望：TP `Ue_ShowInfo_TP` pa=L1；TN `Ue_ShowInfo_TN` pa=-

```text
摘要
  Name_Terminate(pszName)
    req R1  W1 @ pszName + 16                       需 17 ≤ ext(pszName)                    ★1
  Name_Format(pszName, ulUeId)
    本函数还写了 pszName[0]、pszName[1]（需 1 ≤ ext、2 ≤ ext），被 R1 覆盖，合并后只留最强的一条
    req R1  需 17 ≤ ext(pszName)                    path Name_Format → Name_Terminate
  Ue_BuildName(pszName, ulUeId)
    req R1  需 17 ≤ ext(pszName)                    path Ue_BuildName → Name_Format → Name_Terminate
串联
  Ue_ShowInfo_TP：pszName = szName，ext = 16 → 17 ≤ 16 不成立 → L1，3 层
  Ue_ShowInfo_TN：ext = 17 → 成立
```

**LV1-03 常量长度的堆内存逐层下传，最底层循环多写一个**（S1.2+S1.3  A1·M4·P1·V0·R1·X0·K2·G0 · 承诺）

消息体按 MSG_BODY_LEN 申请，填充函数用 i <= MSG_BODY_LEN 循环。　期望：TP `Msg_Build_TP` pa=L2；TN `Msg_Build_TN` pa=-

```text
摘要
  Msg_FillPadding(pucBody)
    循环 i ∈ [0, 64]（阈值 widening 收在 64）
    req R1  W1 @ pucBody + [0, 64]                  需 65 ≤ ext(pucBody)                     ★1
  Msg_InitBody(pucBody)      req R1 原样上传
  Msg_Prepare(pucBody)       req R1 原样上传         path Msg_Prepare → Msg_InitBody → Msg_FillPadding
串联
  Msg_Build_TP：pucBody = new(64)（底层分配器配置了字节数下标）→ 访问范围 [0,64] 与大小 64 部分重叠 → L2
  Msg_Build_TN：new(65) → 成立
  分级说明：循环的访问是一段范围，最后一次才越界，按区间口径是 L2（Infer 的口径相同）。
```

**LV1-04 结构体数组成员的地址逐层下传，底层循环用了新规格的宏**（S1.4  A1·M3·P1·V0·R1·X0·K2·G0 · 承诺）

邻区规格从 16 扩到 32，新宏 MAX_NBR_NUM_V2 只改了邻区模块，小区上下文里的数组还是 16。小区建立时把 pstCell->astNbr 传给邻区模块，邻区模块按 32 个清零。　期望：TP `Cell_Setup_TP` pa=L2；TN `Cell_Setup_TN` pa=-

```text
摘要
  Nbr_ResetAll(pstNbr, ulNum)
    i ∈ [0, ulNum − 1]（符号界，narrowing 收出来）；NBR_S 8 字节，usPci 在 +0，ulFlag 在 +4
    req R1  ulNum ≥ 1 ⇒ W2 @ pstNbr + 8·[0, ulNum−1]       需 8·ulNum − 6 ≤ ext(pstNbr)          ★1
    req R2  ulNum ≥ 1 ⇒ W4 @ pstNbr + 8·[0, ulNum−1] + 4   需 8·ulNum ≤ ext(pstNbr)  （覆盖 R1，合并）★2
  Nbr_Clear(pstNbr)        实参 ulNum = 32 代入 → req R2  W4 @ pstNbr + [4, 252]  需 256 ≤ ext(pstNbr)
  Nbr_Init(pstNbr)         req R2 原样上传
  Cell_InitNbrList(pstCell)
    实参 pstCell->astNbr：大小来自成员类型 NBR_S[16] = 128 字节，在这一层就已知
    → 256 ≤ 128 不成立；访问范围 [0,256) 与 128 部分重叠 → L2，3 层（Cell_InitNbrList 是汇合点）
串联
  汇合点不在入口，而在第一个"知道 astNbr 有多大"的函数。入口函数不需要任何条件。
  Cell_Setup_TN 走的是 Cell_InitNbrList_OK（循环用 MAX_NBR_NUM）→ 256 变成 128 → 成立。
```

**LV1-05 结构体扩展后，清零函数按扩展后的大小 memset_s，传进来的却是老结构体**（S1.7  A3·M1·P1·V0·R1·X0·K0·G0 · 承诺）

UE_CTX_EX_S 在 UE_CTX_S 后面追加了扩展能力。释放流程的最底层统一按 EX 清零，但有一条老路径传进来的还是 UE_CTX_S。指针在中间层是 void*，类型信息早就丢了。　期望：TP `Ue_OnDetach_TP` pa=L1；TN `Ue_OnDetach_TN` pa=-

```text
摘要
  Ue_ClearExt(pCtx)
    req R1  W108 @ pCtx + 0       （memset_s 的 destMax = sizeof(UE_CTX_EX_S) = 108）
                                  需 108 ≤ ext(pCtx)                                         ★1
  Ue_ResetCtx / Ue_Release        req R1 原样上传
串联
  Ue_OnDetach_TP：pCtx = &stCtx（UE_CTX_S，40 字节）→ 108 ≤ 40 不成立 → L1，3 层
  Ue_OnDetach_TN：&stCtxEx（108 字节）→ 成立
  要点：大小按"对象"算，不按指针类型算。void* 一路传下去不影响判定。
```

**LV1-06 查表函数自己的常量表，下标从多层之上传入，中间层只是改名转存**（S1.5  A1·M1·P0·V1·R1·X0·K0·G0 · 承诺）

QCI 权重表按 0..8 存 9 个值；有一个调用方把"QCI 个数"当成了 QCI 传进来。中间每层都把参数转存到一个局部变量再往下传。　期望：TP `Sched_InitDefault_TP` pa=L1；TN `Sched_InitDefault_TN` pa=-

```text
摘要
  Qci_GetWeight(ulQci)
    req R1  R4 @ s_aulWeight + 4·ulQci       需 4·ulQci + 4 ≤ 36（ulQci ≤ 8）               ★1
  Bearer_CalcPrio(ulQci)
    ulTmp = ulQci; ulIdx = ulTmp —— 流敏感分析里中间变量就是同一个符号，不需要任何额外机制
    req R1  需 ulQci ≤ 8
  Sched_AddBearer(ulQci)    req R1 原样上传
串联
  Sched_InitDefault_TP：ulQci = MAX_QCI_NUM = 9 → 9 ≤ 8 不成立 → L1
  Sched_InitDefault_TN：ulQci = 9 − 1 = 8 → 成立
```

**LV1-07 跨文件：告警 ID 用了 BUTT，经三个文件三层到达标志数组**（S1.6  A1·M2·P0·V1·R1·X0·K0·G0（跨 TU） · 承诺）

与 LV1-01 同一类错误，但链分布在 alm_top.c / alm_mid.c / alm_low.c 三个文件，用来验收跨文件执行（多轮扫描 + SummaryDB 工作表）。　期望：TP `Alm_ReportAll_TP` pa=L1；TN `Alm_ReportAll_TN` pa=-

```text
摘要（分在三个文件里生成，经 SummaryDB 拼起来）
  alm_low.c  Alm_SetFlag(ulAlmId)    req R1  W1 @ g_aucAlmFlag + ulAlmId   需 ulAlmId ≤ 2     ★1
  alm_low.c  Alm_RaiseOne(ulAlmId)   req R1 原样上传
  alm_mid.c  Alm_Dispatch(ulAlmId, ulCellId)   req R1 原样上传（第 1 轮时 Alm_RaiseOne 的摘要可能还没生成：
             标记 Pending，等第 2 轮）
串联
  Alm_ReportAll_TP：ulAlmId = ALM_ID_BUTT = 3 → 3 ≤ 2 不成立 → L1，trace 跨 3 个文件
  Alm_ReportAll_TN：ALM_ID_CLK_LOSS = 2 → 成立
```

**LV1-08 拷贝模板：源缓冲区逐层下传，最底层按新长度读（读越界）**（S1.7  A3·M2·P1·V0·R1·X0·K0·G0 · 承诺）

配置块从 32 字节扩到 64 字节，最底层的拷贝改成按 CFG_BLOCK_LEN 读，但老的默认模板还是 32 字节。　期望：TP `Cfg_InitDefault_TP` pa=L1；TN `Cfg_InitDefault_TN` pa=-

```text
摘要
  Cfg_CopyBlock(pucDst, pucTmpl)
    req R1  W64 @ pucDst + 0      需 64 ≤ ext(pucDst)       （memcpy_s 的 destMax）           ★1
    req R2  R64 @ pucTmpl + 0     需 64 ≤ ext(pucTmpl)      （memcpy_s 的 count，读源）         ★1
  Cfg_ApplyTmpl / Cfg_Load        req R1、R2 原样上传
串联
  Cfg_InitDefault_TP：pucTmpl = g_aucDefaultTmpl（32）→ R2：64 ≤ 32 不成立 → L1
                      pucDst = 入口形参 pucCfg → R1 停在入口（StillOpen，不报，记账"入口未证明"）
  Cfg_InitDefault_TN：g_aucDefaultTmplV2（64）→ R2 成立；R1 同上
  要点：同一个访问点的两条条件可以有不同的结局，各自独立判定。
```

### Lv2

**LV2-01 把 sizeof(数组) 当成"个数"传下去，底层按个数循环写 UINT16**（S2.1+S2.2  A1·M1·P1·V1·R2·X1·K2·G0 · 承诺）

ERAB 列表接口的第二个参数是"个数"，调用方顺手写成了 sizeof(ausId)（字节数 = 个数 × 2）。　期望：TP `Erab_OnSetupReq_TP` pa=L2；TN `Erab_OnSetupReq_TN` pa=-

```text
摘要
  Erab_FillIds(pusId, ulNum)
    i ∈ [0, ulNum − 1]
    req R1  ulNum ≥ 1 ⇒ W2 @ pusId + 2·[0, ulNum−1]   需 2·ulNum ≤ ext(pusId)                  ★1
  Erab_BuildList / Erab_Setup      req R1 原样上传（两侧——缓冲区和个数——一起沿同一条链下来）
串联
  Erab_OnSetupReq_TP：pusId = ausId（16 字节），ulNum = sizeof(ausId) = 16 → 需 32 ≤ 16；
                      最小那次访问（i = 0）是安全的 → 部分重叠 → L2
  Erab_OnSetupReq_TN：ulNum = 8 → 16 ≤ 16 成立
  要点：条件里同时有"指针"和"个数"两个形参，代换时一起换掉；元素大小 2 由类型给出（字节归一）。
```

**LV2-02 跳过消息头后下传，长度没扣头长（读越界）**（S2.3  A3·M1·P1·V1·R2·X1·K0·G0 · 承诺）

消息处理入口把整条消息的长度原样交给消息体解析，而消息体指针已经跳过了 8 字节头。　期望：TP `Msg_Entry_TP` pa=L1；TN `Msg_Entry_TN` pa=-

```text
摘要
  Body_Copy(pucBody, ulLen, pucOut)
    req R1  W256 @ pucOut + 0          需 256 ≤ ext(pucOut)                                    ★1
    req R2  R ulLen @ pucBody + 0      需 ulLen ≤ ext(pucBody)                                 ★1
  Body_Parse                            R1、R2 原样上传
  Msg_Proc(pucMsg, ulMsgLen, pucOut)
    实参 pucBody = pucMsg + 8：ext(pucMsg + 8) = ext(pucMsg) − 8
    req R2  R ulMsgLen @ pucMsg + 8    需 ulMsgLen + 8 ≤ ext(pucMsg)
  Msg_Proc_OK：实参 ulLen = ulMsgLen − 8 → 需 ulMsgLen ≤ ext(pucMsg)（−8 与 +8 抵消）
串联
  Msg_Entry_TP：pucMsg = aucMsg（64），ulMsgLen = 64 → 72 ≤ 64 不成立 → L1
                pucOut = aucOut（256）→ R1 成立
  Msg_Entry_TN：走 Msg_Proc_OK → 64 ≤ 64 成立
```

**LV2-03 memcpy_s(pkt + off, 整包大小, …)：destMax 没扣偏移，偏移在中间层又加了头长**（S2.4  A3·M1·P1·V1·R2·X1·K0·G0 · 承诺）

往报文里追加一个 IE：中间层把偏移加上 TLV 头长，最底层 destMax 仍然写整包长度。　期望：TP `Pkt_Build_TP` pa=L1；TN `Pkt_Build_TN` pa=-

```text
摘要
  Tlv_WriteVal(pucPkt, ulOff, pucVal, ulValLen)
    req R1  W128 @ pucPkt + ulOff      需 ulOff + 128 ≤ ext(pucPkt)                           ★1
    req R2  R ulValLen @ pucVal + 0    需 ulValLen ≤ ext(pucVal)                              ★1
  Tlv_WriteVal_OK
    destMax = 128 − ulOff：需 ulOff + (128 − ulOff) ≤ ext(pucPkt) → 先化简（ulOff 抵消）
    req R1' W(128 − ulOff) @ pucPkt + ulOff   需 128 ≤ ext(pucPkt)
  Tlv_Append(pucPkt, ulOff, …)        实参 ulOff + 4 → req R1  需 ulOff + 132 ≤ ext(pucPkt)
  Pkt_AddIe                            原样上传
串联
  Pkt_Build_TP：aucPkt（128），ulOff = 16 → 148 ≤ 128 不成立 → L1；
                R2 的 pucVal 是入口形参 → 停在入口，记账
  Pkt_Build_TN：R1' → 128 ≤ 128 成立
```

**LV2-04 按 n 个元素申请，结束标记写在第 n 个（申请与访问同源，不需要知道 n）**（S2.5  A1·M5·P1·V1·R2·X1·K0·G0 · 承诺）

列表约定"最后一项后面放一个结束标记"，申请的时候忘了多申请一个。　期望：TP `List_Create_TP` pa=L1；TN `List_Create_TN` pa=-

```text
摘要
  Item_MarkEnd(pstItem, ulNum)
    req R1  W4 @ pstItem + 8·ulNum + 4      需 8·ulNum + 8 ≤ ext(pstItem)                       ★1
  List_InitItems / List_Init     原样上传
串联
  List_Create_TP：pstItem = new(8·ulNum)（VOS_MemAlloc 的字节数）→ 需 8·ulNum + 8 ≤ 8·ulNum
                  两边相减恒为 −8 → 对任何 ulNum 都不成立 → L1（ulNum 是入口形参也不影响）
  List_Create_TN：new(8·(ulNum + 1)) → 差为 0 → 成立
  要点：变量长度的堆不需要知道具体值，符号相减就能判定。乘法溢出（8·ulNum 回绕）另记，不在这条里。
```

**LV2-05 字节型位图按 UINT32 清零，字数算多了一个**（S2.6+S2.7  A1·M1·P1·V1·R2·X2+X3·K2·G0 · 承诺）

位图按字节定义（MAX_UE_NUM / 8 字节），清零时强转成 UINT32* 按字清，字数写成了 ulBits / 32 + 1 的等价形式（i <= ulBits / 32）。　期望：TP `Cell_ResetUeMap_TP` pa=L2；TN `Cell_ResetUeMap_TN` pa=-

```text
摘要
  Map_ClearWords(pucMap, ulBits)
    pulMap = (UINT32 *)pucMap：同一基址，元素大小换成 4（字节归一后条件仍写在 pucMap 上）
    i ∈ [0, ulBits / 32]，ulBits / 32 作为"除以常量"保留，不降成 Top
    req R1  W4 @ pucMap + 4·[0, ulBits/32]     需 4·(ulBits/32) + 4 ≤ ext(pucMap)            ★1
  Map_ClearWords_OK(pucMap, ulBits)
    req R1' ulBits ≥ 1 ⇒ W4 @ pucMap + 4·[0, (ulBits+31)/32 − 1]   需 4·((ulBits+31)/32) ≤ ext(pucMap)
  Ue_ResetMap / Cell_ClearUeMap     原样上传
串联
  Cell_ResetUeMap_TP：aucMap 8 字节，ulBits = 64 → 64/32 = 2 → 需 12 ≤ 8；i = 0 那次安全 → L2
  Cell_ResetUeMap_TN：(64+31)/32 = 2 → 需 8 ≤ 8 成立
  要点：除法、强转都在 Lv2 能做的范围里——除以常量对区间是精确的，强转只是换元素大小。
```

**LV2-06 置位图：上层按扩容后的 UE 规格循环，下层按 idx / 32 定位字**（S2.7+S3.9  A1·M1·P1·V1·R2·X3·K2·G0 · 承诺）

UE 规格从 64 扩到 256，活动 UE 刷新的循环改成了 MAX_UE_NUM_EX，位图还是 64 位。　期望：TP `Cell_RefreshActive_TP` pa=L2；TN `Cell_RefreshActive_TN` pa=-

```text
摘要
  Bit_Set(pulMap, ulIdx)
    req R1  W4 @ pulMap + 4·(ulIdx / 32)     需 4·(ulIdx/32) + 4 ≤ ext(pulMap)                 ★1
  Ue_MarkActive / Cell_MarkUe                原样上传
串联
  Cell_RefreshActive_TP：调用点在循环里，ulUe ∈ [0, 255] → ulUe/32 ∈ [0, 7] → 需 32 ≤ 8；
                         最小那次安全 → L2（调用点的区间代入，不需要知道循环跑了几次）
  Cell_RefreshActive_TN：ulUe ∈ [0, 63] → 需 8 ≤ 8 成立
  为什么不用 UINT8 下标举例：类型自带的范围不算"代码给出的上界"（§1.3 的口径），
  否则每个 UINT8 / UINT16 字段都会变成 L2。这里的上界来自循环条件。
```

**LV2-07 哈希桶下标写成 key & BUCKET_NUM（应为 BUCKET_NUM − 1）**（S2.7  A1·M1·P1·V5→X3·R2·X3·K0·G0 · 承诺）

经典笔误。key 来自消息（入口形参，没有约束），但掩码本身给出了有限上界。　期望：TP `Tbl_OnAddReq_TP` pa=L2；TN `Tbl_OnAddReq_TN` pa=-

```text
摘要
  Hash_Insert(pulBucket, ulKey, ulVal)
    ulKey & 64 的区间是 [0, 64]（按位与只能取到掩码以内）
    req R1  W4 @ pulBucket + 4·[0, 64]        需 260 ≤ ext(pulBucket)                          ★1
  Hash_Insert_OK：ulKey & 63 → [0, 63] → 需 256 ≤ ext(pulBucket)
  Tbl_Add / Tbl_AddEntry      原样上传（条件里已经没有 ulKey 了——掩码把它"吃掉"了）
串联
  Tbl_OnAddReq_TP：aulBucket（256 字节）→ 260 ≤ 256 不成立，最小访问安全 → L2
  Tbl_OnAddReq_TN：256 ≤ 256 成立
  要点：入口值没有约束，但掩码之后有了代码给出的上界，所以报 L2 而不是停在 S2。
```

**LV2-08 协议栈逐层加头：每层偏移都没问题，加起来超了帧长**（S2.3  A2·M1·P1·V0·R1·X1·K0·G0 · 承诺）

PHY / MAC / RLC 各自只看自己的头长；RLC 新增了 2 字节扩展头后，CRC 位置落到了帧外。　期望：TP `Frame_Build_TP` pa=L1；TN `Frame_Build_TN` pa=-

```text
摘要
  Rlc_FillCrc(pucRlc)        req R1  W1 @ pucRlc + 50       需 51 ≤ ext(pucRlc)                ★1
  Mac_Fill(pucMac)           实参 pucMac + 6 → R1 变成 需 57 ≤ ext(pucMac)
                             （本层的 pucMac[0] 需 1 ≤ ext，被覆盖）
  Phy_Fill(pucFrame)         实参 pucFrame + 8 → 需 65 ≤ ext(pucFrame)
串联
  Frame_Build_TP：aucFrame（64）→ 65 ≤ 64 不成立 → L1，trace 上每一层的偏移都列出来：+8 → +6 → +50
  Frame_Build_TN：aucFrame（72）→ 成立
  要点：偏移是线性累加的，每过一层只改常数项，条件的形状不变。
```

**LV2-09 无符号下标减一回绕：pos 为 0 时取"前一个"**（S2.2  A1·M1·P1·V1·R2·X1·K0·G0/G1 · 承诺）

历史记录取前一项；有一个调用方在初始化时传了 0。UINT32 的 0 − 1 是 0xFFFFFFFF。　期望：TP `Ue_CheckJitter_TP` pa=L1；TN `Ue_CheckJitter_TN` pa=-

```text
摘要
  Hist_GetPrev(pucHist, ulPos)
    偏移是无符号减法 (ulPos − 1) mod 2^32，BoundExpr 记成带回绕标记的线性式
    req R1  R1 @ pucHist + (ulPos − 1)ᵘ      需 (ulPos − 1)ᵘ + 1 ≤ ext(pucHist)                ★1
  Hist_GetPrevSafe(pucHist, ulPos)
    本函数先判 ulPos == 0 就返回 → 条件带守卫：
    req R1' ulPos ≥ 1 ⇒ R1 @ pucHist + ulPos − 1     需 ulPos ≤ ext(pucHist)
  Ue_CalcJitter / Ue_UpdateJitter     原样上传
串联
  Ue_CheckJitter_TP：ulPos = 0 → 偏移 0xFFFFFFFF → L1
  Ue_CheckJitter_TN：走 Safe 版本，ulPos = 0 → 守卫为假 → 安全
  要点：守卫让"传 0"这个调用点在 TN 里不误报（Lv3 的"条件带守卫"在这里已经要用到）。
```

### Lv3

**LV3-01 消息入口校验写成 > MAX（应为 >=），小区号三层之后索引全局上下文表**（S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 · 承诺）

多层越界最常见的真实形态：校验在入口、访问在三层之外的另一个模块，两处写代码的人不是同一个，校验差了一。　期望：TP `Cell_OnSetupReq_TP` pa=L2；TN `Cell_OnSetupReq_TN` pa=-

```text
摘要
  Cell_SetState(ulCellId, ulState)
    CELL_CTX_S 32 字节，ulState 在 +0
    req R1  W4 @ g_astCellCtx + 32·ulCellId     需 32·ulCellId + 4 ≤ 512（即 ulCellId ≤ 15）  ★1
  Cell_Activate / Cell_ProcSetup               原样上传
串联
  Cell_OnSetupReq_TP：ulCellId 来自消息（入口无约束），但校验 "> 16 就返回" 把调用点的区间收窄成 [0, 16]
                      → 16 不满足 ≤ 15，0..15 满足 → L2（上界来自代码里的校验，按口径要报）
  Cell_OnSetupReq_TN：">= 16" → [0, 15] → 成立
  要点：校验在调用方、访问在三层之下，靠的是"调用点处的区间"代入被调者的条件。
```

**LV3-02 校验用接入模块的规格宏，数组用上下文模块的老规格宏**（S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 · 承诺）

接入模块按"每小区 64 个 UE"校验，上下文模块的数组还是 32。两个宏在不同头文件里。　期望：TP `Ue_OnAttach_TP` pa=L2；TN `Ue_OnAttach_TN` pa=-

```text
摘要
  UeCtx_SetCrnti(ulUeIdx, usCrnti)
    UE_CTX_S 12 字节，usCrnti 在 +4
    req R1  W2 @ g_astUeCtx + 12·ulUeIdx + 4     需 12·ulUeIdx + 6 ≤ 384（ulUeIdx ≤ 31）        ★1
  UeCtx_Init / Ue_CreateCtx / Ue_ProcAttach     原样上传（4 层）
串联
  Ue_OnAttach_TP：校验 ">= 64 返回" → [0, 63] → 32..63 不满足 → L2，4 层
  Ue_OnAttach_TN：校验 ">= MAX_UE_CTX_NUM" → [0, 31] → 成立
```

**LV3-03 访问函数自己校验了，但校验的界比多层之上传进来的数组大（TN 是常量调用点，不能误报）**（S3.2  A1·M2·P1·V5→G1·R2·X1·K1·G1 · 承诺）

端口配置函数按 V2 规格（32 口）自检，单板上的端口表还是 16 口。同一个函数还被初始化流程用常量 3 调用——那个调用点是安全的，不能报。　期望：TP `Board_OnCfgMsg_TP` pa=L2；TN `Board_InitDefault_TN` pa=-

```text
摘要
  Port_SetMode(astPort, ulPortId, ulMode)
    PORT_S 24 字节；先判 ulPortId >= 32 就返回 → 条件带守卫：
    req R1  ulPortId < 32 ⇒ W4 @ astPort + 24·ulPortId     需 24·ulPortId + 4 ≤ ext(astPort)   ★1
    （只留数值区间 [0,31] 的写法会变成"需 748 ≤ ext(astPort)"，常量 3 的调用点也被判越界——这就是要带守卫的原因）
  Port_ApplyCfg / Board_CfgPort    原样上传
串联
  Board_OnCfgMsg_TP：astPort = g_astBoardPort（384 字节），ulPortId 入口无约束
                     → 守卫给出 [0, 31] → 24·31 + 4 = 748 > 384，最小访问安全 → L2
  Board_InitDefault_TN：ulPortId = 3 → 守卫为真，24·3 + 4 = 76 ≤ 384 → 安全
```

**LV3-04 按源列表的个数循环拷贝，目的数组是老规格**（S3.4  A1·M1·P1·V5→G2·R2·X1·K2·G2 · 承诺）

邻区更新消息最多带 32 个邻区（入口按 32 校验），本地缓存数组还是 16。　期望：TP `Cell_OnNbrUpdate_TP` pa=L2；TN `Cell_OnNbrUpdate_TN` pa=-

```text
摘要
  Nbr_CopyN(astDst, astSrc, ulNum)
    i ∈ [0, ulNum − 1]，NBR_S 4 字节
    req R1  ulNum ≥ 1 ⇒ W4 @ astDst + 4·[0, ulNum−1]   需 4·ulNum ≤ ext(astDst)               ★1
    req R2  ulNum ≥ 1 ⇒ R4 @ astSrc + 4·[0, ulNum−1]   需 4·ulNum ≤ ext(astSrc)               ★1
  Nbr_Sync / Cell_SyncNbr                原样上传
串联
  Cell_OnNbrUpdate_TP：astDst = astLocal（64 字节），ulNum 经校验 ∈ [0, 32] → 需 128 ≤ 64 → L2
                       R2 的 astSrc 是入口形参 → 停在入口，记账
  Cell_OnNbrUpdate_TN：校验按 16 → 需 64 ≤ 64 成立
```

**LV3-05 填满后补结尾符：循环退出时计数器等于长度**（S3.5  A1·M1·P1·V1·R2·X0·K2·G0 · 承诺）

小区名按长度填空格再补 '\0'，调用方传的长度是 sizeof(缓冲区)，没有给 '\0' 留位置。　期望：TP `Cell_SetDefaultName_TP` pa=L1；TN `Cell_SetDefaultName_TN` pa=-

```text
摘要
  Name_Pad(pszName, ulLen)
    循环里 i ∈ [0, ulLen − 1]；退出时用退出条件收窄（narrowing）：i ≥ ulLen 且 i ≤ ulLen → i == ulLen
    req R1  ulLen ≥ 1 ⇒ W1 @ pszName + [0, ulLen−1]   需 ulLen ≤ ext(pszName)
    req R2  W1 @ pszName + ulLen                     需 ulLen + 1 ≤ ext(pszName)   （覆盖 R1）  ★1
  Name_Init / Cell_InitName             原样上传
串联
  Cell_SetDefaultName_TP：szName（16），ulLen = 16 → 17 ≤ 16 不成立 → L1（退出后的那次访问是确定值）
  Cell_SetDefaultName_TN：ulLen = 15 → 16 ≤ 16 成立
```

**LV3-06 按制式选长度：NR 的能力长度超过了缓冲区（分支合并后一支越界）**（S3.3  A3·M1·P1·V1·R2·X0·K1·G0 · 尽力）

UE 能力缓存按 LTE 时代的 32 字节定义，NR 能力 48 字节。　期望：TP `Ue_OnCapInfo_TP` pa=L2；TN `Ue_OnCapInfo_TN` pa=-

```text
摘要
  Cap_Store(pucDst, pucSrc, ulLen)
    req R1  W ulLen @ pucDst + 0     需 ulLen ≤ ext(pucDst)                                    ★1
    req R2  R ulLen @ pucSrc + 0     需 ulLen ≤ ext(pucSrc)                                    ★1
  Cap_Save / Ue_SaveCap             原样上传
串联
  Ue_OnCapInfo_TP：ulLen 在两个分支是 48 和 24，合并成 [24, 48]；aucCap 32 → 部分不满足 → L2
                   （保留两个分支各自的值能给出"NR 分支确定越界"，年内不做，所以是尽力）
  Ue_OnCapInfo_TN：aucCap 48 → 成立；R2 的 pucSrc 是入口形参 → 记账
```

**LV3-07 报文组装循环：按剩余长度拷贝（安全） vs. destMax 用整包长度（缺陷）**（S3.7+S3.8  A3·M1·P1·V1·R2·X1·K3·G1 · 承诺（TN）/ 尽力（TP））

最常见的报文组装写法。安全写法里 destMax = 总长 − 偏移；有人复制粘贴时漏了 − 偏移。　期望：TP `Msg_Encode_TP` pa=L2；TN `Msg_Encode_TN` pa=-

```text
摘要
  Pkt_PutIes_OK(pucPkt, ulPktLen, astIe, ulIeNum)
    访问点：memcpy_s(pucPkt + ulOff, ulPktLen − ulOff, …)
    先符号化简：ulOff + (ulPktLen − ulOff) = ulPktLen，ulOff 抵消，与它的循环区间无关
    req R1  W(ulPktLen − ulOff) @ pucPkt + ulOff     化简后 需 ulPktLen ≤ ext(pucPkt)            ★1
    req R2  R astIe[i].ulLen @ astIe[i].pucVal       基址来自数组元素的字段 → 不跟踪，记账
  Pkt_PutIes(pucPkt, ulPktLen, …)
    访问点：memcpy_s(pucPkt + ulOff, ulPktLen, …)；循环内校验 ulOff + len ≤ ulPktLen 给出 ulOff ∈ [0, ulPktLen]
    req R1  W ulPktLen @ pucPkt + [0, ulPktLen]     需 2·ulPktLen ≤ ext(pucPkt)，最小访问需 ulPktLen ≤ ext ★2
  Body_PutIes / Msg_BuildBody          原样上传
串联
  Msg_Encode_TN：aucPkt（128），ulPktLen = 128 → 128 ≤ 128 成立 → 不报
  Msg_Encode_TP：→ 需 256 ≤ 128 不成立，ulOff = 0 那次成立 → L2
  要点：TN 一侧不需要任何循环不变式；不先化简、直接拿 ulOff 的区间去比，这里会误报。
```

**LV3-08 调用链在循环里：上层遍历小区用了 <=，三层之下按小区号清上下文**（S3.9  A3·M2·P0·V1·R1·X1·K2·G0 · 承诺）

全小区复位流程，循环条件多了一个等号。　期望：TP `Cell_ResetAll_TP` pa=L2；TN `Cell_ResetAll_TN` pa=-

```text
摘要
  Ctx_Clear(ulCellId)
    memset_s(&g_astCellCtx[ulCellId], 32, 0, 32)
    req R1  W32 @ g_astCellCtx + 32·ulCellId    需 32·ulCellId + 32 ≤ 512（ulCellId ≤ 15）      ★1
  Cell_FreeRes / Cell_Release                原样上传
串联
  Cell_ResetAll_TP：调用点在循环里，i ∈ [0, 16] → L2
  Cell_ResetAll_TN：i ∈ [0, 15] → 成立
```

**LV3-09 调度网格清零：行列宏写反（嵌套循环，各维界是常量）**（S3.6  A1·M1·P1·V0·R1·X1·K2·G0 · 承诺）

网格按 SLOT_NUM × RB_NUM 定义，按行优先线性化；清零时外层循环用了 RB_NUM。　期望：TP `Sched_InitGrid_TP` pa=L2；TN `Sched_InitGrid_TN` pa=-

```text
摘要
  Grid_Clear(pucGrid)
    s ∈ [0, 7]，r ∈ [0, 3]；s·8 + r ∈ [0, 59]（乘常量、加区间都是精确的）
    req R1  W1 @ pucGrid + [0, 59]       需 60 ≤ ext(pucGrid)                                      ★1
  Grid_Clear_OK                         s ∈ [0, 3] → [0, 27]  需 28 ≤ ext(pucGrid)
  Sched_ResetGrid / Sched_PrepareSlot   原样上传
串联
  Sched_InitGrid_TP：aucGrid（32）→ 60 ≤ 32 不成立，最小访问安全 → L2
  Sched_InitGrid_TN：28 ≤ 32 成立
  要点：r * COLS 里 COLS 是常量才这么精确；COLS 换成变量就是 S7.6 / S8.3（不承诺）。
```

**LV3-10 校验之后又改了下标："取下一个位置"没再校验**（S3.1  A1·M2·P0·V5→G2·R1·X1·K1·G2 · 承诺）

切换流程先校验源侧下标，再按"源 + 1"取目标侧预留位置。校验只覆盖了源。　期望：TP `Ue_OnHoPrepare_TP` pa=L2；TN `Ue_OnHoPrepare_TN` pa=-

```text
摘要
  Ho_MarkSlot(ulSlot)
    req R1  W4 @ g_aulHoSlot + 4·ulSlot     需 4·ulSlot + 4 ≤ 128（ulSlot ≤ 31）                 ★1
  Ho_Reserve / Ue_PrepareHo               原样上传
串联
  Ue_OnHoPrepare_TP：校验后 ulIdx ∈ [0, 31]；ulNext = ulIdx + 1 ∈ [1, 32] → L2
                     （流敏感：校验收窄的是 ulIdx，ulNext 是它 + 1 之后的新值，区间跟着平移）
  Ue_OnHoPrepare_TN：校验写成 ulIdx + 1 >= 32 → ulIdx ∈ [0, 30] → ulNext ∈ [1, 31] 成立
```

### Lv4

**LV4-01 三层封装的分配器按老规格申请，初始化按新规格遍历**（S4.1  A1·M4·P2+P1·V0·R1·X1·K2·G0 · 承诺）

UE 表由模块自己的分配器申请（Ue_AllocTbl → Cell_MemAlloc → Pub_MemAlloc → VOS_MemAlloc），初始化循环用了扩容后的 MAX_UE_NUM_EX。大小信息先往上走三层、再往下走三层（V 形）。　期望：TP `Ue_ModuleInit_TP` pa=L2；TN `Ue_ModuleInit_TN` pa=-

```text
摘要（eff = 对调用方的效果）
  Pub_MemAlloc(ulSize)       eff ret = new(ulSize) ∨ NULL         （由 MEM_ALLOC_SIZE 给 VOS_MemAlloc 的字节数下标）
  Cell_MemAlloc(ulSize)      eff ret = new(ulSize) ∨ NULL         （LOG_Print 不影响效果）
  Ue_AllocTbl()              eff ret = new(512) ∨ NULL            （32 × sizeof(UE_S) = 32 × 16）
  Ue_InitOne(pstTbl, ulIdx)  req R1  W4 @ pstTbl + 16·ulIdx      需 16·ulIdx + 4 ≤ ext(pstTbl)          ★1
  Ue_InitTbl(pstTbl, ulNum)  调用点 i ∈ [0, ulNum − 1] 代入
                             req R1  ulNum ≥ 1 ⇒ W4 @ pstTbl + 16·[0, ulNum−1]   需 16·ulNum − 12 ≤ ext(pstTbl)
  Ue_Setup(pstTbl)           实参 ulNum = 64 → 需 1012 ≤ ext(pstTbl)
串联
  Ue_ModuleInit_TP：pstTbl = Ue_AllocTbl() 的返回 → ext = 512（NULL 分支已被判空排除）
                    → 1012 ≤ 512 不成立，最小访问安全 → L2
  Ue_ModuleInit_TN：走 Ue_Setup_OK（ulNum = 32）→ 需 500 ≤ 512 成立
```

**LV4-02 申请写成 sizeof(指针)，三层之下按结构体成员写**（S4.1  A3·M4·P2+P1·V0·R1·X1·K0·G0 · 承诺）

经典笔误 sizeof(pstSec)。目标是 ILP32，申请到的是 4 字节；host 上分析是 8 字节——两种都越界。　期望：TP `Ue_CreateSec_TP` pa=L1；TN `Ue_CreateSec_TN` pa=-

```text
摘要
  Pub_MemAlloc(ulSize)       eff ret = new(ulSize) ∨ NULL
  Sec_SetKey(pstSec, pucKey)
    aucKey 在 +8，32 字节
    req R1  W32 @ pstSec + 8       需 40 ≤ ext(pstSec)                                         ★1
    req R2  R32 @ pucKey + 0       需 32 ≤ ext(pucKey)                                         ★1
  Sec_Init / Ue_InitSec      原样上传
串联
  Ue_CreateSec_TP：ext(pstSec) = sizeof(SEC_CTX_S *) = 4（ILP32；按 host 算是 8）→ 40 ≤ 4 不成立 → L1
                   R2 的 pucKey 是入口形参 → 记账
  Ue_CreateSec_TN：sizeof(*pstSec) = 40 → 成立
  要点：sizeof 的值按被分析代码的目标 ABI 算；这条两种 ABI 结论一样，有的用例不一样（T0-1c）。
```

**LV4-03 带头部的缓冲池分配器：调用方按"长度 + 结尾符"写**（S4.2  A1·M5·P2+P1·V1·R2·X1·K0·G0 · 承诺）

缓冲池在用户区前面放 16 字节管理头，返回用户区指针。按形参名猜长度会把 ulLen + 16 当大小（错），摘要推导得到的是 ext(返回值) = ulLen（对）。　期望：TP `Msg_MakeText_TP` pa=L1；TN `Msg_MakeText_TN` pa=-

```text
摘要
  Buf_Alloc(ulLen)
    pucRaw = new(ulLen + 16)；返回 pucRaw + 16
    eff ret = new(ulLen + 16) + 16 ∨ NULL       即 ext(ret) = ulLen
  Msg_AllocBody(ulBodyLen)    eff ext(ret) = ulBodyLen
  Txt_Terminate(pucTxt, ulLen) req R1  W1 @ pucTxt + ulLen    需 ulLen + 1 ≤ ext(pucTxt)          ★1
  Txt_Fill / Msg_FillText      原样上传
串联
  Msg_MakeText_TP：pucTxt = Msg_AllocBody(32) → ext = 32；ulLen = 32 → 33 ≤ 32 不成立 → L1
  Msg_MakeText_TN：Msg_AllocBody(33) → 成立
```

**LV4-04 出参分配器申请报告缓冲，填充时按"报告长 + 尾长"清零**（S4.3  A3·M4·P2+P1·V0·R1·X1·K0·G0 · 承诺）

报告缓冲由 Buf_Create(&p, len) 申请；后来报告加了 8 字节尾，填充改了，申请没改。　期望：TP `Rpt_Send_TP` pa=L1；TN `Rpt_Send_TN` pa=-

```text
摘要
  Buf_Create(ppucBuf, ulLen)     eff *ppucBuf = new(ulLen) ∨ NULL；ret == VOS_OK ⇒ *ppucBuf ≠ NULL
  Rpt_CreateBuf(ppucBuf)         eff *ppucBuf = new(64) ∨ NULL（实参 RPT_LEN 代入）
  Rpt_Clear(pucBuf, ulLen)       req R1  W ulLen @ pucBuf + 0   需 ulLen ≤ ext(pucBuf)             ★1
  Rpt_Fill / Rpt_Prepare         实参 ulLen = 72 → 需 72 ≤ ext(pucBuf)
串联
  Rpt_Send_TP：出参效果把 pucBuf 记成 new(64) → 72 ≤ 64 不成立 → L1
  Rpt_Send_TN：Rpt_CreateBuf_OK 申请 72 → 成立
```

**LV4-05 个数来自"取配置并截断"的 getter（两层），使用处循环多了一个等号**（S4.4  A1·M1·P1·V2·R3·X0·K2·G0 · 承诺）

DRB 个数从配置读，取值函数把它截到 MAX_DRB_NUM；复位循环写成了 i <= ulNum。　期望：TP `Ue_OnReconfig_TP` pa=L2；TN `Ue_OnReconfig_TN` pa=-

```text
摘要
  Cfg_ClampDrbNum(ulCfg)     eff ret ∈ [0, 8]（两个分支：8 或 ulCfg 且 ulCfg ≤ 8）
  Drb_GetMaxNum(ulCfg)       eff ret ∈ [0, 8]                       ← 返回值范围自底向上
  Drb_ResetOne(astDrb, i)    req R1  W4 @ astDrb + 8·i       需 8·i + 4 ≤ ext(astDrb)            ★1
  Drb_ResetAll(astDrb, ulNum)
    调用点 i ∈ [0, ulNum]
    req R1  W4 @ astDrb + 8·[0, ulNum]      需 8·ulNum + 4 ≤ ext(astDrb)
  Ue_ResetDrb               原样上传
串联
  Ue_OnReconfig_TP：ulNum = Drb_GetMaxNum(...) → ∈ [0, 8]（ensures 代入）；astDrb 64 字节
                    → 8·8 + 4 = 68 > 64，ulNum = 0 时安全 → L2
  Ue_OnReconfig_TN：Drb_ResetAll_OK（i < ulNum）→ 需 8·ulNum − 4 ≤ 64 → 成立
  要点：长度从"兄弟链"的返回值来（异链汇合最简单的形态），靠的是返回值的取值范围。
```

**LV4-06 校验封装在独立函数里（返回布尔），校验函数自己差了一**（S4.6（v3.11 新增）  A3·M2·P0·V5→G3·R1·X1·K1·G3 · 尽力）

业务代码几乎都这么写：if (Cell_CheckPara(id) != VOS_OK) return; 真正的比较在两层之下。要把"返回值为真 ⇒ 参数在范围内"这条条件效应也自底向上传。　期望：TP `Cell_OnDelReq_TP` pa=L2；TN `Cell_OnDelReq_TN` pa=-

```text
摘要
  Cell_IsValidId(ulCellId)     eff ret ≠ 0 ⇒ ulCellId ≤ 16；ret = 0 ⇒ ulCellId ≥ 17          （条件效应）
  Cell_CheckPara(ulCellId)     eff ret = VOS_OK ⇒ ulCellId ≤ 16                               （沿返回值再上传一层）
  Ctx_Clear(ulCellId)          req R1  W32 @ g_astCellCtx + 32·ulCellId    需 ulCellId ≤ 15        ★1
  Cell_FreeCtx / Cell_Delete   原样上传
串联
  Cell_OnDelReq_TP：分支 "!= VOS_OK 就返回" 之后，应用条件效应 → ulCellId ∈ [0, 16] → L2
  Cell_OnDelReq_TN：校验函数用 < → [0, 15] → 成立
  不支持条件效应时：TP 里 ulCellId 停在"入口无约束" → 不报（漏报，不是误报）。所以先列为尽力。
```

**LV4-07 分配器的函数体不在分析范围里，只出现在 config.ini / alloc_free_identity 结果里**（S4.5  A1·M5·P2+P1·V0·R1·X0·K0·G0 · 规则性处理）

第三方内存池 Ext_PoolAlloc 只有头文件。config.ini 里配了 MEM_ALLOC=Ext_PoolAlloc,-1，但没有配字节数下标。　期望：TP `Pool_UseBlock_TP` pa=记账 · later=L1；TN `Pool_UseBlock_TN` pa=-

```text
摘要
  Ext_PoolAlloc               无函数体；按配置：eff ret = new(?) ∨ NULL —— 知道是新堆，不知道多大
  Blk_SetTail(pucBlk, ulPos)  req R1  W1 @ pucBlk + ulPos   需 ulPos + 1 ≤ ext(pucBlk)            ★1
  Blk_Init / Pool_PrepareBlk  原样上传
串联
  Pool_UseBlock_TP：ulPos = 64；ext(pucBlk) = ext(new@Ext_PoolAlloc) 是符号 → 判不了 → 不报，记账"分配大小未知"
                    补上 MEM_ALLOC_SIZE=Ext_PoolAlloc,1 之后：ext = 64 → 65 ≤ 64 不成立 → L1
  Pool_UseBlock_TN：写 63 → 需 64 ≤ ext；同样判不了，但它本来就没问题
  要点：不知道大小时宁可不报，不能拿"未知"当 0 去比（那样每个外部分配都会误报）。
```

### Lv5

**LV5-01 编码上下文在链顶填好（缓冲区 + 长度），长度用了新宏，缓冲区还是老大小**（S5.1  A3·M1·P3·V3·R2·X0·K0·G0 · 承诺）

编码器的上下文结构体一路以指针下传，最底层按 pstCtx->ulBufLen 当 destMax。链顶初始化时长度写了 ENC_BUF_LEN（256），缓冲区数组是 ENC_BUF_LEN_OLD（128）。　期望：TP `Rrc_EncodeMsg_TP` pa=L1；TN `Rrc_EncodeMsg_TN` pa=-

```text
摘要（基址和长度都是"形参指向的结构体的字段"——字段级 AccessPath）
  Enc_PutBytes(pstCtx, pucData, ulLen)
    req R1  W pstCtx->ulBufLen @ pstCtx->pucBuf + 0     需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)  ★1
    req R2  R ulLen @ pucData + 0                        需 ulLen ≤ ext(pucData)                  ★1
  Enc_PutIe / Enc_Encode                                 原样上传（字段路径不变，形参换名）
串联
  Rrc_EncodeMsg_TP：实参 &stCtx；帧内记录了 stCtx.pucBuf = aucBuf（ext 128）、stCtx.ulBufLen = 256
                    → R1 代入 → 256 ≤ 128 不成立 → L1，3 层
                    R2：pucData = aucIe（16），ulLen = 16 → 成立
  Rrc_EncodeMsg_TN：ulBufLen = sizeof(aucBuf) → 成立
  要点：要在链顶函数里记住"局部结构体的字段现在指向谁、值是多少"（帧内字段存储）。
```

**LV5-02 上下文由独立的 Init 函数填：长度多算了尾部，缓冲区按原长申请（写效应）**（S5.2  A3·M5·P3+P2·V3·R3·X1·K0·G0 · 2027H1）

与 LV5-01 同一类错误，区别是缓冲区和长度在 Enc_Init 里被写进结构体字段——链顶看到的只是一次函数调用。　期望：TP `Rrc_EncodeSib_TP` pa=记账 · later=L1；TN `Rrc_EncodeSib_TN` pa=记账 · later=-

```text
摘要
  Enc_Init(pstCtx, ulLen)
    eff（写效应，在 ret == VOS_OK 时成立）
        pstCtx->pucBuf   := new(ulLen)
        pstCtx->ulBufLen := ulLen + 4
        pstCtx->ulUsed   := 0
  Enc_PutBytes / Enc_PutIe / Enc_Encode   同 LV5-01：req R1  需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)
串联
  Rrc_EncodeSib_TP：Enc_Init(&stCtx, 128) 返回 OK 的分支上，把写效应应用到 stCtx
                    → pucBuf = new(128)、ulBufLen = 132 → R1：132 ≤ 128 不成立 → L1
  年内（没有写效应）：stCtx 的字段在调用后变成未知 → R1 停在链顶判不了 → 不报，记账"字段被调用改写"（漏报，不误报）
```

**LV5-03 IE 长度沿另一条链（按类型查表、两层返回）得到，缓冲区沿主链下传**（S5.3  A3·M1·P1·V2·R3·X0·K1·G0 · 尽力）

按 IE 类型取最大长度的函数里新增了一种 64 字节的 IE，接收缓冲区还是 32。　期望：TP `Ue_OnIeInd_TP` pa=L2；TN `Ue_OnIeInd_TN` pa=-

```text
摘要
  Ie_LookupLen(ucType)       eff ret ∈ [0, 64]       （switch 的各分支：16、24、64、默认 0）
  Ie_GetMaxLen(ucType)       eff ret ∈ [0, 64]       （返回值范围再上传一层）
  Ie_Store(pucDst, pucSrc, ulLen)  req R1  W ulLen @ pucDst   需 ulLen ≤ ext(pucDst)            ★1
                                   req R2  R ulLen @ pucSrc   需 ulLen ≤ ext(pucSrc)            ★1
  Ie_Save / Ue_SaveIe              原样上传
串联
  Ue_OnIeInd_TP：ulLen = Ie_GetMaxLen(...) ∈ [0, 64]；aucIe 32 → L2；R2 的 pucIe 是入口形参 → 记账
  Ue_OnIeInd_TN：aucIe 64 → 成立
  为什么是尽力：返回值范围靠各分支合并，链上任何一层不精确都会退成更宽的区间。
```

**LV5-04 中间层打开扩展字段时把长度加了，缓冲区没变**（S5.4  A3·M1·P3·V3·R2·X1·K0·G1 · 尽力（S5.4 里"本层先改字段再下传"的形态，用 S5.1 的帧内字段跟踪即可；被调者改写字段的形态见 LV5-02，2027H1））

上下文在链顶正确初始化（128 / 128），中间一层"打开扩展"时把 ulBufLen 加了 16，没有换更大的缓冲区。　期望：TP `Rrc_EncodeExt_TP` pa=L1；TN `Rrc_EncodeExt_TN` pa=-

```text
摘要
  Enc_PutBytes(pstCtx, …)    req R1  需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)                   ★1
  Enc_WithExt(pstCtx, …)
    本函数先写 pstCtx->ulBufLen += 16，再调用 → 代换时 pstCtx->ulBufLen 取"调用点处的值"
    req R1  需 pstCtx->ulBufLen + 16 ≤ ext(pstCtx->pucBuf)     （以本函数入口时的字段值表示）
    eff     pstCtx->ulBufLen := pstCtx->ulBufLen + 16
  Enc_EncodeExt              原样上传
串联
  Rrc_EncodeExt_TP：stCtx.ulBufLen = 128、pucBuf = aucBuf（128）→ 144 ≤ 128 不成立 → L1
  Rrc_EncodeExt_TN：缓冲区 144 → 成立
```

**LV5-05 字段级申请（alloc_free_identity 里带 offset 的那类）：资源缓冲写尾越界**（S5.5  A1·M4·P3+P2·V0·R1·X0·K0·G0 · 尽力）

Ue_AllocRes 把申请结果写进 pstUe->pucRes（JSON 里是 para 0、offset [4]）；三层之下的填充函数按 RES_LEN 位置写校验字节。　期望：TP `Ue_OnSetup_TP` pa=L1；TN `Ue_OnSetup_TN` pa=-

```text
摘要
  Ue_AllocRes(pstUe)       eff pstUe->pucRes := new(64) ∨ NULL；ret == VOS_OK ⇒ pstUe->pucRes ≠ NULL
                           （与 alloc_free_identity 的 {para: 0, offset: [4]} 交叉校验一致）
  Res_WriteChk(pucRes, ulPos)   req R1  W1 @ pucRes + ulPos     需 ulPos + 1 ≤ ext(pucRes)           ★1
  Res_Fill(pstUe, ulPos)        实参 pstUe->pucRes → req R1  需 ulPos + 1 ≤ ext(pstUe->pucRes)
  Ue_PrepareRes                 原样上传
串联
  Ue_OnSetup_TP：Ue_AllocRes(&stUe) 返回 OK 的分支上 stUe.pucRes = new(64)，ulPos = 64 → 65 ≤ 64 不成立 → L1
  Ue_OnSetup_TN：校验字节写在 RES_LEN − 1 → 成立
```

**LV5-06 菱形：缓冲区从 A 链（取块，三层）、长度从 B 链（取配置，两层）分别回到链顶，再一起下传**（S4.1+S4.4 组合（R3）  A3·M4·P2+P1·V2·R3·X0·K0·G0 · 承诺（用 Lv4 的返回值效应就够））

报告块大小 64，上报长度配置从 48 改成了 96，两边由不同的人维护。　期望：TP `Rpt_Flush_TP` pa=L1；TN `Rpt_Flush_TN` pa=-

```text
摘要
  Pool_GetBlk(ulLen)        eff ret = new(ulLen) ∨ NULL
  Rpt_BlkAlloc()            eff ret = new(64) ∨ NULL
  Rpt_GetBuf()              eff ret = new(64) ∨ NULL              （A 链：大小往上走 3 层）
  Cfg_GetRptLen()           eff ret == 96
  Rpt_GetLen()              eff ret == 96                         （B 链：值往上走 2 层）
  Rpt_Write(pucBuf, ulLen, pucData)   req R1  需 ulLen ≤ ext(pucBuf)；R2 需 ulLen ≤ ext(pucData)  ★1
  Rpt_Store / Rpt_Commit             原样上传
串联
  Rpt_Flush_TP：pucBuf = new(64)，ulLen = 96 → R1：96 ≤ 64 不成立 → L1；R2：aucData 128 → 成立
  Rpt_Flush_TN：Rpt_GetLen_OK 返回 48 → 成立
  要点：汇合点在链顶，两侧都是"往上走"的效应（V 形 + 菱形），不需要 Lv5 的字段能力。
```

### Lv6

**LV6-01 跨文件：extern 不完整声明的全局统计表，下标常量从三层之上传入**（S6.1  A1·M2·P4·V1·R1·X0·K0·G0（跨 TU） · 尽力）

本文件只有 extern UINT32 g_aulPortStat[];——看不到大小。表定义在 port_stat_def.c 里，16 项。　期望：TP `Port_OnLinkDown_TP` pa=L1；TN `Port_OnLinkDown_TN` pa=-

```text
摘要
  Stat_PortInc(ulPort)
    req R1  W4 @ g_aulPortStat + 4·ulPort     需 4·ulPort + 4 ≤ ext(g_aulPortStat)               ★1
    （本文件里 ext(g_aulPortStat) 是未知符号，所以条件留成符号，不在这里判）
  Port_CountDown / Port_HandleDown           原样上传
串联
  Port_OnLinkDown_TP：ulPort = 16 → 需 68 ≤ ext(g_aulPortStat)
                      查跨 TU 全局大小表（扫 port_stat_def.c 时登记 g_aulPortStat = 64 字节）→ 68 ≤ 64 不成立 → L1
  没有全局大小表时：条件停在 ext(g_aulPortStat) 上 → 不报，记账"全局大小未知"
```

**LV6-02 全局规格变量只在初始化函数里写常量（新规格 64），数组还是老规格 32**（S6.2  A1·M2·P0·V4·R1·X0·K2·G0 · 2027H1）

规格值放在全局变量里，Spec_Init 写一次；三层之下的清理循环按它遍历全局 UE 表。　期望：TP `Cell_ResetUe_TP` pa=记账 · later=L2；TN `Cell_ResetUe_TN` pa=记账 · later=-

```text
摘要
  Ue_ClearAll()
    g_ulMaxUeNum 以"函数入口时的全局值"作符号 G
    req R1  G ≥ 1 ⇒ W4 @ g_astUe + 16·[0, G−1]     需 16·G ≤ 512                               ★1
  Ue_Release / Cell_ReleaseUe                       原样上传（全局符号 G 一路不变）
串联
  年内：链顶也不知道 G → 停在入口，记账"依赖全局值"
  2027：全局取值集合 = {0（静态初始化）, 64（Spec_Init）} → G ∈ [0, 64] → 需 1024 ≤ 512 → L2
        TN 版本的全局只被写成 32 → 成立
  说明：经全局变量传递没有"层数"可言，按跨函数处理；这里的调用链只是让访问点在 3 层之下。
```

**LV6-03 运行期申请的全局表：大小由 g_ulTblNum 决定，访问前按 > g_ulTblNum 校验（应为 >=）**（S6.3  A1·M5·P4·V4·R1·X1·K1·G4 · 2027（程序分析验证 + AI 提不变式））

全局资源在模块初始化时按配置申请；各处访问只认 g_ulTblNum。　期望：TP `Svc_OnSetReq_TP` pa=记账 · ai+pa=L2；TN `Svc_OnSetReq_TN` pa=记账 · ai+pa=-

```text
摘要
  Tbl_Init(ulNum)
    eff g_pulTbl := new(4·ulNum) ∨ NULL；ret == VOS_OK ⇒ g_ulTblNum := ulNum
  Tbl_Set(ulIdx, ulVal)
    req R1  ulIdx ≤ N ⇒ W4 @ T + 4·ulIdx     需 4·ulIdx + 4 ≤ ext(T)      （T、N 是两个全局的入口值）    ★1
  Tbl_Update / Svc_SetValue                 原样上传
串联
  年内：ext(T) 和 N 没有关系可用 → 停在入口，记账"依赖全局不变式"
  AI + 程序分析：AI 从 Tbl_Init 与各处校验提出不变式 ext(g_pulTbl) = 4·g_ulTblNum；
    程序分析先验证所有写 g_pulTbl / g_ulTblNum 的地方都维持它（只有 Tbl_Init，成立），再代入：
    TP：ulIdx ≤ N ⇒ 需 4·ulIdx + 4 ≤ 4·N → ulIdx = N 时不成立 → L2
    TN：ulIdx < N ⇒ 成立
  为什么交给 AI 提：从代码里自动归纳"哪个全局量是哪块全局内存的长度"不可靠；验证它却很便宜。
```

**LV6-04 端口数来自配置文件解析（运行期值），遍历全局端口表**（S6.4  A1·M2·P0·V4·R1·X0·K2·G0 · AI + 程序分析）

单板端口数从配置文件读进 g_stBoardCfg.ulMaxPort，产品文档写的取值范围是 1~32，端口表按 16 定义。　期望：TP `Board_InitPorts_TP` pa=记账 · ai+pa=L2；TN `Board_InitPorts_TN` pa=-

```text
摘要
  Cfg_Parse(pucFile, ulLen)    eff g_stBoardCfg.ulMaxPort := 文件内容（无约束）
  Port_ResetAll()
    req R1  P ≥ 1 ⇒ W4 @ g_astPort + 24·[0, P−1]   需 24·P ≤ 384        （P = ulMaxPort 的入口值）     ★1
  Port_ResetAll_OK()
    先截断 ulNum = min(P, 16) → 需 24·ulNum ≤ 384 恒成立 → 不产生条件
  Board_ResetPort / Board_PrepPort   原样上传
串联
  年内：P 只来自文件内容 → 无约束 → 不报 L2（口径），记账"依赖运行期配置"
  AI + 程序分析：AI 从产品文档 / 配置模板读出"ulMaxPort ∈ [1, 32]"作为前置条件 → 需 768 ≤ 384 → L2
  TN：本地截断让条件在函数内就判定为安全，不需要任何外部事实
```

**LV6-05 全局指针在初始化时指向静态内存池的尾部 64 字节，三层之下按 64 写结尾**（S6.3 变体  A1·M2·P4·V1·R1·X1·K0·G0 · 2027H1）

共享区指针 g_pucShm 只在 Shm_Init 里赋值一次（池的最后 64 字节）。　期望：TP `Om_SaveTrace_TP` pa=记账 · later=L1；TN `Om_SaveTrace_TN` pa=记账 · later=-

```text
摘要
  Shm_Init()                  eff g_pucShm := &g_aucShmPool[192]
  Shm_WriteAt(ulPos)          req R1  W1 @ S + ulPos     需 ulPos + 1 ≤ ext(S)    （S = g_pucShm 的入口值）  ★1
  Shm_Put / Trace_Save        原样上传
串联
  年内：S 指向哪里不知道 → 记账"全局指针目标未知"
  2027：全局指针的流不敏感指向 {NULL, g_aucShmPool + 192} → ext(S) = 64 → TP：ulPos = 64 → 65 ≤ 64 → L1
```

### Lv7

**LV7-01 柔性数组：按 n 项申请，封口时写第 n 项**（S7.1  A1·M6·P2+P1·V1·R2·X1·K0·G0 · 2027H1（计划 P1））

变长列表 LIST_S 的最后一个成员是柔性数组；申请按 n 项，"封口"写在 astItem[n]。　期望：TP `List_Build_TP` pa=记账 · later=L1；TN `List_Build_TN` pa=记账 · later=-

```text
摘要
  List_New(ulNum)           eff ret = new(4 + 8·ulNum) ∨ NULL
  Item_Seal(pstList, ulIdx)
    astItem 在 +4，元素 8 字节，ulFlag 在元素内 +4
    req R1  W4 @ pstList + 4 + 8·ulIdx + 4     需 8·ulIdx + 12 ≤ ext(pstList)                      ★1
    （年内：柔性数组成员的大小显式标 Unknown——成员类型给不出大小，不能按 0 算）
  List_Seal / List_Finish   原样上传
串联
  2027：List_Build_TP：ext = 4 + 8·n，ulIdx = n → 需 8n + 12 ≤ 8n + 4 → 差 −8 恒不成立 → L1（与 LV2-04 同理）
  年内：标 Unknown → 不报，记账"柔性数组"
```

**LV7-02 二维数组行级越界：列下标多走一个，越进了下一行（整数组范围内）**（S7.2  A1·M7·P0·V1·R1·X1·K2·G0 · 计划 P1（CSA 盲区 #18，1–2 层也报））

调度表 g_aucSched[SLOT][RB]；某个时隙的 RB 循环写成了 <= RB_NUM。按整个数组算偏移 1·8 + 8 = 16 < 32 不越界；按行算第 8 列已经越界。　期望：TP `Sched_OnSlot_TP` pa=- · later=L2；TN `Sched_OnSlot_TN` pa=-

```text
摘要
  Sched_Mark(ulSlot, ulRb)
    年内（整数组口径）：req R1  W1 @ g_aucSched + 8·ulSlot + ulRb    需 8·ulSlot + ulRb + 1 ≤ 32      ★1
    P1（逐维口径）：再加 req R2  ulRb ≤ 7、req R3  ulSlot ≤ 3
  Sched_MarkRbs(ulSlot, ulRbNum)   调用点 ulRb ∈ [0, ulRbNum] 代入
  Sched_Alloc                      原样上传
串联
  Sched_OnSlot_TP：ulSlot = 1，ulRbNum = 8 → R1：8 + 8 + 1 = 17 ≤ 32 成立（所以年内不报）；
                   R2：ulRb ∈ [0, 8] → 8 > 7 → L2（P1 之后）
```

**LV7-03 字节缓冲按消息结构体解释，三层之下写数据区——缓冲区本身比结构体短**（S7.3  A1·M1·P1·V1·R2·X4·K0·G0 · 2027H1）

收到的短消息只有 8 字节，处理函数统一强转成 MSG_S*（头 4 字节 + 数据 12 字节）去写确认位。　期望：TP `Om_OnShortMsg_TP` pa=记账 · later=L1；TN `Om_OnShortMsg_TN` pa=记账 · later=-

```text
摘要
  Msg_SetData(pucRaw, ulIdx)
    (MSG_S *)pucRaw 之后访问 aucData[ulIdx]：字节偏移 4 + ulIdx；同时受成员（子对象）12 字节约束
    req R1  W1 @ pucRaw + 4 + ulIdx     需 ulIdx + 5 ≤ ext(pucRaw)      （对象边界）                   ★1
    req R2  需 ulIdx ≤ 11                                                 （成员边界）
  Msg_Ack / Om_HandleMsg            原样上传
串联
  2027：Om_OnShortMsg_TP：aucRaw（8），ulIdx = 6 → R1：11 ≤ 8 不成立 → L1；R2 成立
  年内："把字节缓冲当结构体用"不建模 → 记账；不会误报
```

**LV7-04 循环中指针步进拷贝：个数来自三层之上，比目的缓冲区大**（S7.4  A2·M1·P1·V1·R2·X1·K3·G0 · 2027H1）

老代码手写的字节拷贝 while (ulCnt-- > 0) *pucOut++ = *pucIn++;　期望：TP `Om_DumpReg_TP` pa=记账 · later=L2；TN `Om_DumpReg_TN` pa=记账 · later=-

```text
摘要
  Buf_CopyStep(pucOut, pucIn, ulCnt)
    年内：pucOut 的偏移在循环里被 widening 到 [0, +∞)，和 ulCnt 的关系丢了
          → 这种"widening 出来的无穷上界"不算代码给出的界 → 不产生 L2 条件，记账"指针步进"
    2027：记住"偏移 = 已迭代次数 ∈ [0, ulCnt − 1]"
          req R1  ulCnt ≥ 1 ⇒ W1 @ pucOut + [0, ulCnt−1]   需 ulCnt ≤ ext(pucOut)                     ★1
          req R2  同上，读 pucIn                               需 ulCnt ≤ ext(pucIn)
  Reg_Snapshot / Om_CollectReg     原样上传
串联
  2027：TP：aucOut 16、aucIn 32、ulCnt = 32 → R1 部分不满足 → L2；TN：ulCnt = 16 → 成立
  要点：这里不能偷懒报 L2——同样的 widening 结果在 TN 里也是 [0, +∞)，会误报。
```

**LV7-05 strcpy_s 的 destMax 用了名字的最大规格，目的缓冲区是老长度**（S7.5（destMax 那一半）  A4·M1·P1·V0·R1·X0·K0·G0 · 计划 P1（destMax 一半与 memcpy_s 相同；源串长度那一半 2027H2））

小区名规格从 16 扩到 32，拷贝函数统一用 CELL_NAME_LEN_MAX 当 destMax。　期望：TP `Cell_Rename_TP` pa=- · later=L1；TN `Cell_Rename_TN` pa=-

```text
摘要
  Name_Copy(pszDst, pszSrc)
    req R1  W32 @ pszDst + 0     需 32 ≤ ext(pszDst)          （destMax；出错路径会按 destMax 清零）        ★1
    req R2  需 strlen(pszSrc) + 1 ≤ 32                          （源串那一半，要字符串长度抽象，2027H2）
  Cell_SetName / Cell_UpdateName    原样上传
串联
  P1 之后：Cell_Rename_TP：szName（16）→ 32 ≤ 16 不成立 → L1
  年内（P0 只有 memcpy_s / memmove_s / memset_s 模型）：strcpy_s 没有模型 → 不报
```

**LV7-06 二维表按"行数 × 列数"申请，访问用变量步长线性化（两个变量相乘）**（S7.6  A1·M5·P2+P1·V1·R2·X5·K2·G0 · 不承诺）

行列数都来自小区配置；申请 ulRow × ulCol 字节，填表时列循环多了一个等号。　期望：TP `Cell_BuildMap_TP` pa=记账；TN `Cell_BuildMap_TN` pa=记账

```text
摘要
  Map_Set(pucMap, r, c, ulCol)
    偏移 r·ulCol + c：r·ulCol 是两个符号相乘，按"宁可表示得出"原则保留成不透明项 ⟨r·ulCol⟩
    req R1  W1 @ pucMap + ⟨r·ulCol⟩ + c      需 ⟨r·ulCol⟩ + c + 1 ≤ ext(pucMap)                       ★1
  Map_Fill(pucMap, ulRow, ulCol)   调用点 r ∈ [0, ulRow−1]、c ∈ [0, ulCol]（TP）
  Map_Init                          原样上传
串联
  Cell_BuildMap_TP：ext(pucMap) = ⟨ulRow·ulCol⟩，访问末端 ⟨(ulRow−1)·ulCol⟩ + ulCol + 1
    要证安全或证越界都得做非线性推理（两个乘积项之间的关系），线性 + 不透明项做不到 → 不报，记账"非线性"
  Cell_BuildMap_TN：同样判不了 → 记账（不误报）
  对照：如果 ulCol 是常量（LV3-09），乘积变线性，能判。
```

### Lv8

**LV8-01 TLV 解析循环：每轮的校验漏算了 2 字节 TLV 头（读越界）**（S8.1（相对条件写法）  A3·M1·P1·V5·R2·X1·K3·G1 · 尽力）

步长是从缓冲区里读出来的长度（数据驱动）。安全与否不需要跨轮不变式：只看"本轮的访问是否被本轮的校验覆盖"。　期望：TP `Om_OnCfgMsg_TP` pa=L2；TN `Om_OnCfgMsg_TN` pa=-

```text
摘要
  Tlv_ParseAll(pucBuf, ulTotal, pucOut)
    ulLen = pucBuf[ulOff + 1] 是缓冲区内容（无约束符号）；循环里 ulOff 被 widening，但我们不用它的区间——
    本轮访问的末端 E = ulOff + 2 + ulLen，本轮守卫给出 ulOff + ulLen ≤ ulTotal（线性关系）
    → 访问末端 E ≤ ulTotal + 2，最小 2
    req R1  R ulLen @ pucBuf + ulOff + 2   末端 ∈ [2, ulTotal + 2]   需 ulTotal + 2 ≤ ext(pucBuf)       ★1
  Tlv_ParseAll_OK：守卫 ulOff + 2 + ulLen ≤ ulTotal → 末端 ≤ ulTotal → 需 ulTotal ≤ ext(pucBuf)
  Msg_ParseBody / Om_HandleCfg     原样上传
串联
  Om_OnCfgMsg_TP：aucMsg（64），ulTotal = 64 → 需 66 ≤ 64 不成立，最小末端 2 成立 → L2
  Om_OnCfgMsg_TN：需 64 ≤ 64 成立
  要点：用的是"守卫蕴含条件"（§3.3 的条件带守卫），不是循环不变式，所以年内可以尽力做。
```

**LV8-02 两遍式 TLV：第一遍校验总长，第二遍按字节搬运时不再逐个校验**（S8.1（跨轮 / 跨循环关系）  A1·M1·P1·V5·R2·X1·K3·G4 · AI + 程序分析）

第二遍的安全性靠"第一遍算过 Σlen ≤ cap"。缺陷版本里第一遍只加了 len，第二遍却把 2 字节头也搬了进去。　期望：TP `Om_OnBatchMsg_TP` pa=记账 · ai+pa=L2；TN `Om_OnBatchMsg_TN` pa=记账 · ai+pa=-

```text
摘要
  Tlv_SumLen(pucBuf, ulTotal, ulCap)   eff ret == VOS_OK ⇒ ⟨第一遍的 Σ⟩ ≤ ulCap   （关于"另一个循环"的事实，摘要表达不了）
  Tlv_Gather(pucBuf, ulTotal, pucDst)
    ulDOff 在循环里被 widening 到 [0, +∞)，且与 ulTotal、ulCap 没有可用的线性关系
    → 无穷上界来自 widening，不是代码给出的界 → 不报 L2，记账"跨循环关系"（不误报，也查不出 TP）
串联
  年内：TP、TN 都只记账
  AI + 程序分析：AI 识别"两遍式"模式，提出不变式"第二遍写入总量 = 第一遍校验过的量"；
    程序分析检查两遍是否对同一序列、同一长度表达式求和：
      TN：两遍都是 Σ len → 不变式成立 → 安全
      TP：第一遍 Σ len、第二遍 Σ (2 + len) → 不一致 → L2
```

**LV8-03 手写的字符串拷贝循环（while (*src)），链顶传了一个比目的缓冲区长的字面量**（S8.2+S7.5  A2·M1·P1·V1·R2·X1·K4·G0 · 2027H2（字符串长度抽象 + 指针步进））

老代码没用 strcpy_s，自己写了拷贝循环；某个调用方把一个 18 字符的默认名传进来，缓冲区 8 字节。　期望：TP `Cell_SetDefault_TP` pa=记账 · later=L2；TN `Cell_SetDefault_TN` pa=记账 · later=-

```text
摘要
  Str_CopyRaw(pszDst, pszSrc)
    循环次数由源串内容决定（非典型循环），两个指针同步步进
    年内：偏移 widening 到 [0, +∞)，不是代码给出的界 → 不报，记账"非典型循环 / 字符串长度"
    2027H2：req R1  W1 @ pszDst + [0, strlen(pszSrc)]   需 strlen(pszSrc) + 1 ≤ ext(pszDst)       ★1
  Name_Set / Cell_ApplyName     原样上传
串联
  2027H2：TP：strlen("CELL_DEFAULT_00001") = 17 → 需 18 ≤ 8 → L2；TN："C1" → 3 ≤ 8 成立
```

**LV8-04 三维资源格（符号 × RB × 端口）按配置值线性化，嵌套循环多走了一个端口**（S8.3  A1·M5·P2+P1·V1·R2·X5·K4·G0 · 不承诺）

物理层常见写法：缓冲区按 ulSymNum × ulRbNum × ulPortNum 申请，下标 (s·RB + r)·PORT + p。　期望：TP `Phy_BuildGrid_TP` pa=记账；TN `Phy_BuildGrid_TN` pa=记账

```text
摘要
  Grid_Set(pucGrid, s, r, p, ulRbNum, ulPortNum)
    偏移 (s·ulRbNum + r)·ulPortNum + p：多个变量相乘，保留成不透明项
    req R1  W1 @ pucGrid + ⟨(s·ulRbNum + r)·ulPortNum⟩ + p     需 … ≤ ext(pucGrid)                    ★1
串联
  ext(pucGrid) = ⟨ulSymNum·ulRbNum·ulPortNum⟩；要比较两个乘积式需要非线性推理 → TP、TN 都只记账
  这是程序分析覆盖上限的第 ② 条（取值要能用线性表达式和区间表达）落空的典型
```

**LV8-05 环形缓冲区：尾指针回绕条件写成 > RING_LEN（应为 >=），访问在三层之下**（S8.1 变体（字段不变式）  A1·M2·P4·V4·R1·X0·K3·G4 · AI + 程序分析（也是 2027 字段级不变式的候选））

trace 环形缓冲在全局；每次写入后尾指针加一，回绕条件差一，tail 会停在 RING_LEN 上一次。　期望：TP `Om_Trace_TP` pa=记账 · ai+pa=L2；TN `Om_Trace_TN` pa=记账 · ai+pa=-

```text
摘要
  Ring_Put(pstRing, ucVal)
    req R1  W1 @ pstRing + 4 + pstRing->ulTail      需 pstRing->ulTail ≤ 63                        ★1
    eff     pstRing->ulTail := (pstRing->ulTail + 1 > 64) ? 0 : pstRing->ulTail + 1
  Trace_Put / Trace_Record          原样上传
串联
  年内：链顶传的是 &g_stTraceRing，ulTail 的值来自"上一次调用留下的状态" → 无约束 → 记账
  AI + 程序分析：AI 从字段名与访问方式提出字段不变式 0 ≤ ulTail ≤ 63；
    程序分析检查所有写 ulTail 的地方是否维持它：Ring_Put 的更新允许 ulTail = 64 → 不维持 → 在写入点报 L2
    TN（>=）：更新后 ∈ [0, 63] → 维持 → R1 在不变式下成立
```

### Lv9

**LV9-01 同一函数里按制式选处理函数（函数指针取常量），NR 处理按"长度"位置写结尾**（S9.1  A1·M1·P5·V1·R2·X0·K1·G0 · 尽力（计划 P1））

能力处理按制式分发，两个处理函数对"长度"的约定不同：NR 版把 ulLen 当成最后一个字节之后的位置。　期望：TP `Ue_OnCap_TP` pa=L2；TN `Ue_OnCap_TN` pa=-

```text
摘要
  Nr_SaveCap(pucBuf, ulLen)    req R1  W1 @ pucBuf + ulLen       需 ulLen + 1 ≤ ext(pucBuf)              ★1
  Lte_SaveCap(pucBuf, ulLen)   req R2  W1 @ pucBuf + ulLen − 1   需 ulLen ≤ ext(pucBuf)
  Cap_Dispatch(pucBuf, ulLen, ulRat)
    pfn 在本函数里只可能是 {Nr_SaveCap, Lte_SaveCap}（局部常量赋值）→ 两个摘要都代入，各带守卫：
    req R1  ulRat == NR ⇒ 需 ulLen + 1 ≤ ext(pucBuf)
    req R2  ulRat ≠ NR ⇒ 需 ulLen ≤ ext(pucBuf)
  Cap_Handle / Ue_ProcCap      原样上传
串联
  Ue_OnCap_TP：aucCap 16、ulLen 16、ulRat 入口无约束 → R1 在 NR 分支上不成立 → L2（依赖制式）
  Ue_OnCap_TN：ulLen 15 → 都成立
  没有 S9.1 时：Cap_Dispatch 的调用是断链 → 记账"函数指针"
```

**LV9-02 静态消息分发表（全局 const 结构体数组里的函数指针），其中一个处理函数写结尾越界**（S9.2  A1·M1·P5·V1·R2·X0·K1·G0 · 2027H1）

消息分发表在编译期就确定；B 消息的处理函数按 ulLen 位置写 '\0'。　期望：TP `Msg_OnRecv_TP` pa=记账 · later=L2；TN `Msg_OnRecv_TN` pa=记账 · later=-

```text
摘要
  A_Proc(pucBody, ulLen)    req     需 ulLen ≤ ext(pucBody)
  B_Proc(pucBody, ulLen)    req R1  W1 @ pucBody + ulLen     需 ulLen + 1 ≤ ext(pucBody)             ★1
  Msg_Dispatch(ulMsgId, pucBody, ulLen)
    g_astHandler[i].pfnProc：按"MSG_HANDLER_S.pfnProc 这个字段被赋过哪些函数"解析 → {A_Proc, B_Proc}
    （字段级解析；目标多时可让 AI 结合 ulMsgId 做精度过滤）
    req R1  需 ulLen + 1 ≤ ext(pucBody)      （取最坏的目标；带"可能"标记）
  Msg_Route / Msg_Handle    原样上传
串联
  2027：TP：aucBody 32、ulLen 32 → 33 ≤ 32 不成立，但只在 B 目标上 → L2；TN：ulLen 31 → 成立
  年内：函数指针调用断链 → 记账
```

**LV9-03 运行期注册的事件回调：注册在模块初始化里，触发在另一条链上**（S9.3  A1·M1·P5·V1·R2·X0·K1·G0 · AI + 程序分析（2027 的指向分析也能做一部分））

事件回调表按事件号存函数指针；小区模块注册的回调按 ulLen 位置写结尾。　期望：TP `Evt_FireCellUp_TP` pa=记账 · ai+pa=L1；TN `Evt_FireCellUp_TN` pa=记账 · ai+pa=-

```text
摘要
  Cell_OnUpEvt(pucData, ulLen)     req R1  W1 @ pucData + ulLen    需 ulLen + 1 ≤ ext(pucData)         ★1
  Evt_Register(ulEvt, pfn)         eff g_apfnEvt[ulEvt] := pfn       （写进全局函数指针表）
  Evt_Fire(ulEvt, pucData, ulLen)  调用 g_apfnEvt[ulEvt](…) → 目标未知 → 断链
串联
  年内：断链，记账"函数指针（全局表）"
  AI + 程序分析：AI（或 2027 的指向分析）从所有 Evt_Register 调用点恢复 g_apfnEvt[EVT_CELL_UP] ∈ {Cell_OnUpEvt}
    → 作为外部调用边注入 → 程序分析代换 → TP：ulLen 16、缓冲 16 → 17 ≤ 16 → L1；TN：ulLen 15 → 成立
```

**LV9-04 BSP 驱动接口只有声明：按 ulLen 往 pucBuf 读寄存器，调用方缓冲区 16、长度 32**（S9.4  A3·M1·P5·V1·R2·X0·K0·G0 · 模型表（程序分析） + AI 生成模型草稿）

驱动库是二进制交付的，头文件注释写了"pucBuf 至少 ulLen 字节"。　期望：TP `Om_ReadChip_TP` pa=记账 · ai+pa=L1；TN `Om_ReadChip_TN` pa=记账 · ai+pa=-

```text
摘要
  Drv_ReadReg                  无函数体、无模型 → 断链，记账"未知函数"
    模型（summary_models.yaml，来源 = llm，人工确认后生效）：
      - name: Drv_ReadReg
        conditions: [ { base: param1, bytes: "param2", kind: W } ]
    有模型后：req R1  W ulLen @ pucBuf + 0     需 ulLen ≤ ext(pucBuf)                                ★1
  Chip_ReadStatus / Om_CollectChip   原样上传
串联
  有模型：TP：16 < 32 → L1；TN：ulLen 16 → 成立
```

**LV9-05 递归遍历配置树，按深度写路径数组，深度保护差一**（S9.5  A1·M1·P1·V1·R2·X1·K5·G1 · 2027H1）

路径数组 MAX_DEPTH 项；递归入口判 ulDepth > MAX_DEPTH 才返回（应为 >=）。　期望：TP `Cfg_DumpTree_TP` pa=记账 · later=L2；TN `Cfg_DumpTree_TN` pa=记账 · later=-

```text
摘要
  Tree_Walk(pstNode, pulPath, ulDepth)   —— 自己调用自己（强连通分量）
    2027：分量内迭代到不动点，守卫给出 ulDepth ∈ [0, 8]（初值 0，每层 + 1，守卫截在 8）
          req R1  ulDepth ≤ 8 ⇒ W4 @ pulPath + 4·ulDepth     需 4·ulDepth + 4 ≤ ext(pulPath)         ★1
    年内：递归按断链处理 → 记账
  Cfg_Walk / Cfg_Dump      原样上传
串联
  2027：TP：aulPath 32 字节，ulDepth ∈ [0, 8] → 36 ≤ 32 部分不满足 → L2；TN：[0, 7] → 成立
```

**LV9-06 跨任务消息：发送方把"端口个数"填进了"端口号"字段，接收任务三层之后按端口号索引**（S9.6（v3.11 新增）  A1·M2·P5·V1·R1·X0·K0·G0 · AI + 程序分析）

OM 任务给端口任务发配置消息（VOS_SendMsg），接收方的任务入口由平台按 PID 调起。调用图里两边没有边：一边是"发消息"，另一边是"任务入口"。　期望：TP `Om_OnPortCfg_TP` pa=记账 · ai+pa=L1；TN `Om_OnPortCfg_TN` pa=记账 · ai+pa=-

```text
摘要
  Port_SetMode(ulPortId)        req R1  W4 @ g_astPort + 24·ulPortId     需 ulPortId ≤ 15              ★1
  Port_Apply / Port_HandleCfg   原样上传（实参 pstMsg->ulPortId）
  Port_TaskEntry(pstMsg)        req R1  需 pstMsg->ulPortId ≤ 15     → 任务入口没有调用者：停在入口，记账
  Om_OnPortCfg_TP / _TN         写 stMsg.ulPortId，然后 VOS_SendMsg(PID_PORT, &stMsg, …)：未知函数，记账
串联
  年内：两边各记一条账，连不起来
  AI + 程序分析：AI 恢复"发往 PID_PORT 的 PORT_CFG_MSG_S 由 Port_TaskEntry 处理"（看任务注册与消息结构体），
    注入一条"消息边"：Port_TaskEntry 的条件里 pstMsg->ulPortId 换成发送方写入的值
    → TP：16 ≤ 15 不成立 → L1；TN：15 → 成立
  这一类在真实代码里很常见：任务之间、板间、进程间的消息都会在调用图上断开。
```

### Lv10

**LV10-01 报文结构体有两组"缓冲区 + 长度"，访问 ext 时用 dataLen 做了校验**（S10.1  A1·M8·P3·V3·R2·X0·K1·G4 · AI + 程序分析）

驱动交上来的报文描述符；缓冲区都在分析范围之外分配。哪个长度字段属于哪个缓冲区，只有业务知道。　期望：TP `Drv_OnRxPkt_TP` pa=记账 · ai+pa=L2；TN `Drv_OnRxPkt_TN` pa=记账 · ai+pa=-

```text
摘要
  Pkt_SetExt(pstPkt, ulOff, ucVal)
    req R1  ulOff < pstPkt->ulDataLen ⇒ W1 @ pstPkt->pucExt + ulOff   需 ulOff + 1 ≤ ext(pstPkt->pucExt)   ★1
  Pkt_SetExt_OK：守卫换成 ulOff < pstPkt->ulExtLen
  Pkt_MarkExt / Pkt_Proc   原样上传；入口 Drv_OnRxPkt_* 的 pstPkt 来自驱动 → 条件停在入口，记账
串联
  年内：TP、TN 都是"入口未证明"，记账
  AI + 程序分析：AI 推断字段配对 pucExt ↔ ulExtLen、pucData ↔ ulDataLen（命名 + 其它模块里的申请点），
    作为契约 ext(pstPkt->pucExt) = pstPkt->ulExtLen 注入；程序分析在所有访问点检查：
      TP：守卫 ulOff < ulDataLen 推不出 ulOff < ulExtLen → L2
      TN：守卫 ulOff < ulExtLen ⇒ ulOff + 1 ≤ ulExtLen → 成立
  对照：如果 pucExt 在汇合点指向一个已知大小的数组，这就是 Lv3 的"校验界与实际大小不一致"，不需要 AI。
```

**LV10-02 平台 API 的"缓冲区 + 长度"约定只写在注释里，调用方传的长度大于缓冲区**（S10.2  A3·M1·P5·V0·R1·X0·K0·G4 · AI + 程序分析）

平台接口 Plat_GetDevInfo 二进制交付，注释写"ulBufLen 为 pucBuf 的字节数，不小于 DEV_INFO_LEN"。　期望：TP `Om_QueryDev_TP` pa=记账 · ai+pa=L1；TN `Om_QueryDev_TN` pa=记账 · ai+pa=-

```text
摘要
  Plat_GetDevInfo     无函数体 → 断链；AI 从注释抽出参数配对 pucBuf ↔ ulBufLen，生成模型：
                      W ulBufLen @ param0（写满 ulBufLen 字节）；前置条件 ulBufLen ≥ 64
  Dev_Read(pucBuf, ulBufLen)   有模型后 req R1  需 ulBufLen ≤ ext(pucBuf)                          ★1
  Dev_Query / Om_GetDev        原样上传
串联
  AI + 程序分析：TP：aucInfo 32、ulBufLen 64 → 64 ≤ 32 不成立 → L1；TN：aucInfo 64 → 成立
  与 LV9-04 的区别：这里不仅要模型，还要从注释里读出"哪个参数是哪个缓冲区的长度"。
```

**LV10-03 对外接口注释承诺"端口号 0~31"，实现里的表只有 16 项且不校验**（S10.3  A1·M2·P0·V1·R1·X0·K0·G4 · AI + 程序分析）

接口是对外导出的，分析范围里没有调用者。注释（接口文档）就是唯一的调用约定。　期望：TP `Port_SetSpeed_TP` pa=记账 · ai+pa=L2；TN `Port_SetSpeed_TN` pa=记账 · ai+pa=-

```text
摘要
  Port_WriteSpeed(ulPortId, ulSpeed)   req R1  W4 @ g_astPort + 24·ulPortId + 4    需 ulPortId ≤ 15      ★1
  Port_Apply / Port_Cfg                原样上传
  Port_SetSpeed_TP / _TN（对外接口）    条件停在入口（没有调用者）→ 记账"入口未证明"
串联
  AI + 程序分析：AI 从注释抽出前置条件：TP 的注释是 ulPortId ∈ [0, 31]，TN 的是 [0, 15]
    → 在这个前提下判 R1：TP：16..31 不满足 → L2（实现没兑现文档）；TN：成立
```

**LV10-04 外部消息里的长度一路下传到 memcpy_s，没有任何校验（TN：框架在注册时声明了最大长度）**（S10.4  A3·M2·P0·V5·R1·X0·K0·G0 · AI + 程序分析（候选研判））

OM 消息入口把 usMsgLen 原样交给配置保存；TN 版本的消息在注册时声明了最大长度 256，由平台框架在投递前统一校验——这件事在分析范围之外。　期望：TP `Om_OnSaveCfg_TP` pa=记账 · ai+pa=L2；TN `Om_OnSaveCfg_TN` pa=记账 · ai+pa=-

```text
摘要
  Cfg_Save(pucBody, ulLen)    req R1  W ulLen @ g_aucCfg + 0   需 ulLen ≤ 256                         ★1
                              req R2  R ulLen @ pucBody + 0    需 ulLen ≤ ext(pucBody)
  Cfg_Store / Om_SaveCfg      原样上传
  入口：ulLen = pstMsg->usMsgLen，只有类型范围 [0, 65535]，不算代码给出的界 → 停在 S2，不报 L2
串联
  年内：两条入口都记一条"外部输入未证明安全"的候选（带逐层 trace）
  AI 研判：TP 找不到任何校验 → 升级 L2；TN 读到 OM_RegMsg(…, 256) 的注册与框架的投递校验 → 抑制
```

**LV10-05 "个数"字段与数组容量的对应关系：解码时直接从消息里取个数，没跟数组容量比**（S10.1 变体（个数 ↔ 数组）  A1·M3·P3·V3·R2·X1·K2·G4 · AI + 程序分析）

NBR_LIST_S { ulNum; astNbr[16]; }——"ulNum 是 astNbr 的有效个数"只是约定。解码函数写 ulNum 时没截断；使用处三层之下按 ulNum 遍历。　期望：TP `Cell_OnNbrMsg_TP` pa=记账 · ai+pa=L2；TN `Cell_OnNbrMsg_TN` pa=-

```text
摘要
  NbrList_Decode(pstList, pucMsg)   eff pstList->ulNum := pucMsg[0]（消息内容，无约束）
  NbrList_Decode_OK                 eff pstList->ulNum := min(pucMsg[0], 16)   → ∈ [0, 16]
  Nbr_Use(pstList, ulNum)           调用点 i ∈ [0, ulNum − 1]
    req R1  ulNum ≥ 1 ⇒ R2 @ pstList + 4 + 4·[0, ulNum−1]    需 4·ulNum ≤ 64（astNbr 64 字节）        ★1
  Nbr_Apply / Cell_ApplyNbr         实参 pstList->ulNum
串联
  Cell_OnNbrMsg_TN：解码效应给出 ulNum ∈ [0, 16] → 64 ≤ 64 成立 → 不报（程序分析自己就能证明）
  Cell_OnNbrMsg_TP：ulNum 来自消息内容 → 无约束 → 记账
  AI + 程序分析：AI 提出字段不变式 ulNum ≤ ARRAY_SIZE(astNbr)；程序分析检查所有写 ulNum 的地方：
    NbrList_Decode 的写入不维持它 → 在写入点报 L2（比在使用点报更有用：修复点就在这里）
```
<!-- /CASES:appendix -->
