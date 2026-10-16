# CSA Summary 2.0 规格说明（P0 · 2026-12-15 交付）

> 调研包 v3.12 · 2026-10-13 · 状态：**评审稿**（§12 的决策点头后定稿，代码按定稿写）
> **v3.13**：§5.3 的摘要格式定稿为"表达式写成字符串"的可读 JSON（附 JSON Schema 与 16 个用例的 104 份实样、回放器）；F-03 / F-04 按 securec 真实语义细化；新增 F-12b（守卫界判定）、A-44 的覆盖合并、账本原因 `untracked_pointer`、选项 `CallTrace`。方案备选按维度重排，见 `08-options-and-summary-examples.md`。
> **v3.14**：§5.3 新增 D-30——摘要分**机器字段**与键名为 `doc` 的说明两类，引擎不读 `doc`，去掉 `doc` 就是完整摘要；对象写法 `obj`、访问标识 `access{fn,key,at,end}`、
> 结构化的调用点结局入规格；F-32 消息、F-33 总链 note、F-34 指纹改为全部由机器字段生成（指纹改用访问末端式、实测与 `llvm::xxHash64` 一致）；新增选项 `Emit`（I-25）、决策 D14 / D15。
>
> 这份文件回答"代码写成什么样"：功能边界、对外接口、数据格式、算法、跨 TU 执行、非功能要求、测试与验收。
> "什么时候写、谁做、按什么顺序"在 **`07-dev-plan.md`**。设计理由在 `01-architecture.md`（本文件细化它，改动处列在 §13），
> 场景与承诺边界在 `04-scenario-tiers.md`，摘要的形式与串联在 `05-summary-design-and-cases.md`，验收语料是 `cases/` 的 67 个用例。
>
> 证据标注：【实测数据】可复现的实验（本版新增的来自 `probes/i0-integration/` 沙箱实测，上游 LLVM 14.0.6）；
> 【推断】设计判断；【立项输入】使用方给定。

---

## 0. 读法与约定

* **需求编号**：`F-` 功能、`I-` 接口、`D-` 数据、`A-` 算法、`X-` 跨 TU、`N-` 非功能、`T-` 测试。§11 的追溯矩阵把它们挂到 P0 条目、模块、用例与检查点。
* **用词**：**必须**＝不满足不能交付；**应**＝默认要做，偏离要在提交信息里说明；**可**＝可选。
* **优先级**：本文件只规定 P0（12-15 交付）。P1 / P2 的内容只在需要预留接口时提到，标 `[P1]` / `[2027]`。
* **与其它文件冲突时**：实现以本文件为准；排期以 `07-` 为准；场景归属以 `04-` 为准。

### 0.1 本版新增的设计决策（详见 §12，需要你点头）

| # | 决策 | 一句话理由 |
|---|---|---|
| **D5′** | 跨 TU 改为**方案 B**：一遍导出 → 离线求解 → 一遍出告警（替代 v3.7 建议的多轮扫描） | 有了 D9，B 的额外工作只剩序列化；B 的遍数固定为 2，与调用链跨多少文件无关；多轮的代价要看探针统计，B 没有这个不确定性 |
| **D9** | 引擎跑在自己的轻量中间表示 **FIR** 上，不直接跑 `clang::CFG` | 引擎与 Clang 解耦：可单测、可离线求解、可直接喂给 AI；Clang 相关的复杂度集中在一处（降级） |
| **D10** | 落盘用 **JSON**（`llvm::json`），查询库（SQLite）由 Python 驱动生成；**C++ 侧不引入任何第三方库** | 不动 xir 的依赖；跨语言可读（`01-` R5）；多进程写不同文件、无锁 |
| **D11** | checker 只挂 **`check::ASTDecl<TranslationUnitDecl>`**，不挂任何路径敏感回调 | 沙箱实测：多挂一个空的 `EndOfTranslationUnit`，每个函数都会跑一遍 ExprEngine【实测数据】 |
| **D12** | 告警报在**汇合点的调用表达式**上；跨文件的 trace 写进 note 文本 | CSA 的 note 只能指向本 TU 可见的文件，被调者在别的 `.c` 里时无法定位 |
| **D13** | 函数 / 全局用**自定义键**（外部链接用名字，内部链接带定义文件），不用 USR | C 没有重载，名字即可；不依赖 `clangIndex`；头文件 static inline 可跨 TU 去重 |
| **D7** | 新 checker：`rb.enhance.ipa.OobSummary`（类 `OobSummaryChecker`） | 跨 TU 规则按 `csa-checker-dev` 约定须与你确认包；它不用 CSA 的 IPA，`ipa` 取值不影响它 |
| **D14** | 摘要里的说明文字一律放在键名为 **`doc`** 的字段里，引擎不读；去掉 `doc` 就是完整摘要（§5.3 D-30） | 自然语言静态分析消费不了，只能给人看；分开放才能保证判定与告警不依赖它，也能一键去掉 |
| **D15** | L2 消息的 `{source}` P0 只写界的种类（`a loop condition`），出处函数靠调用链 note；`bound_at` 留到 P1 | 出处函数要随代换多维护一个字段；先保证消息全部由机器字段生成 |

---

## 1. 范围

### 1.1 一句话

一个只做语法遍历的 CSA checker 把每个函数降成 FIR；一个与 Clang 无关的抽象解释引擎自底向上给每个函数算出越界摘要
（前置条件 Req、效果 Eff、已判定 Rep、账本 Open），在调用点代换、逐层上传，在大小和访问都已知的那一层（汇合点）判定；
判定出的越界按现有告警格式报出，只报现有规则看不到的（> `inlineDepth` 层，CSA 盲区例外）。

### 1.2 P0 必须支持的场景（验收口径）

| 类别 | 场景（`04-`） | 用例（`cases/`） | 要求 |
|---|---|---|---|
| **年内承诺** | 26 个：Lv1–Lv4 全部 + S5.1 | 32 个用例：LV1-01…08、LV2-01…09、LV3-01…05、LV3-08…10、LV4-01…05、LV5-01、LV5-06，以及 LV3-07 的 TN | TP 按期望级别报出（L1 / L2），TN 不报 |
| **年内尽力** | 7 个：S3.3、S3.8、S4.6、S5.3、S5.5、S6.1、S9.1 | 9 个 TP：LV3-06、LV3-07(TP)、LV4-06、LV5-03、LV5-04、LV5-05、LV6-01、LV8-01、LV9-01 | 统计报出率，不设门槛；**不得因此误报** |
| **其余** | 2027 / AI + 程序分析 / 不承诺 | 其余用例的全部入口 | **期望为 `-` 或 `记账` 的入口一律不得报**；`记账` 的应在账本里有对应原因 |
| **链长** | 合成直线链 | 3 / 5 / 10 / 17 / 20 层变体（由 `cases/gen_chain.py` 生成，见 `07-`） | 全部报出；每条 < 1 秒；≤ 2 层的不报 |

### 1.3 不做（P0）

C++；函数指针的指向分析（只做同函数内常量赋值，S9.1 尽力）；递归的 SCC 不动点（环上保守回退）；
二维数组行级越界 #18 `[P1]`；`strcpy_s` 族的源串长度 `[2027]`（`destMax` 一半可在 P1 顺带）；
字段级不变式、跨任务消息、AI 注入 `[2027]`；增量分析、CI 门禁 `[2027]`；路径解释（CSA 侧 `checkPreCall`）`[P1]`；
S2 级（符号可疑）输出。

### 1.4 术语

| 术语 | 含义 |
|---|---|
| **FIR** | Function IR：本引擎的函数级中间表示（§5.2），由 checker 从 Clang AST + CFG 降级得到，JSON 可序列化 |
| **接口符号** | 摘要里允许出现的符号：形参值 `p0`、`p1`…、形参指向对象的剩余字节数 `ext(p0)`、经形参可达的字段（深度 ≤ 2）`p0->ulLen`、全局 `g:g_x`（§5.3） |
| **Req / Eff / Rep / Open** | 前置条件 / 效果 / 已判定的越界 / 账本（`05-` §1.1） |
| **汇合点** | 访问侧条件与大小侧信息都已知、能判定的那个函数（调用点） |
| **depth / effDepth** | 访问点到汇合点经过的调用层数 / 大小、偏移或长度经效果（返回值、出参、字段）向上传了几层；报告过滤用两者的最大值（§3.3） |
| **L1 / L2** | 确定越界 / 可能越界（上界来自代码）（§3.2） |
| **记账** | 判不了，不报，在账本里记一条带原因的记录（`01-` §3.12 的原因枚举） |
| **TU id** | 主文件规范化绝对路径的 xxHash64（16 位十六进制），驱动与 checker 用同一规则算（§5.1） |

---

## 2. 运行形态总览

```
                    ① Phase=export（bxen 全量模式，按 TU 并行；只做语法遍历，不跑 ExprEngine）
compile_commands ─▶ rb.enhance.ipa.OobSummary ──▶ WorkDir/fir/<tu>.json   （每个 TU 一份 FIR + 全局声明）
                                                         │
                    ② solve（一次；bxen 跑一个桩 TU 的 Phase=solve，或独立命令 oobsum-solve）
                                                         ▼
                          读全部 FIR → 全局调用图 → SCC 逆拓扑序逐函数求摘要 → 调用点代换与判定
                          ──▶ WorkDir/summaries/*.json、decisions/<tu>.json、ledger.jsonl、stats.json
                                                         │
                    ③ Phase=report（只对有判定的 TU，bxen 全量模式 + 过滤后的 cdb）
                                                         ▼
                          按 decisions/<tu>.json 发 BugReport ──▶ 与现有规则同格式的告警
                    ④ oobsum.py sqlite（可选）：把 summaries / ledger / reports 灌进 SQLite，给查询与 AI 用

  Phase=inline（默认值）：单个 TU 内一次做完 ①②③（跨 TU 的被调者视为未知）。
  所以 csa-checker-dev 约定的"单文件 bxen 命令"不加任何参数就能跑同文件内的多层链。
```

**为什么从多轮扫描（方案 A）改成方案 B**【推断】：

| | A：多轮 TU 扫描 + 工作表（v3.7 建议） | **B：一遍导出 → 离线求解 → 一遍出告警（本版）** |
|---|---|---|
| 解析遍数 | 轮数 ≈ 调用链上**跨文件切换**的次数；同一 TU 可能被重复解析多次 | **固定 2 遍**（export 全量；report 只跑有告警的 TU） |
| 代价的不确定性 | 取决于真实代码的调用图形态，要先量（探针 `probe_stats.py` 的"TU 分析总次数 / TU 数"） | 无 |
| 新增基础设施 | 每轮的导入文件、合并、工作表、跨轮去重 | FIR 序列化（`llvm::json`）、求解入口 |
| 跨 TU 递归 / 调度 | 按轮次近似 | 全局 SCC，和同 TU 一样处理 |
| 2027 增量分析 | 每次都从解析开始 | 只重导出改过的 TU、只重算受影响的 SCC |
| 给 AI / 查询（`05-` 方案 E、G） | 另行导出 | FIR + 摘要本身就是 |

前提是 D9（引擎跑在 FIR 上）：两种方案共用同一个引擎，只是编排不同。A 保留为回退方案（§7 的 X-08），不在 P0 实现。

---

## 3. 功能需求

### 3.1 检测对象

| 编号 | 访问形态 | 生成的安全条件（字节） | 备注 |
|---|---|---|---|
| **F-01** | 数组下标读 / 写 `a[i]`、`p[i]`、`s.arr[i]`、`p->arr[i]`、`g.arr[i]` | `off + size ≤ ext(base)`，且 `off ≥ 0` | 结构体成员数组以**成员**为对象（子对象边界，`01-` §3.1 #2–#7、#11） |
| **F-02** | 指针解引用 `*p`、`*(p + k)`、`p->f`（`p` 指向的对象可能比类型小） | `off(f) + sizeof(f) ≤ ext(p)` | 覆盖"按大结构体访问小对象"（LV1-05、LV4-02） 〔由 SPEC-0019 修订：入口上按声明类型的常数偏移访问记账 `typed_field`〕 |
| **F-03** | `memcpy_s` / `memmove_s(dest, destMax, src, count)` | 目的：`destMax ≤ ext(dest)`；源：`count ≤ destMax ⇒ count ≤ ext(src)` | 按 securec（libboundscheck）的实现【文档】：`destMax == 0` 或 `> SECUREC_MEM_MAX_LEN`（默认 `0x7fffffff`）直接返回、不写；`count > destMax` 时先把 `dest` 的 `destMax` 字节清零再返回、不读源。所以目的一侧总是写 `destMax` 字节，源一侧只在 `count ≤ destMax` 时读。`destMax` 是**可能回绕的无符号减法**（`128 − ulOff`）时，回绕后的值必然大于上限 → 生成守卫"减法不为负"（LV2-03 TN） 〔由 SPEC-0020 修订：两段，`E >= 0` 与 `E <= 0x7fffffff − 2^W`（destMax = E + 2^W）〕 |
| **F-04** | `memset_s(dest, destMax, c, count)` | `destMax ≤ ext(dest)` | 同 F-03 的 `destMax` 规则 |
| **F-05** | libc `memcpy` / `memmove` / `memset` | `n ≤ ext(dest)`；`n ≤ ext(src)` | 代价很小，顺带支持 |
| **F-06** | 模型表里配置的其它访问函数（内部封装） | 按配置（§4.4） | 清单由你提供（`07-` 输入 I8） |
| **F-07** | `strcpy_s` / `strncpy_s` / `strcat_s` / `strncat_s` / `sprintf_s` / `vsprintf_s` / `snprintf_s` / `vsnprintf_s(dest, destMax, …)` | 目的：写 `1 … destMax` 字节（`upto`），`destMax ≤ ext(dest)` 不成立是 L2（`model`），偏移已越界是 L1 | 〔SPEC-0034 新增〕securec 最多写实际长度 + 1、出错只写 `dest[0]`；`destMax` 规则同 F-03；源串长度不建模（`[2027]`） |
| **F-07** | 循环里的访问 | 下标取循环后的区间（阈值 widening + narrowing） | **任何循环都必须产出结果**，不得 unsupported |
| **F-08** | 指向数组中间的指针再下标 `p = &a[2]; p[j]`、`p = a + k; p[j]` | 基址 `a`、偏移 `2·elem + j·elem` | CSA 盲区 #21：打 `csaBlindSpot`，不做层数过滤（`01-` §0.1） |

`ext(x)` 的含义与 `05-` §2.1 相同：从指针 `x` 往后还能访问的字节数（对象大小 − 偏移）。负偏移（`off < 0`）同样是越界，按同一套级别判定。

### 3.2 判定与级别

在汇合点，对代换后的条件 `need ≤ have`（`need = off + size`，`have = ext(base)`）：

| 编号 | 结论 | 判据 | 去向 |
|---|---|---|---|
| **F-10** | **Safe** | `need` 的上界 ≤ `have` 的下界（区间或经守卫蕴含，§6.5） | 丢弃 |
| **F-11** | **L1** | `need` 的**下界** > `have` 的上界（对所有取值都越界） | 报（Definite） |
| **F-12** | **L2** | 下界不越界、上界越界，**且上界来自代码**：校验、掩码、`%`、分支常量、循环界、被调者返回值范围、守卫 | 报（Possible） |
| **F-12b** | **守卫界判定** | 还含当前函数的接口符号，但 `have` 已知、且这些符号都被**代码给出的界**框住（访问侧校验带上来的守卫、调用点处成立的守卫——如条件效应收窄出来的）：按界的最值套用 F-10 / F-11 / F-12 | 当场判定，不再上传（LV3-03、LV4-06）。理由：访问侧的校验就是代码自己声明的"接受范围"，它与对象大小不匹配本身就是缺陷 |
| **F-13** | **上传** | 还含当前函数的接口符号（F-12b 不适用时） | 代换成当前函数的 Req，`depth + 1` |
| **F-14** | **记账** | 到入口仍含符号；或上界只来自类型范围（`UINT16` 的 65535）、widening 的 ∞、不透明非线性项、读自内存内容的无约束值 | 不报；账本记原因 |

上界的**来源标签**随值一起传播（§6.2），F-12 只认标签集合 ⊆ {`check`, `mask`, `branch`, `loop`, `ret`, `guard`, `const`} 的上界。
**F-15**：`S2` 级（符号可疑）P0 不输出。**F-16**：`ReportL2=false` 时只报 L1（G3 的"精度不够先只出 L1"预案）。

### 3.3 只报现有规则看不到的（通道分工）

* **F-20**：每条判定带两个层数：`depth`（访问点到汇合点经过的调用边数）与 `effDepth`（判定用到的大小、偏移、长度里，经效果向上传得最远的层数）。
  报告条件：`max(depth, effDepth) > InlineDepth`（默认 2，取你们现有规则的 inline 上限，`07-` 输入 I6）。
  理由：现有规则要 inline 到大小和访问**两侧**才能判，任何一侧超出 inline 深度它就看不到【推断】。
* **F-21**：`csaBlindSpot` 的判定（F-08、二维数组字段 `[P1]`）**不做层数过滤**。
* **F-22**：`EvalMode=true` 时不过滤，全报并在消息里带层数，用于和现有规则逐条对比（`03-` T1-5）。

### 3.4 告警内容

* **F-30 位置**：汇合点函数里**那次调用的调用表达式**（被判定的条件从这里进入）。`depth = 0`（只在 F-21 / F-22 时才会报）报在访问点本身。
* **F-31 BugType**（英文，`csa-checker-dev` 约定；category 只用于展示）：
  `"Out-of-bound access via call chain (definite)"`、`"Out-of-bound access via call chain (possible)"`，category `"Memory error"`。
* **F-32 消息**〔由 SPEC-0021 修订：加 SUSPECT / SAFE 的模板〕（英文模板，`{}` 为占位）：

  ```
  L1: Definite out-of-bound {write|read} through a {N}-level call chain: up to byte {need} of {object} ({size} bytes) is accessed
  L2: Possible out-of-bound {write|read} through a {N}-level call chain: up to byte {needHi} of {object} ({size} bytes) may be accessed; the bound comes from {source}
  ```

  **全部占位都取自判定记录的机器字段**（D-26），不读 `doc`：`{N}` = `depth`，`{need}` / `{needHi}` = `end[1]`，`{size}` = `have`，
  `{object}` 由 `obj` 渲染——`g:` / `local:` 写变量名（`'aucName'`），`heap@位置` 写 `heap object allocated at alm_top.c:42`，
  `*pK->f` 等按形参名渲染（`'pstCtx->aucBuf'`）；有 `via` 时在大小后面加 `; via stCtx.pucBuf`。
  `{source}` = `bound` 的短语（`a loop condition`、`a check`、`a guard condition`、`a bit mask`、`a branch constant`、`a return value range`、`a constant`）。
  调用链跨了文件时，消息末尾附整条链 `[A (a.c:22) -> B (b.c:6) -> …]`（F-33 ③）。`{size}`、`{need}` 可以是符号式（LV2-04：`4*ulNum+4` 对 `4*ulNum`）。
  （v3.13 样例里 L2 写的 "a loop bound **in Ue_InitTbl**" 是手写的：界在哪个函数给出，机器字段里没有。要的话 P1 给 Req 加 `bound_at`，见 §12。）
* **F-33 trace（note）**：
  ① 一条**总链 note**，锚在告警位置：`Call chain: Ue_Setup (ue_init.c:80) -> Ue_InitTbl (ue_init.c:60) -> Ue_InitOne (ue_tbl.c:40)`（由 `chain` 生成；
  访问的字节数和偏移式看访问点那条 note 指着的源码行）；
  ② 链上每一跳、访问点、大小来源（分配点）里**位于本 TU 可见文件**的，各一条 note 指向那一行；
  ③ 不在本 TU 的只出现在 ① 的文本里（D12）。
* **F-34 指纹**：`llvm::xxHash64("OobSummary|" + level + "|" + 汇合点函数键 + "|" + access.key + "|" + R/W + "|" + access.end + "|" + obj)`，
  十六进制 16 位，**不含行号**（`01-` §3.10）。访问用"访问函数键 + 访问点自身的末端式"标识（v3.14 起替代"访问序号"：
  序号要看整个 FIR 才能定，末端式就在摘要里；同一函数里同类、同末端式的两次访问本来就是同一个检查，合并正合适）。
  同一指纹全工程只报一次。`replay.py` 里的 XXH64 与 LLVM 14 的 `llvm::xxHash64` 逐位相同【实测数据：同一输入两边都是 `dc29cb0defb73e92`】。
* **F-35 确定性**：同一 TU 的告警按（文件、行、列、指纹）排序后发出（`csa-checker-dev` 约定）。

### 3.5 账本与可查性

* **F-40**：每个函数的摘要都有 `status`：`analyzed`、`partial:<原因>`、`skipped:<原因>`（`01-` §3.4 全函数性）。**"没分析"必须和"分析了没问题"可区分**。
* **F-41**：记账条目带原因（§5.3 的枚举）、位置、函数、明细；`stats.json` 按原因汇总，W10 起作为补模型、补能力的依据。
* **F-42**：`oobsum.py explain <指纹 | 函数名>` 打印该告警 / 函数的逐层摘要与代换过程（用 `05-` §2.1 的记法），供误报研判。

---

## 4. 外部接口

### 4.1 注册（五处落点，`csa-checker-dev` 约定）

| 落点 | 内容 |
|---|---|
| 源码 | 入口 `xir/llvm_ast/StaticAnalyzer/Checkers/OobSummaryChecker.cpp`（平铺，与其它 checker 一致）；引擎在同级子目录 `OobSummary/`（文件清单见 §9.1） |
| 源码 CMakeLists | `add_library()` 末尾追加入口与 `OobSummary/...cpp`（**I-01**：子目录路径能否这样追加，由探针 R1 的编译验证，探针本身就是这个布局） |
| `Checkers.td` | `let ParentPackage = RosaRBEnhanceIpa in { … }` 块末尾追加 `def OobSummaryChecker : Checker<"OobSummary">`（D7 待确认） |
| 用例目录 | `xir/llvm_ast/TestCase/OobSummaryChecker/`：`src/` 放由 `cases/` 生成的用例（带四块文件头）、`include/` 放公共头 |
| 用例登记 | `TestCase/CMakeLists.txt` 末尾 `add_subdirectory(OobSummaryChecker)` |

命名：类 / 文件 / `def` / `register…` 带 `Checker` 后缀；`Checker<"OobSummary">` 与 `-checker=rb.enhance.ipa.OobSummary` 用短名。

### 4.2 选项（`CheckerOptions`，全部 `Released`）

| 编号 | 选项 | 类型 | 默认 | 说明 |
|---|---|---|---|---|
| I-10 | `Phase` | string | `inline` | `inline` / `export` / `solve` / `report`（§7） |
| I-11 | `WorkDir` | string | `""` | 工作目录（§4.5）。`export` / `solve` / `report` 必填 |
| I-12 | `InlineDepth` | int | `2` | F-20 的阈值 |
| I-13 | `EvalMode` | bool | `false` | F-22 |
| I-14 | `ReportL2` | bool | `true` | F-16 |
| I-15 | `ModelFile` | string | `""` | 模型表（§4.4）；空则用内置的最小模型（安全函数族 + libc） |
| I-16 | `AllocConfig` | string | `""` | 现有 `config.ini` 的路径（读 `MEM_ALLOC` / `MEM_FREE`）。显式传入，不依赖 fork 内部读 `-rbchecker-config` 的接口 |
| I-17 | `AllocIdentity` | string | `""` | `alloc_free_identity` 结果 JSON 的路径（数组或逐行 JSON 均可） |
| I-18 | `MaxBlocks` | int | `5000` | 超过即 `skipped:cfg_too_large`（探针给出 P99 后再定） |
| I-19 | `TimeLimitMs` | int | `5000` | 单函数求解时间上限，超时 `skipped:timeout` |
| I-20 | `MaxConds` | int | `64` | 单函数 Req 上限，超出按严重度保留、其余记账 `cond_truncated` |
| I-21 | `Dump` | string | `""` | 调试输出：`fir`、`summary`、`trace` 的逗号组合，打印到 stderr（规范记法） |
| I-22 | `FuncFilter` | string | `""` | 只对名字匹配的函数打印调试输出（正则） |
| I-23 | `Verbose` | int | `0` | `1`：每 TU 一行统计；`2`：每函数一行 |
| I-24 | `CallTrace` | string | `full` | 摘要里 `calls[]` 调用点记录的详细程度：`full`（实参、局部区间、守卫、结局，调试用）/ `results`（只留结局）/ `none`。全量跑出来体积过大时降级 |
| I-25 | `Emit` | string | `machine` | 摘要里写不写说明：`machine`（只写机器字段，D-30）/ `doc`（另生成 `doc`：形参名渲染的一句话、源码原文、宏值，调试与评审用；判定结果不变） |

### 4.3 运行命令

**单文件 / 单目录（Phase=inline，同 TU 内的多层链）**——与 `csa-checker-dev` 的标准命令一致，只换 `-checker=`，ipa 包不加 `ipa=none`：

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/src/lv1_01_stat_enum_butt.c \
  -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

**全量 / 跨 TU（export → solve → report）**——用驱动，一条命令：

```bash
python3 /usr1/BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/OobSummary/tools/oobsum.py run \
  --bxen /usr1/BXPlugin/cmake-build-release-release/bxen \
  --cc-json <业务工程>/compile_commands.json \
  --work /usr1/oobsum-work \
  --alloc-config /usr1/clang_check/src/cfg/ini/config.ini \
  --rbchecker-config /usr1/clang_check/src/cfg/ini/config.ini \
  --funcptr-config /usr1/clang_check/src/cfg/ini/config_funcptr.ini
```

驱动拼出的三条 bxen 命令会原样打印出来（可单独重跑），形如：

```bash
# ① export：全量，每个 TU 写 fir/<tu>.json
bxen -exec_mode=onlySa -cc_json=<业务工程>/compile_commands.json -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:Phase=export -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work -rbchecker-config=... -funcptr-config=..."
# ② solve：驱动生成的桩 TU（WorkDir/solve_stub/），一次
bxen -exec_mode=onlySa -cc_json=/usr1/oobsum-work/solve_stub/ -analysis_file=/usr1/oobsum-work/solve_stub/stub.c -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-config rb.enhance.ipa.OobSummary:Phase=solve -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work ..."
# ③ report：只含有判定的 TU 的 cdb（WorkDir/report_cdb/compile_commands.json）
bxen -exec_mode=onlySa -cc_json=/usr1/oobsum-work/report_cdb/compile_commands.json -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:Phase=report -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/usr1/oobsum-work ..."
```

**I-30 驱动子命令**（`oobsum.py`，Python 3.6+ 标准库）：

| 子命令 | 作用 |
|---|---|
| `run` | 依次 export → solve → report；`--from <阶段>` 断点续跑 |
| `export` / `solve` / `report` | 单独跑一个阶段 |
| `status` | 各阶段产物是否齐全、TU 失败清单、函数状态统计 |
| `explain <指纹 \| 函数名>` | F-42 |
| `stats` | 打印 `stats.json`：告警按级别 / 层数、账本按原因、耗时 |
| `sqlite` | 生成 `WorkDir/oobsum.sqlite`（§4.6） |
| `check --cases <cases 目录>` | 把结果与用例头里的 `@expect` 对照（验收用，§10） |

### 4.4 配置

**I-40 模型表 `oob_models.yaml`**〔由 SPEC-0018、SPEC-0033 修订：默认表编进代码，`ModelFile` 合并进来，示例 `oobsum/config/oob_models.example.yaml`〕（`llvm::yaml` 读取；默认随代码放 `OobSummary/config/oob_models.yaml`）：

```yaml
allocators:                     # 底层分配器：只配最底层几个，封装函数由摘要推出（01- §3.7.1）
  - { name: VOS_MemAlloc, result: ret, size: "p2", maybe_null: true }   # 字节数是第几个形参：待你确认（07- 输入 I5）
  - { name: malloc,       result: ret, size: "p0", maybe_null: true }
  - { name: calloc,       result: ret, size: "p0*p1", maybe_null: true }
access:                         # 访问类函数：安全条件按字节
  - { name: memcpy_s,  conds: [ { base: p0, bytes: "p1", kind: W }, { base: p2, bytes: "p3", kind: R } ] }
  - { name: memmove_s, conds: [ { base: p0, bytes: "p1", kind: W }, { base: p2, bytes: "p3", kind: R } ] }
  - { name: memset_s,  conds: [ { base: p0, bytes: "p1", kind: W } ] }
  - { name: memcpy,    conds: [ { base: p0, bytes: "p2", kind: W }, { base: p1, bytes: "p2", kind: R } ] }
  - { name: memset,    conds: [ { base: p0, bytes: "p2", kind: W } ] }
pure:                           # 不改任何被跟踪内存、返回值无约束的函数（日志、打点）
  names:    [ LOG_Print, VOS_Printf ]
  patterns: [ "^LOG_", "^DIAG_", "_Trace$" ]           # 正则；清单由你补（07- 输入 I9）
```

* **I-41** `size` / `bytes` 是形参的线性式或乘积（`p0*p1`），解析成与摘要相同的表达式。
* **I-42** `AllocConfig`：只读 `MEM_ALLOC=<名字>,<下标>`（`-1` 为返回值，其余为出参）；`MEM_FREE` 读入但 P0 不用。**不新增 `config.ini` 的键**（v3.7 设想的 `MEM_ALLOC_SIZE` 改放模型表，免得动现有解析器）。
* **I-43** `AllocIdentity`：读 `func_name`、`func_type`、`para`、`offset`、`inner_wrapped_func`。函数体在分析范围内时**以摘要推导为准**，JSON 只做交叉校验（不一致写一条日志）；函数体不在范围内时当作分配器、大小记为未知（`01-` §3.7.2）。
* **I-44** 同名冲突：模型表 > `AllocConfig` > `AllocIdentity` > 摘要推导；函数体可见且模型表没配的，一律用摘要。

### 4.5 工作目录

```
WorkDir/
  fir/<tu>.json            ① 每个 TU 一份（原子写：先写 .tmp 再改名）
  solve_stub/              ② 驱动生成：stub.c + compile_commands.json
  summaries/<00..ff>.json  ② 按函数键哈希分 256 片
  decisions/<tu>.json      ② 汇合点所在 TU 的判定（③ 读它发告警）
  reports.json             ② 全部判定（含被层数过滤掉的，带 filtered 标记）
  ledger.jsonl             ② 账本，一行一条
  stats.json               ② 汇总
  report_cdb/compile_commands.json   ③ 驱动生成
  logs/                    各阶段 bxen 的输出
  oobsum.sqlite            ④ 可选
```

### 4.6 查询库（SQLite，驱动生成，供 `05-` 方案 E 与 AI 使用）

表结构即 `05-` §2.2 的 `summaries / reqs / effs / opens / reports` 五张表，外加 `funcs(fn_key, name, tu, file, line, status)` 与
`calls(caller, callee, site_file, site_line, kind)`。由 `oobsum.py sqlite` 从 JSON 灌入；C++ 侧不读写 SQLite（D10）。

---

## 5. 数据规格

### 5.1 键与位置（D13）

| 编号 | 对象 | 键 | 例 |
|---|---|---|---|
| D-01 | 外部链接函数 | `F:<名字>` | `F:Ue_InitOne` |
| D-02 | 内部链接函数（`static`） | `F@<定义文件 id>:<名字>`；定义在头文件里的再加 `#<FIR 哈希前 8 位>`〔SPEC-0003：S2 先不加，S4 有 FIR 后补〕 | `F@3fa1…:Ue_Fill`、`F@9c02…:Pub_Min#a1b2c3d4` |
| D-03 | 外部链接全局 | `G:<名字>` | `G:g_astCellCtx` |
| D-04 | 内部链接全局 / 函数内 `static` | `G@<定义文件 id>:<名字>` / `S@<函数键>:<名字>` | |
| D-05 | 文件 id、TU id | 规范化绝对路径（`make_absolute` + `remove_dots`）的 xxHash64，16 位小写十六进制 | 探针已按此实现【实测数据】 |
| D-06 | 位置 | `[文件, 行, 列]`，取宏展开位置；JSON 里文件写路径，FIR 内部用下标 | |

* **D-07 同名外部函数有多个定义**（同一个 cdb 里有多个产品 / 二进制）：调用方解析时取**与调用方 TU 公共目录前缀最长**的那个定义；
  仍并列的，记账 `ambiguous_callee`、不代换。探针 `probe_stats.py` 会给出这类名字有多少（`07-` 输入 I2）。
* **D-08** 头文件里的 `static inline` 各 TU 一份；FIR 哈希相同的只求解一次。

### 5.2 FIR（`oobsum-fir/1`）

FIR 是"能决定越界的最少信息"：类型只留大小与字段布局，表达式只留整数 / 指针运算，访问都显式化。**引擎只看 FIR，不看 Clang**（D9）。

```json
{ "schema": "oobsum-fir/1", "tu": "abe9fde904012438", "file": "/src/ue/ue_init.c",
  "target": { "triple": "aarch64_be-unknown-linux-gnu", "ptr": 4, "long": 4, "big_endian": true },
  "files": [ "/src/ue/ue_init.c", "/src/inc/ue.h" ],
  "records": [ { "id": 0, "name": "UE_TBL_S", "size": 16,
                 "fields": [ { "name": "ulId", "off": 0, "t": { "k": "int", "w": 32, "s": false } },
                             { "name": "aucData", "off": 4, "t": { "k": "arr", "n": 12, "elem": { "k": "int", "w": 8, "s": false } } } ] } ],
  "globals": [ { "key": "G:g_ulUeNum", "name": "g_ulUeNum", "def": true, "t": { "k": "int", "w": 32, "s": false }, "loc": [0, 12, 8] } ],
  "funcs": [ { "key": "F:Ue_InitOne", "name": "Ue_InitOne", "ext": true, "loc": [0, 40, 6],
               "ret": { "k": "void" },
               "locals": [ { "id": 0, "name": "pstTbl", "param": 0, "t": { "k": "ptr", "to": { "k": "rec", "id": 0 } } },
                           { "id": 1, "name": "ulIdx",  "param": 1, "t": { "k": "int", "w": 32, "s": false } } ],
               "entry": 2, "exit": 0,
               "blocks": [ { "id": 2, "insts": [
                   { "op": "store", "addr": ["padd", ["l", 0], ["bin", "mul", ["l", 1], ["c", 16]]], "size": 4, "val": ["c", 0], "loc": [0, 42, 5] } ],
                 "succ": [ { "to": 0 } ] },
                 { "id": 0, "insts": [], "succ": [] } ] } ] }
```

**D-10 类型** `t`：〔Clang 类型的映射见 SPEC-0007 §2.1–§2.2：函数类型 / 不完整类型 → `{other, size: 0}`，`char` 的符号取编译选项，位域的 `w` 是位宽〕`{k:int,w,s}`、`{k:ptr,to}`、`{k:arr,n|null,elem}`（`n` 为 null 即柔性 / 不完整）、`{k:rec,id}`、`{k:void}`、`{k:other,size}`。
记录类型放模块级 `records` 表，字段带字节偏移（**按被分析 TU 自己的目标 ABI 算**，即 ILP32 与否以 cdb / fork 为准，§5.4）。

**D-11 局部**：形参也是局部（带 `param` 下标），入口处的值是接口符号；`obj: true` 表示是内存对象（数组、结构体、取过地址的标量），
对它的读写一律走 `load` / `store`；其余标量走 `set`。降级产生的临时值是 `tmp: true` 的局部，**永远不进摘要**。

**D-12 指令**（每条带 `loc`）：

| `op` | 字段 | 语义 |
|---|---|---|
| `set` | `dst`, `val` | 标量赋值 |
| `load` | `dst`, `addr`, `size` | 读 `size` 字节（同时是一次 **R** 访问） |
| `store` | `addr`, `size`, `val`（可为 null） | 写（同时是一次 **W** 访问） |
| `call` | `dst`（可无）, `callee`（函数键或 `["ind", E]`）, `args`, `ord`（调用点序号） | 调用（安全函数等访问类函数的条件由模型表给出） |
| `havoc` | `dst`, `why` | 值未知（内联汇编、不支持的构造），`why` 进账本 |
| `ret` | `val`（可为 null） | 返回值；其后一条边到 `exit` |

**D-13 表达式**（纯，无副作用；副作用已被 CFG 线性化成前面的指令）：〔由 SPEC-0005 §2.3 细化：`bin` 可省 `w,s`（数学整数）、`cmp` 必带 `s`、常量严格在 int64 内、可选键省略 / 可为 null 的键显式 null、读入严格〕

| 形式 | 含义 |
|---|---|
| `["c", v]` | 整数常量（含 `sizeof`、枚举、宏展开后的常量） |
| `["l", id]` | 局部的当前值 |
| `["bin", op, a, b, w, s]` | `add sub mul div rem shl shr and or xor`，结果宽度 / 有无符号 |
| `["cmp", op, a, b, s]` / `["lnot", a]` / `["neg", a]` / `["bnot", a]` | 比较与一元运算 |
| `["cast", w, s, a]` | 整数转换（截断 / 扩展） |
| `["laddr", id]` / `["gaddr", key]` / `["str", len]` / `["faddr", key]` | 局部对象、全局对象、字符串字面量（对象 `len + 1` 字节）、函数的地址 |
| `["padd", P, E]` | 指针加**字节**偏移（下标乘元素大小在降级时完成；指针类型转换在 FIR 里不存在） |
| `["fld", P, off, size, name]` | 取**数组成员**的地址：偏移 `off`，并把可访问范围收窄到这个成员（子对象边界，F-01） |
| `["psub", P1, P2, elem]` | 指针相减（元素数） |
| `["unk", why]` | 未知值 |

**D-14 边**：〔SPEC-0009 实现：后继 0 为真、`&&` / `||` 第二块测最后一个元素、常数条件与 GNU 区间不带条件、`default` 为各 `!=` 的按位与；A-04 推广到汇合块的所有前驱〕`succ: [{to, assume?}]`。`if` / 循环条件降成两条边上的 `assume E` / `assume ["lnot", E]`；`switch` 降成每个 `case` 一条
`assume x == c` 与 `default` 一条（`case` 超过 16 个时 `default` 不带 assume）。`&&`、`||`、`?:` 由 Clang CFG 拆好，
`?:` 的结果在两个前驱块末尾各补一条 `set tmp`（**A-04**）。

### 5.3 摘要（`oobsum-summary/1`）——v3.13 定稿，v3.14 划清机器字段与说明

> **一个函数一份 JSON**，表达式都写成人能直接读的字符串。完整字段定义：`cases/summaries/summary.schema.json`（JSON Schema）；
> 16 个用例的 104 份实样与逐步回放：`08-options-and-summary-examples.md` Part B 与 `cases/summaries/`。
> 全量运行时落在 `WorkDir/summaries/<00..ff>.jsonl`（按函数键哈希分片，一行一份）；`oobsum.py show <函数名>` 打印成实样那样。

**D-30 机器字段与 `doc`（v3.14）**：摘要里只有两类内容。

* **键名为 `doc` 的**（任意层级，字符串或对象）：只给人看的说明——用形参名渲染的可读形式、源码原文、宏的值、"为什么"。
  **引擎读入时把所有 `doc` 整棵删掉，任何计算（代换、判定、出告警、指纹）都不读它。**
* **其余全部是机器字段**：整数、枚举、规范表达式（D-21）、对象（D-20）、位置（`文件:行`）、函数键。**去掉 `doc` 后就是完整摘要**——
  逐个调用点的判定、上传、记账，以及最终告警的位置、消息（F-32 由模板生成）、调用链 note（F-33）、指纹（F-34）都只用它们算出来。
* 保证手段：① schema 里除 `doc` 外每个对象都是 `additionalProperties: false`，多一个键（比如顺手加的 `note`）校验就不过；
  ② `replay.py` 检查 `doc` 之外不出现中文或成句的英文；③ `replay.py --strip DIR` 生成去掉 `doc` 的机器版，`--dir DIR` 从机器版重跑，
  结果与读原文件**逐字节相同**（v3.14 实测：13 条最终告警的 JSON 一致，回放输出只有实参的显示名不同）。
* `Emit=machine`（I-25，默认）只写机器字段；`Emit=doc` 额外生成 `doc`，供调试与评审。T-06 快照比的是去掉 `doc` 后的 JSON。
* 用例目录里的 `case.json`（标题、要点、代码）是走查文档用的元数据，**不是摘要**，引擎不产生也不读。

**D-20 接口符号**（摘要里允许出现的符号）与**对象写法**〔由 SPEC-0039 修订：加 `bext(x)`，指针 `x` 前面还有多少字节〕：

| 符号 | 含义 |
|---|---|
| `p0`、`p1`… | 第 i 个形参在入口处的值（`params` 数组按下标给出名字与类型） |
| `ext(p0)` | 形参指针所指对象从该指针起还剩多少字节 |
| `p0->ulLen`、`p0->pstNext->ulLen` | 经形参可达的字段在入口处的值（最多两次解引用）；偏移、大小、类型在顶层 `fields` 表里 |
| `ext(p0->pucBuf)` | 指针型字段所指对象的剩余字节数 |
| `g:g_x`、`ext(g:g_px)` | 全局在入口处的值（P0 只记录、不跟踪传播）；全局指针的剩余字节数 |
| `div(e, n)`、`mod(e, n)`、`wrap32(e)`、`wrap16(e)`、`mul(a, b)` | 不透明项：除以常数、取模、无符号回绕、两个变量相乘；代换后内层是常数才求值 |

| 对象 `obj` | 含义 | 在调用点怎么代换 |
|---|---|---|
| `g:g_aulCellStat` | 全局对象 | 不变 |
| `local:aucPkt` | 本函数的局部对象 | 不变（只出现在判定和实参里） |
| `heap@ue.c:68` | 在这个调用点分配的堆对象 | 不变 |
| `*p0` | 第 0 个形参所指的对象 | 换成第 0 个指针实参的 `obj` |
| `*p0->pucBuf` | 形参所指结构体里指针字段 `pucBuf` 所指的对象 | 换成实参 `fields.pucBuf.obj`（判定里记 `via`）；实参是形参别名时改写成调用方的 `*pK->pucBuf` |
| `p0->aucBody` | 形参所指结构体里的数组成员 | 实参是形参别名时改写成 `pK->aucBody` |

调用点记录（`calls[]`）里还会出现**调用方的局部名**（如 `i`、`ulOff`），只用来描述"实参是什么"，**不进 Req / Eff**。

**D-21 表达式语法**：

```
expr   := ['-'] term (('+' | '-') term)*
term   := factor ('*' factor)*              至多一个非常数因子；两个都不是常数时成为 mul(a, b)
factor := INT | '(' expr ')' | name | 'ext(' name ')' | op '(' expr [',' expr] ')'
name   := IDENT [':' IDENT] ('->' IDENT)*    p0、p1->ulLen、g:g_x、调用方局部名
op     := 'div' | 'mod' | 'wrap32' | 'wrap16' | 'mul'
cond   := expr ('<=' | '<' | '>=' | '>' | '==') expr
```

读入时解析成线性式（64 位有符号系数，溢出则整式降为不透明项〔SPEC-0010：实现为整式"未知"；`div` / `mod` 的常数求值向下取整，同 `replay.py`〕）；**比较两条表达式一律比规范形式**：
形参 → 字段 → `ext(…)` → 全局 → 局部 → 不透明项，常数最后（`16*p1 - 12`）。C++ 侧的解析器约 200 行，Python 侧见 `replay.py`。

**D-22 来源标签** `bound`〔由 SPEC-0034 修订：加 `model`——库函数大小实参给的上界，代码给出的界，优先级在 `data` 之后、`check` 之前〕：`exact`（没有区间）、`const`、`check`、`mask`、`branch`、`loop`、`ret`、`guard`（以上是代码给出的界，可判 L2）；
`type`、`widen`、`data`（不可判 L2，只能记账）。

**D-23 顶层与 Req**〔由 SPEC-0015、SPEC-0019、SPEC-0039 修订：负偏移用反向 Req（`dir: back`，`have` 是 `bext(…)` / 0）判〕（下表只列机器字段；每个对象都可以另带 `doc`）：

| 顶层字段 | 含义 |
|---|---|
| `schema` | `"oobsum-summary/1"` |
| `fn`、`key`、`tu`、`loc` | 函数名、键（D-01…D-08）、定义所在 TU（跨 TU 时）、定义位置 |
| `params` | `[{name, type}, …]`，下标即 `p0`、`p1`… |
| `fields` | `{"p0->ulBufLen": {"off": 4, "size": 4, "type": "UINT32"}, …}` |
| `status` | D-28 |
| `reqs` / `effs` / `calls` / `reps` / `opens` | 前置条件 / 效果 / 调用点记录 / 本函数判定的越界 / 账本 |

| Req 字段 | 含义 |
|---|---|
| `id`、`kind` | 编号、`R` / `W` |
| `have`、`obj` | 从基址起对象还剩多少字节（常数、`ext(…)`）；是哪个对象（D-20 写法） |
| `end` | `[下界, 上界]`：访问末端（偏移 + 字节数）的范围。**安全条件就是 `end[1] <= have`** |
| `start` | 最小偏移（说明用；负偏移由反向 Req 判，SPEC-0039） |
| `bound` | `end[1]` 的来源标签（D-22） |
| `guard` | 守卫：只有它们成立时才发生这次访问 |
| `access` | `{fn, key, at, end}`：真正的访问点——函数、函数键、位置、访问点自身的末端上界（访问函数的符号）。代换过程中原样上传，`key + end` 用于指纹 |
| `path` | 从本函数到访问点经过的调用点（`函数@文件:行`） |
| `depth`、`eff_depth`、`blind` | F-20、F-21 |
| `absorbed` | 被本条覆盖、不再单列的较弱访问 `[{kind, end, at}]`（A-44） |
| `doc` | `{text, obj, bytes, code, note}`：用形参名渲染的一句话、对象名、访问字节数、源码原文、说明 |

**D-24 效果** `effs[].kind`：`ret_new`（`size`、`maybe_null`、`eff_depth`）、`ret_ptr`、`ret_int`（`range`、`bound`）、
`ret_cond`（`when: "ret == c"`、`then: [条件…]`，至多 4 条）、`out_new` / `out_int`、`field`（`target: "p0->ulUsed"`、`value`）、`global`、`invalidate`。
`doc.via` 记效果是从哪个分配器 / 封装传上来的（只给人看）。

**D-25 调用点记录**〔由 SPEC-0015、SPEC-0027、SPEC-0035、SPEC-0039 修订：成员数组实参加 `parent: {obj, have}`，`results[]` 的外层对象那一条标 `parent: true`；指针实参加 `back`（指针前面的字节数）〕 `calls[]`：`site`、`callee`、`model`（`access` / `alloc` / `pure` / `unknown`，被调者是模型或未知时）、
`args`（整数实参写表达式；指针实参写 `{obj, have, alias?, fields?, eff_depth?}`——指向哪个对象、从这个指针起还剩多少字节、
就是调用方的哪个形参、栈帧里记着的字段值；与越界无关的写 `null`）、`locals`（`{名字: {range: [lo, hi], bound}}`）、
`guards`（`[{cond, bound}]`）、`results`——被调者每条 Req / Eff 在这里的结局，全部结构化：

| `results[]` 形状 | 含义 |
|---|---|
| `{req, outcome: "up", as}` | 代换后上传成本函数的 `as` |
| `{req, outcome: "safe"}` | 这里已证明安全 |
| `{req, outcome: "L1"\|"L2", report}` | 这里判出越界，记在 `reps[]` 的 `report` |
| `{req, outcome: "open", reason}` | 到此为止，记账 |
| `{eff, apply}` | 被调者第 `eff` 条效果代进来后的样子（`apply` 与效果同形，用调用方的符号） |
| `{gen: [Req id…]}` / `{gen_eff: [序号…]}` | 访问类 / 分配器模型在本函数生成的 Req / 效果 |
| `{pure: true}` / `{open: reason}` | 纯函数模型 / 未知函数记账 |

详细程度由 `CallTrace`（I-24）控制。

**D-26 判定** `reps[]`：`{id, level, reported, site, access, kind, obj, via?, have, end, bound, depth, eff_depth, chain, fp}`，`doc.msg` 是 F-32 模板的生成结果（副本）。
全局告警记录 `reports.json` 每条是 F-30…F-35 的成品（`checker`、`bug_type`、`category`、`level`、`fn`、`loc`、`msg`、`notes`、`fp`），
另带 `decision_fn`、`decision_tu`（`report` 阶段按它分发）。`replay.py --alerts FILE` 输出的就是这个形状。

* **D-27 账本**〔由 SPEC-0019 修订：加 `typed_field`；SPEC-0021：`not_in_tu` 在调用点、每个调用点一条〕 `opens[]` / `ledger.jsonl`：`{reason, at, req?}`（`req` 指向入口处到此为止的那条 Req），`doc` 写细节。`reason` 取值：
  `01-` §3.12 的 16 类（`fptr`、`unknown_callee`、`recursion`、`msg_edge`、`global_value`、`global_ptr_target`、`field_written_by_callee`、
  `alloc_size_unknown`、`flexible_array`、`struct_overlay`、`pointer_stepping`、`string_length`、`nonlinear`、`type_range_only`、`entry_unproven`、`cond_truncated`）
  加运行类 11 个：`ambiguous_callee`、`not_exported`（被调者不在 cdb 里）、`not_in_tu`（inline 模式下的跨 TU 被调者）、`cfg_too_large`、`timeout`、
  `iter_limit`、`unsupported_construct`、`struct_by_value`、`inline_asm`、`imprecise_join`（两个不可比的符号界合并后丢失）、
  `untracked_pointer`（指针来自数组元素或超过两层的字段，不跟踪它指向多大）。
* **D-28 状态**〔由 SPEC-0037 修订：加 `skipped:no_cfg`；`partial:*` / `skipped:*` 在声明处报 SUSPECT `unanalyzed:<后缀>`〕 `status`：`analyzed`；`partial:iter_limit|cond_truncated`；`skipped:cfg_too_large|timeout|no_body`。

**D-29 可读输出**：`doc.text`、`Dump=summary` 与 `oobsum.py show / explain` 都用形参名渲染（`ulNum >= 1 ⇒ 写 4 字节 @ *pstTbl，末端 [4, 16*ulNum - 12]；需 16*ulNum - 12 <= ext(pstTbl)`），
与用例文件头的"摘要"块同形。它们都是从机器字段**派生**的；T-06 的快照测试比的是去掉 `doc` 的 JSON（规范形式）。

### 5.4 大小与布局（ExtentModel 的 P0 部分）

| 编号 | 对象 | 大小来源 | 备注 |
|---|---|---|---|
| D-30 | 局部 / static / 全局数组、结构体（含无初始化器） | `ASTContext` 的类型大小 | `01-` §3.1 #1–#8 |
| D-31 | 数组成员（含嵌套、结构体数组元素的成员、形参结构体指针的成员） | 成员类型大小；访问以成员为界（`fld`） | #2–#7、#11 〔由 SPEC-0022 修订：外层对象的边界也要满足〕 |
| D-32 | 形参指针 | 符号 `ext(p0)` 等 | #10 |
| D-33 | 分配器 / 封装分配器的返回值、出参 | 模型表的 `size` 代入实参；封装函数由 `ret_new` 效果逐层推出 | #12、#13、#15、#20 |
| D-34 | `extern` 全局 | 求解时取定义所在 TU 的大小；全工程都没有定义的 → 未知 | #9 |
| D-35 | 字符串字面量 | 长度 + 1 | #19 |
| D-36 | VLA | 符号或区间 | #16 |
| D-37 | 柔性数组成员 `[]`、`[0]`、末尾 `[1]` | **不按成员收窄**：以整个对象为界；对象大小未知时记账 `flexible_array` | #17；P0 对末尾 `[1]` 一律按柔性处理，宁可漏报不误报 |
| D-38 | 指向数组中间的指针 | 基址 + 偏移，不读 ExprEngine 的地址 | #21，`csaBlindSpot` |

* **D-39** 字节布局取被分析 TU 的 `ASTContext`：fork 按 ILP32 分析业务代码时（探针 R2 的 `ptr=4`）布局即真实布局；
  若是 `ptr=8`，含指针 / `long` 的结构体偏移会与真实不同，**数组下标与元素个数之比不受影响**，影响面在 `03-` T0-1c 已列出。

---

## 6. 算法规格

### 6.1 降级：Clang → FIR（`lower/`，只有这一层碰 Clang）

| 编号 | 规则 |
|---|---|
| **A-01** | CFG 用 `AnalysisManager::getCFG(fd)`（与 CSA 同一份）。按块号、块内元素顺序降级；每个子表达式的值按 `Stmt*` 记忆，纯子表达式内联成 `Expr` 树，有副作用的（调用、赋值、`++`）生成指令并落到临时局部 |
| **A-02** | 左值降成地址表达式：局部对象 → `laddr`；全局 → `gaddr`；`a[i]` → `padd(基址, i × 元素大小)`；数组成员（`.` / `->`）→ `fld`；标量成员 → `padd(基址, 偏移)`；`*p` → `p`；字符串字面量 → `str` |
| **A-03** | 右值：未取过地址的标量局部 → `["l", id]`；其余左值转右值 → `load`（同时是一次 R 访问）；整数转换 → `cast`；**指针类型转换在 FIR 里消失**（偏移都按字节）；数组退化 → 首元素地址；`sizeof` / 枚举 / 常量表达式 → `EvaluateAsInt` 成常数；浮点 → `unk` |
| **A-04** | `?:`、`&&`、`||` 作为值使用时，在两个前驱块末尾补 `set tmp`，汇合块引用 `tmp` |
| **A-05** | 调用：直接调用 → 函数键；`__builtin_memcpy` 等映射到 libc 名；`__builtin_expect(x, c)` → `x`；间接调用 → `["ind", E]` |
| **A-06** | 结构体整体赋值 `a = b` → 一次 `call __oobsum_copy(&a, &b, sizeof)`（内置访问模型：W / R 各 `sizeof` 字节）；结构体按值传参 → 实参 `unk(struct_by_value)` |
| **A-07** | 下标访问在 `load` / `store` 上带 `sub: true`；引擎据此识别 F-08 的盲区形态（指针来自 `&a[k]` / `a + k`、再用非常量下标） |
| **A-08** | 不认识的构造 → `havoc` 或 `unk`，带原因；**函数照样导出**（全函数性） |
| **A-09** | 块数 > `MaxBlocks` 的函数只导出签名与 `flags: ["cfg_too_large"]`，求解时 `skipped:cfg_too_large` |

### 6.2 抽象域（`core/`，不依赖 Clang）

* **A-10 整数值** `AbsInt = [lo, hi]`，`lo` / `hi` 是 **界**：`LE`（接口符号的线性式 + 不透明项）、`min(c, LE)` / `max(c, LE)`、`±∞`。
  每个界带**来源标签**集合 `src ⊆ {const, check, mask, branch, loop, ret, guard, type, widen, data, unknown}`（F-12 用）；另带 `via`（经效果传了几层，饱和于 255）。
* **A-11 指针值** `PtrVal = (base, off: AbsInt 字节, maybeNull, via)`。`base` 种类：`Param(i)`、`ParamField(i, path)`、`Global(K)`、`GlobalPtr(K)`、`Local(id)`、
  `Sub(父, 偏移, 大小, 名字)`（数组成员子对象）、`Alloc(调用点, size: LE)`、`Str(len)`、`Ret(调用点)`（大小未知）、`Func(K)`、`Unknown(原因)`。
  `have(PtrVal)` = `size(base) − off`，其中 `size(Param(i)) := ext(p<i>)` 并约定 `off` 相对形参指针。
* **A-12 内存格** `cells: (base, 常量偏移, 大小) → 值`。形参可达的格第一次读到时取初值符号 `p<i>->字段`（两次解引用以内，偏移与大小记在 `fields` 表）；局部对象的格初值未知；
  非常量偏移的写 → 失效该 base 上重叠的格；经 `Unknown` 基址的写 → 失效所有非局部对象的格（按类型细分留 P1）。
* **A-13 定义式** `defs: 局部 → Expr`（只含局部与接口符号，深度 ≤ 4）：记录"`rem = total − off`"这类关系，操作数被重新赋值时失效。用于 §6.4 的同类项抵消（`01-` §3.6）。
* **A-14 守卫** `guards`：至多 8 个原子 `LE ≤ 0`（只含接口符号），先进先出。来源：`assume` 边（把局部的确切符号值代进去后仍含接口符号的比较）；
  带符号界的区间非空约束（`i ∈ [0, p1−1]` ⇒ `p1 ≥ 1`）。
* **A-15 合并**〔由 SPEC-0023 修订：差不是常数时用类型范围与两边都成立的守卫比较；SPEC-0042：仍比不出来时各自在自己路上的守卫下求常数界、取 min / max（上界须是代码给出的界）；SPEC-0043：再不行试符号候选——一边的界在另一边自己路上的守卫下也成立就取它，两个都成立时不取变量自己的原值〕：值逐点合并；两个界差为常数时取数值较大 / 较小者；一个是常数一个是 `LE` 时取 `max(c, LE)` / `min(c, LE)`；其余 → `±∞` 并带 `imprecise_join`。
  指针同 base 合并偏移；不同 base → `Unknown(base_join)`；一边是空指针常量 → `maybeNull`。守卫取交集；定义式保留两边相同的。
* **A-16 widening**：在 WTO 分量头，前 2 次合并照常，之后按阈值外推：阈值集 = 函数里所有比较中的常数 `c` 的 `{c−1, c, c+1}`，
  以及与接口符号式比较时的 `{E−1, E, E+1}`；没有阈值可用才到 `±∞`（`src` 加 `widen`）。**narrowing** 2 轮。
* **A-17 化简**：`min(max(c, x), x + d)`（`d ≤ 0`）→ `x + d` 等少量规则（与 Infer `Bound` 的 MinMax 化简同类）；化简失败保留原式。

### 6.3 转移

| 编号 | 指令 / 表达式 | 规则 |
|---|---|---|
| A-20 | `set` | 求值写入局部；更新定义式；失效引用了被写局部的定义式 |
| A-21 | `add` / `sub` / 乘常数 | 界逐项运算；乘负常数交换上下界 〔由 SPEC-0014 修订〕 |
| A-22 | 变量 × 变量 | 两边都是确切符号值 → 不透明项 `<mul a b>`（确切），区间由两边数值范围相乘；否则 `±∞`，`src: nonlinear` |
| A-23 | `/ c`、`>> k` | 确切符号值 → 不透明项 `<div a c>`（确切），区间为数值范围的商；**不丢成 ∞**（Infer 在 LV2-05 / LV2-06 上的误报即出于此【实测数据】） |
| A-24 | `% c`、`& m`（常数，非负） | `[0, c−1]` / `[0, m]`，`src: mask` |
| A-25 | `| ^ ~`、位域 | 两边常数则求值；否则类型范围，`src: type` |
| A-26 | 无符号减法可能为负 | 确切符号值 → `<wrapW a−b>`（代换成常数后按 2^W 取模）；区间为类型范围，`src: type` 〔由 SPEC-0014、SPEC-0017 修订〕 |
| A-27 | 截断 `cast` | 区间落在目标类型内则不变；否则 `<truncW a>`，区间为类型范围 |
| A-28 | `load` | 先做访问检查（§6.4）；值：命中格 → 格值；形参可达 → 初值符号〔由 SPEC-0040 修订：只在这块内存进函数后没改过时；改过（被调者拿到过指针、偏移不定的写、同类型形参的写）→ `data`〕；`const` 全局且有常量初值 → 常数；其余全局 → `g:K`〔由 SPEC-0041 修订：只在进函数后没可能被改过时；调用过可能写全局的函数、经未知指针写过、地址交给过被调者之后 → `data`〕；数组元素 / 消息内容 → 类型范围，`src: data` |
| A-29 | `store` | 先做访问检查；常量偏移、base 已知 → 强更新格；否则按 A-12 失效 |
| A-30 | `assume a op b` | 收窄出现在两边的局部（`l < E` → `hi := min(hi, E−1)`）；两边都是常数且为假 → 该边不可达；含接口符号 → 加守卫；`p != 0` / `p == 0` 收窄 `maybeNull`；被比较的局部是带条件效应的调用结果 → 应用条件效应（§6.6 第 5 步） 〔由 SPEC-0016、SPEC-0017、SPEC-0023 修订〕 |
| A-31 | `call` | §6.6 |
| A-32 | `havoc` / `unk` | 未知值，`src: unknown`，原因随值携带（只在它真正影响某次判定时才写进账本） |

**A-33 不动点**〔由 SPEC-0017 修订〕：Bourdoncle WTO（递归策略）；分量头上 widening / narrowing；每个分量头迭代上限 20 次、全函数 `TimeLimitMs`；
超限 → 仍不稳定的值置 `±∞`，`status = partial:iter_limit`，**照常产出摘要**。

### 6.4 访问检查与 Req 生成

对一次访问（`kind`、地址 `P`、字节数 `S`）：

1. **A-40 符号形**：把 `P` 求成 `(base, offExpr)`、`S` 求成 `sizeExpr`，其中 `offExpr` / `sizeExpr` 是**局部与接口符号的表达式**（按定义式展开，深度 ≤ 4），先不代区间。
2. **A-41 化简**：`D = offExpr + sizeExpr − have(base)` 线性化，**同类项抵消**（局部也参与抵消，LV3-07 的 `ulOff`）。
3. **A-42 代区间**：`D` 里剩下的局部换成它们的界（系数为正取上界、为负取下界），得到 `D` 的上界 `U` 与下界 `L`（只含接口符号），并记下所用界的来源标签；
   另对 `offExpr` 求下界，检查 `off ≥ 0`。
4. **A-43 当场判定**：`U`、`L` 不含符号时按 F-10…F-14 判；`depth = 0`，判出的进 `reps`（只有 F-21 / F-22 时才会报出，其余被 F-20 过滤，但照样入 `reports.json` 并标 `filtered`）。
5. **A-44 生成 Req**〔由 SPEC-0014、SPEC-0016、SPEC-0018 修订〕：含接口符号时，先用守卫判蕴含（§6.5）：`U ≤ 0` 成立 → 安全，丢弃；否则生成 Req：`have`、`end`（末端的 [下界, 上界]）、`start`、`bytes`、`bound`、
   与 Req 符号相关的守卫原子、访问位置与序号、`depth = 0`、`blind`（A-07）。**覆盖合并**：同一 `have`、守卫相同的两条 Req，若一条的 `end[1]` 恒不大于另一条
（差为非正常数），弱的那条并进强的、记进 `absorbed`（LV1-02 的 `pszName[0]`、`pszName[1]`）；守卫不同的不合并（LV2-03 TN 的 `R3`）。
6. **A-45 记账**：`have` 无法表示（`Ret` / `Unknown` base、未知大小的分配）→ 记账 `alloc_size_unknown` / 对应原因；上界只来自 `type` / `widen` / `data` → `type_range_only` / `pointer_stepping`；含 `nonlinear` → `nonlinear`。

### 6.5 蕴含判定（守卫 ⇒ 条件）

**A-50**：判定 `U ≤ 0` 是否在守卫 `G` 与符号取值范围 `R`（无符号形参 ≥ 0、`ext(·) ≥ 0`、类型范围）下恒成立〔SPEC-0011：实现同 `replay.py` 的 `entails_le0`，P0 不做第 3 步的两两组合〕：

1. 只用 `R` 求 `U` 的上界 ≤ 0 → 成立；
2. 对每个守卫原子 `g`、`U` 与 `g` 共有且系数同号的符号 `s`：`U′ = U − (a_s / b_s)·g`（有理系数），`U′` 的上界 ≤ 0 → 成立；
3. 再对守卫原子两两组合做一次第 2 步（至多 28 对）；
4. 否则"判不了"。

**A-51**：L1 的判定对称：`L > 0` 在 `G` 与 `R` 下恒成立。**A-52**：判定只用线性推理与区间，**不调 SMT**，代价有界。

### 6.6 调用点（`05-` §3.1 的五步）

| 步 | 编号 | 规则 |
|---|---|---|
| ① 取摘要 | A-60 | 直接调用 → 求解阶段已算好的摘要（SCC 逆拓扑序保证被调者先算）或模型；间接调用 → 被调表达式是定义式为 `faddr` 常量的局部时当直接调用（S9.1 尽力），否则记账 `fptr` |
| ② 代换表 | A-61 | `p<i> ↦` 实参的值与符号形；`ext(p<i>) ↦ have(实参指针)`；`p<i>->字段 ↦` 调用方在（实参 base，实参偏移 + o，s）处的格值——实参 base 是调用方形参时改写成调用方的字段符号（路径加一层，超过两次解引用则未知）；`g:K ↦ g:K` |
| ③ 代换并判定 | A-62 | 每条 Req：代换 `have` / `lo` / `hi` / `size` / 守卫，按 §6.4 第 2–6 步判定：安全 / L1 / L2 / 上传（`depth + 1`，`path` 前加本调用点，`eff_depth` 取所用值的 `via` 最大值）/ 记账。守卫代换后为假 → 这次调用不会走到访问 → 安全；守卫判不了且含调用方局部 → **不报**（避免误报），记账 `entry_unproven` |
| ④ 效果 | A-63 | `ret_new` → 结果为 `Alloc(调用点, 代换后的大小)`、`via + 1`；`ret_ptr` / `ret_int` → 代换后的值、`src` 加 `ret`；`out_*` / `field` → 写调用方的格；`invalidate` → 失效调用方相应的格；`global` → P0 只失效调用方对该全局的缓存 |
| ⑤ 条件效应 | A-64 | `ret_cond` 挂在接收返回值的局部上；之后在该局部上的 `assume` 选中 `ret == c` 那一侧时，把代换后的原子加入守卫并收窄实参对应的局部（LV4-06 `if (Check(id) != OK) return;`） |

**A-65 模型**：分配器 → `Alloc(调用点, size)`、`via = 0`；访问类 → 按条件生成访问检查（§6.4）；纯函数 → 不失效、结果未知。
**A-66 未知函数**（无摘要、无模型）：结果未知（原因 `unknown_callee` / `not_exported` / `not_in_tu`）；经指针实参可达的格失效。

### 6.7 效果生成（函数出口）

**A-70**：返回值 → `ret_new`（base 是 `Alloc` 且大小能用接口符号表示）/ `ret_ptr`（base 是形参或全局）/ `ret_int`（界能投影到接口符号）；
每条 `ret c` 处的守卫（只含接口符号）→ `ret_cond`（同一个 `c` 取交集，至多 4 条）；形参可达的格在出口处与初值不同 → `field` / `out_*`；
经未知写失效过的 → `invalidate`。**A-71 投影**：含非接口符号的界换成它的数值界（来源标签保留），没有数值界则为 `±∞`。

### 6.8 调度

* **A-80 solve**〔由 SPEC-0019、SPEC-0036 修订：同一入口同一次访问的账本第一条报出了其余只写日志〕：读全部 FIR；建函数表（D-07 处理重名）与全局大小表（D-34）；按直接调用 + 可静态解析的函数指针建全局调用图；
  Tarjan 求 SCC，逆拓扑序逐个求解；SCC 内（递归）按键排序各求一次，环内调用按未知处理并记账 `recursion`（`01-` §3.9）。
* **A-81 inline**：同样的过程，范围限于本 TU。
* **A-82** 求解过程**单线程、确定性**（所有遍历按键排序，不依赖指针地址）；并行求解 `[P1]`（按 SCC 层级分批）。

### 6.9 出告警

* **A-90**：`solve` 给每条未被过滤的判定定一个汇合点 TU（汇合点函数的 FIR 所在 TU），写进 `decisions/<tu>.json`。
* **A-91**：`report` / `inline` 阶段把 `[文件, 行, 列]` 经 `FileManager` + `SourceManager::translateFileLineCol` 换成 `SourceLocation`；
  换不出来（文件不在本 TU）→ 退回汇合点函数的声明位置，并把原位置写进消息。按 F-33 组 note，按 F-35 排序后发出；同一进程内按指纹去重。
* **A-92**（v3.14）：告警成品（位置、BugType、消息、note、指纹）**只由判定记录的机器字段生成**，不读任何 `doc`；
  参考实现是 `replay.py` 的 `alert_msg`、`make_alert`、`fingerprint`，C++ 的 `Report.cpp` 在 T-06 用例上的输出须与 `replay.py --alerts` 逐条相同。

### 6.10 全函数性与上限

**A-95** 任何函数都必须产出合法摘要：不支持的构造降为未知、迭代超限置 `±∞`、超时 / CFG 过大产出空摘要并标 `skipped`。
**A-96** 不得因单个函数 / TU 失败而中断整体：export 失败的 TU 记入 `logs/`，其中的函数在调用方看来是 `not_exported`；引擎对异常输入（读进来的 FIR 不一致、超出上限）一律返回错误码、
把该函数降级为 `skipped`，**不靠 `assert`**（release 构建里 `assert` 不生效，而 LLVM 不开异常）。

---

## 7. 跨 TU 执行（方案 B）

| 编号 | 阶段 | 规格 |
|---|---|---|
| **X-01** | export | 每个 TU 导出主文件与头文件里所有有函数体的函数、所有全局声明 / 定义（带类型与大小）、用到的记录类型、目标信息；写 `fir/<tu>.json`（紧凑 JSON，先写 `.tmp` 再改名）。重跑覆盖，幂等 |
| **X-02** | export | 不做任何求解，不发告警；只挂 `ASTDecl<TranslationUnitDecl>`（D11），因此**不触发 ExprEngine** |
| **X-03** | solve | 读 `fir/` 下全部文件（单进程）；按 §6.8 求解；写 `summaries/`、`decisions/`、`reports.json`、`ledger.jsonl`、`stats.json`。缺文件 / 坏文件跳过并记日志 〔由 SPEC-0029 修订：本步写 `solved/<tu>.json`，判定日志在 report 阶段写〕 |
| **X-04** | solve 的宿主 | **默认**：驱动生成一个桩 TU（`solve_stub/stub.c` 与只含它的 `compile_commands.json`），用 bxen 跑 `Phase=solve`，checker 在桩 TU 的回调里完成求解——**不需要在 BXPlugin 里新增可执行目标**。**可选**：独立命令 `oobsum-solve`（只链接 LLVM Support 与 `core/`），需要在 BXPlugin 的 CMake 里加一个可执行目标（`07-` 输入 I1） |
| **X-05** | report | 驱动从原 cdb 里**原样复制**出 `decisions/` 里出现的 TU 的条目，组成 `report_cdb/compile_commands.json`，bxen 全量模式跑 `Phase=report`。原样复制保证两个阶段算出的 TU id 一致（同一 bxen、同样的 `directory` / `file`） |
| **X-06** | inline | 默认模式：本 TU 内 export → solve → report 一次做完，不读写 `WorkDir`（设置了 `WorkDir` 时额外写一份 `fir/`，方便对照） |
| **X-07** | 规模预估 | FIR 体积与求解内存按探针的函数数与 CFG 规模估算（`07-` 输入 I2）；若 solve 峰值内存超过约定值，按 SCC 分批加载（只常驻摘要，FIR 用完即释放）`[P1]` |
| **X-08** | 回退 | 若 B 在你们环境里不可行（如 bxen 对单个 TU 有时长上限、桩 TU 跑不完求解），退回方案 A：export 照旧，solve 改成按"被调者都已就绪的 TU"分轮在 bxen 里做（`01-` §3.11）。引擎不变，只换编排；P0 不预先实现 |

**X-10 驱动的其它约定**：每条 bxen 命令先打印再执行，输出存 `logs/<阶段>.log`；任一阶段非零退出码 → 停止并提示单独重跑该阶段的命令；
`status` 子命令能在任何时候说明"到哪一步了、缺什么"。

---

## 8. 非功能需求

| 编号 | 项 | 要求 |
|---|---|---|
| **N-01** | 深度 | 合成直线链 3 / 5 / 10 / 17 / 20 层全部报出；20 层在 `inline` 模式下求解 < 1 秒（不含 bxen 启动与解析） |
| **N-02** | export 耗时 | ≤ 探针全量墙钟时间（R3）× 1.5【推断：在探针的"解析 + 建 CFG"之上多一层降级与写文件】 |
| **N-03** | solve | 单函数中位数 ≤ 2 ms、P99 ≤ 100 ms；全量单线程 ≤ 1 小时；峰值内存 ≤ 64 GB（47 核 / 251 GB 机器）。实测超标先降级 X-07 / 并行 |
| **N-04** | 全量总耗时 / 内存 | 数值由你定（`00-` §7 的空格，`07-` 输入 I10） |
| **N-05** | 健壮性 | 在全量 cdb 上**不得让 bxen 崩溃**；单个函数 / TU 的失败只影响它自己（A-96） |
| **N-06** | 确定性 | 同样的输入，`summaries/`、`reports.json`、告警输出逐字节相同（`stats.json` 里的耗时除外） |
| **N-07** | 可移植 | C++14；只用 LLVM 14 的 Support / ADT / JSON / YAML 与 Clang 的 AST / Analysis / StaticAnalyzer Core；不开 RTTI、不用异常；同一份源码既能在 fork 里随 `Checkers.td` 编进 xir，也能在沙箱里编成插件（`-DOOBSUM_PLUGIN_BUILD`，探针已验证这种双构建【实测数据】）；驱动只用 Python 3.6+ 标准库 |
| **N-08** | 编码规范 | `csa-checker-dev`：命名（checker 类内统一小驼峰、其余函数大驼峰、变量小驼峰）、`.clang-format`、单语句控制流带 `{}`、`BuiltinCheckerRegistration.h`、诊断与 td 全英文、include 不排序；每次交付跑 `check_checker_conventions.py` 无 FAIL |
| **N-09** | 可观测 | `Verbose=1` 每 TU 一行（函数数、导出耗时）；`stats.json`：函数状态分布、Req / Eff 数分布、告警按级别与层数、账本按原因、各阶段耗时 |
| **N-10** | 产物内容 | FIR 与摘要只含名字、类型布局、位置与抽象值，**不含源码文本** |

---

## 9. 代码组织与构建

### 9.1 目录与文件（入口平铺在 `Checkers/`，其余在 `Checkers/OobSummary/`）

| 路径 | 内容 | 依赖 | 估算行数 |
|---|---|---|---|
| `../OobSummaryChecker.cpp` | checker 入口（平铺在 `Checkers/`）：注册、选项、四个 `Phase` 的分派、告警发出（F-30…F-35） | Clang SA | 450 |
| `lower/Keys.{h,cpp}` | D-01…D-08 | Clang AST | 150 |
| `lower/TypeLayout.{h,cpp}` | 类型 → FIR 类型、记录布局、柔性数组识别（D-30…D-39） | Clang AST | 300 |
| `lower/Lowering.{h,cpp}` | A-01…A-09 | Clang AST / CFG | 1400 |
| `core/Fir.{h,cpp}`、`core/FirJson.{h,cpp}` | FIR 结构与 JSON 读写（D-10…D-14） | LLVM Support | 900 |
| `core/LinExpr.{h,cpp}`、`core/Bound.{h,cpp}` | 线性式、不透明项、界、来源标签、化简（A-10、A-17） | LLVM ADT | 900 |
| `core/Value.{h,cpp}`、`core/State.{h,cpp}` | `AbsInt`、`PtrVal`、格、定义式、守卫、合并 / widening（A-10…A-16） | | 1000 |
| `core/Wto.{h,cpp}`、`core/Interp.{h,cpp}` | WTO 与不动点（A-33） | | 450 |
| `core/Transfer.{h,cpp}`、`core/Access.{h,cpp}` | 转移（A-20…A-32）、访问检查与 Req 生成（A-40…A-45） | | 1000 |
| `core/Entail.{h,cpp}` | 蕴含判定（A-50…A-52） | | 300 |
| `core/Summary.{h,cpp}`、`core/SummaryJson.{h,cpp}` | 摘要结构与 JSON（D-20…D-29）、效果生成（A-70、A-71） | | 800 |
| `core/Apply.{h,cpp}` | 调用点五步（A-60…A-66） | | 700 |
| `core/Models.{h,cpp}` | 模型表、`config.ini`、`alloc_free_identity` 读取（I-40…I-44） | LLVM YAML / JSON | 400 |
| `core/Solver.{h,cpp}` | 全局表、调用图、SCC 调度、判定汇总与指纹（A-80…A-90、F-34） | | 600 |
| `core/Dump.{h,cpp}` | 规范文本记法（D-29） | | 300 |
| `config/oob_models.yaml` | 默认模型表 | | 40 |
| `tools/oobsum.py` | 驱动（I-30、X-04、X-05、X-10）与 `sqlite` / `explain` / `check` | Python 3 | 900 |
| **合计** | | | **C++ 约 9 700 行、Python 约 900 行** |

**分层规则**：`core/` 不得包含任何 `clang/` 头文件；`lower/` 只产出 FIR；只有 `OobSummaryChecker.cpp` 同时看到两边。
这条规则让 `core/` 能在沙箱里不经 Clang 单测、也能编进独立的 `oobsum-solve`（X-04 可选项）。

### 9.2 在 fork 里

* 源码 `CMakeLists.txt`：`add_library()` 末尾逐个追加上表的 `.cpp`（以 `OobSummary/` 开头的相对路径）。
* `Checkers.td`：`ipa` 包块末尾追加（选项按 §4.2 的 `CheckerOptions` 写）。
* 用例：`TestCase/OobSummaryChecker/` 由 `cases/run.py testcase` 生成（T-05）。
* 编译命令与 `csa-checker-dev` 相同（`cmake … ../ && make -j16`）。

### 9.3 在沙箱里（我这边每次交付前跑完）

* 插件：`clang++-14 -std=c++14 -fno-rtti -shared -fPIC -DOOBSUM_PLUGIN_BUILD …`，与探针的 `sandbox_build.sh` 同法。
* `core/` 单测：独立可执行 `oobsum-core-tests`（自带最小测试框架，不依赖 gtest）。〔SPEC-0004：源码在 `oobsum/unittests/`，独立 CMake 工程链接本仓库构建的 `LLVMSupport`，由 lit 的 `core-unittests.test` 驱动〕
* 语料：`cases/run.py csa`（驱动沙箱里的 `clang -cc1 -load` 走完 export → solve → report，对照 `@expect`）。〔由 SPEC-0002 修订：目前用本仓库构建的 clang 跑 `Phase=inline`，三阶段接上后加 `--phase`〕
* 规范检查：`check_checker_conventions.py` 跑在一个模拟的 BXPlugin 目录上（与探针同法，无 FAIL）。

---

## 10. 测试规格

| 编号 | 层级 | 内容 | 在哪跑 | 何时 |
|---|---|---|---|---|
| **T-01** | 单元 | 线性式 / 界 / 区间运算、合并与 widening、蕴含判定、代换；手写 FIR 上的转移与摘要 | 沙箱 | 每次交付 |
| **T-02** | 降级快照 | 10 个代表用例的 `Dump=fir` 文本快照 | 沙箱 | 降级改动时 |
| **T-03** | 语料验收 | `cases/` 全部 134 个入口按 `@expect` 对照（下述口径） | 沙箱 + fork | 每次交付；检查点 |
| **T-04** | 链长 | `cases/gen_chain.py` 生成 4 类模板（形参透传、偏移累加、封装分配器链、跨 TU 链）× N ∈ {2, 3, 5, 10, 17, 20} | 沙箱 + fork | G1 起 |
| **T-05** | fork 用例 | `TestCase/OobSummaryChecker/`（由 `cases/` 生成）跑驱动，告警数与 `EXPECTED.md` 一致 | fork | 每次集成 |
| **T-06** | 摘要快照 | **金标准是 `cases/summaries/` 的 104 份 JSON 去掉 `doc` 后的机器版**（16 个用例，v3.13 手写、v3.14 划清 `doc`，经 `replay.py` 逐调用点核对与"从局部事实推到底"两种方式验证）：引擎在这些函数上的输出按规范形式逐字段比对，最终告警与 `replay.py --alerts` 逐条比对（含消息与指纹）；其余用例的摘要在 WP4 起由引擎产出、与用例头"摘要"块对照后冻结。改动须有意更新 | 沙箱 | 每次交付 |
| **T-07** | 健壮性 | 本包 `05-experiments/`、`07-capability-boundary/corpus/`、`cases/` 全部跑 export + solve 不崩溃、全函数性成立 | 沙箱 | 每次交付 |
| **T-08** | 真实代码 | 子集（几个顶层目录）→ 全量：告警按级别 / 层数、账本按原因、耗时 / 内存；抽样 50 条 L1 由你研判精确率 | fork | W10 起 |

**T-03 的对照口径**（`oobsum.py check` 与 `run.py csa` 共用同一实现）：〔由 SPEC-0002 §2.3 修订：D16 后 SUSPECT 计为召回，`记账` 的 TP 要报出 SUSPECT，`-` / `记账` 的入口只禁止 DEFINITE / POSSIBLE；下表是原写法〕

| `@expect` 的 `pa` | 判为通过的条件 |
|---|---|
| `L1` / `L2` | 恰有一条告警的**汇合点函数是该入口**，且级别相同（L2 的用例报成 L1 也算不通过，单独列出） |
| `-` | 没有任何汇合点为该入口的告警 |
| `记账` | 同 `-`；另外统计账本里是否有该链上的记录（P0 只统计不判失败） |

**T-09 检查点的验收集**：

| 检查点 | 必须全部通过 |
|---|---|
| **G1** | Lv1 + Lv2 的 17 个用例（34 个入口，含跨 TU 的 LV1-07）；T-04 的模板 1、2、4 在 N = 3…20 全报、N = 2 不报 |
| **G2** | 32 个承诺用例的 TP 级别正确；**全部 134 个入口里期望为 `-` / `记账` 的零告警**〔SPEC-0002 §7 提议改为"零 DEFINITE / POSSIBLE，SUSPECT 单列"，待用户确认〕；尽力项 9 个给出报出率；T-04 全部模板 |
| **G3** | 全量 cdb 跑通（N-05）；N-02、N-03 达标或有降级方案；T-08 的指标达到你定的数值 |

---

## 11. 追溯矩阵

| P0 条目（`00-` §4） | 需求 | 模块 | 主要用例 / 测试 | 检查点 |
|---|---|---|---|---|
| P0-1 多层传播主干 | F-01、F-02、F-10…F-16、F-20…F-22、A-10…A-17、A-20…A-33、A-40…A-52、A-60…A-66 | `core/*` | Lv1、Lv2、Lv3 全部；T-01、T-04 | G1、G2 |
| P0-2 跨 TU 执行 | X-01…X-10、D-01…D-08 | `lower/Keys`、`core/Solver`、`core/FirJson`、`tools/oobsum.py` | LV1-07；T-04 模板 4 | G1 |
| P0-3 循环 | F-07、A-16、A-33 | `core/Interp`、`core/Wto` | LV1-03、LV1-04、LV2-01、LV2-06、LV3-04、LV3-05、LV3-08、LV3-09 | G2 |
| P0-4 ExtentModel 高频形态 | D-30…D-39、F-08 | `lower/TypeLayout`、`core/Value` | LV1-04、LV1-06、LV3-01、LV3-02、LV6-01（尽力） | G2 |
| P0-5 分配器 | I-40…I-44、D-33、A-63、A-65 | `core/Models`、`core/Apply` | LV1-03、LV2-04、LV4-01…LV4-04、LV5-06 | G2 |
| P0-6 安全函数族 | F-03…F-05 | `core/Models`、`core/Access` | LV1-05、LV1-08、LV2-02、LV2-03、LV3-07、LV5-01 | G1（memcpy_s / memset_s）、G2 |
| P0-7 告警输出 | F-30…F-35、F-40…F-42、A-90、A-91 | `OobSummaryChecker.cpp`、`core/Solver` | T-05 | G1 |
| P0-8 验收语料与报告 | T-01…T-09 | `cases/`、`run.py`、`gen_chain.py` | 全部 | 各检查点 |

---

## 12. 待确认的决策（点头即定稿）

| # | 决策 | 推荐 | 备选 | 不选推荐的影响 |
|---|---|---|---|---|
| **D5′** | 跨 TU 编排 | **B**：export → solve → report（§7） | A：多轮扫描（`01-` §3.11） | A 要多做每轮导入 / 合并 / 去重；轮数取决于探针统计，可能把全量耗时放大数倍 |
| **D7** | 包与名字 | `rb.enhance.ipa.OobSummary` / `OobSummaryChecker` | `rb.enhance.core.OobSummary` | 只影响 `Checkers.td` 的位置与 `-checker=` 字符串 |
| **D9** | 引擎跑在 FIR 上 | **是** | 直接跑 `clang::CFG` | 否的话 B 不成立（求解要 AST 在内存里），只能走 A；单测要带 Clang |
| **D10** | 落盘格式 | JSON + Python 侧 SQLite | C++ 侧 SQLite / FlatBuffers（`01-` §3.8） | 要给 xir 加第三方依赖；多进程写同一个库要处理锁 |
| **D11** | 宿主回调 | 只挂 `ASTDecl<TranslationUnitDecl>` | `ASTCodeBody` + `EndOfTranslationUnit` | 后者会让每个函数跑 ExprEngine【实测数据】 |
| **D12** | 告警位置与 trace | 汇合点调用表达式 + 文本 trace | 报在访问点 | 访问点常在别的 TU 里，CSA 的告警位置必须在本 TU 可见文件内 |
| **D13** | 键 | 名字 / 定义文件 + 名字 | Clang USR | USR 要链接 `clangIndex`（fork 里是否可用待确认），对 C 没有额外好处 |
| **X-04** | solve 宿主 | 桩 TU（不改 BXPlugin 构建） | 独立可执行 `oobsum-solve` | 后者更干净，但要在 BXPlugin 的 CMake 里加一个可执行目标 |
| **D14** | 摘要里的说明文字（v3.14） | 放在键名为 `doc` 的字段里，引擎不读；默认不写（`Emit=machine`） | 不要说明 / 说明与机器字段混写 | 不要说明调试时得对着 FIR 猜；混写则无法保证引擎不依赖自然语言 |
| **D15** | L2 消息里界的出处函数（v3.14） | P0 只写界的种类（`a loop condition`），出处靠调用链 note | P1 给 Req 加 `bound_at`（界在哪个函数 / 哪一行给出），沿上传保留 | 多一个随代换维护的字段；收益是消息里能直接写 "in Ue_InitTbl" |

---

## 13. 本文件对 `01-` / `05-` 的修订（定稿后同步回去）

| 原文 | 修订 | 依据 |
|---|---|---|
| `01-` §3.11：跨 TU 走方案 A（多轮），B 放 2027 | P0 走方案 B；A 为回退 | D5′、D9 |
| `01-` §3.8：SummaryDB 用 SQLite，序列化建议 FlatBuffers / Protobuf | JSON 落盘（`llvm::json`），SQLite 由驱动生成 | D10 |
| `01-` §3.2：函数与全局用 USR 做键 | 自定义键（D-01…D-08） | D13 |
| `01-` §2 / `00-` §3：checker 挂 `check::ASTCodeBody` | 挂 `check::ASTDecl<TranslationUnitDecl>` | D11 |
| `01-` §3.3：`BoundExpr` 的 min/max 变体与守卫二选一 | 两者都要：界带 `min/max(c, LE)`（循环 narrowing 需要），条件另带守卫 | A-10、A-14、A-17 |
| `01-` §3.5：`SafetyCondition` 以元素为单位 | 统一按**字节**（安全函数、强转、结构体覆盖都是字节语义） | D-21…D-23 |
| `05-` §2.2：落盘的 SQL 表 | 不变，改为驱动生成的查询库（§4.6） | D10 |
