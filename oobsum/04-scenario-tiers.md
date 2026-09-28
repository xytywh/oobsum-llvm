# 多层调用内存越界：场景分级、年内承诺边界与 AI + 程序分析分工

> 调研包 v3.10 · 2026-09-28 · 依据：【立项输入】（使用方与技术专家的讨论结论、正交维度与能力边界清单）+ 本包已有实测 + 【推断】
>
> 回答四个问题：
> 1. 今年（12-15 前）优先支持哪些场景；
> 2. 各类场景的实现顺序和优先级；
> 3. 程序分析方案最终计划覆盖到什么复杂度；
> 4. 哪些复杂场景不由传统程序分析完整解决，而交给 AI + 程序分析。
>
> 与 `00-implementation-plan.md` 的关系：**本文件定"做哪些场景、承诺到什么程度"，计划文件定"什么时候做完"。**
> 计划已按本文件同步（v3.10）；两者若有出入，场景边界以本文件为准。

---

## 0. 结论先行

**十个级别，每升一级只多要一种分析能力。** 年内承诺前四级加第五级的第一个场景；
程序分析最终做到第七级，第八、九级做一部分；第十级和几类"缺事实"的场景交给 AI + 程序分析。

| 级别 | 一句话 | 这一级新增的分析能力 | 年内（12-15） | 最终归属 |
|---|---|---|---|---|
| **Lv1** | 一侧跨层、原样传递，另一侧在访问点就是常量 | 形参代换；常量界；数组 / 常量长度堆 / 全局数组的大小 | **承诺** | 程序分析 |
| **Lv2** | 两侧沿同一条链下传，带线性变换、指针偏移、标量强转、掩码 / 取模 | 线性表达式；带偏移的基址；按字节归一 | **承诺** | 程序分析 |
| **Lv3** | 链上有校验、分支、典型循环 | 按条件收窄取值范围；合并；阈值 widening + narrowing；**条件带守卫**；**访问点先符号化简** | **承诺**（2 项尽力） | 程序分析 |
| **Lv4** | 内存来自下层：多层封装的分配器、出参、getter 返回的长度 | 返回值 / 出参效应；返回值取值范围 | **承诺** | 程序分析 |
| **Lv5** | 内存或长度装在结构体字段里跨层；两侧沿不同链汇合 | 字段级基址；帧内字段存储；写效应摘要 | S5.1 **承诺**，2 项尽力，其余 2027H1 | 程序分析 |
| **Lv6** | 全局状态：跨文件全局数组、全局长度变量、运行期申请的全局资源 | 全局大小表；全局取值集合；全局不变式 | 1 项尽力，其余 2027 | 程序分析；运行期配置类 AI + 程序分析 |
| **Lv7** | 内存建模精度：柔性数组、多维行级、字节缓冲按结构体解释、指针步进、字符串 | 分配大小减偏移；逐维界；字节偏移；字符串长度 | ——（P1 余量） | 2027 程序分析；变量乘积类不承诺 |
| **Lv8** | 复杂循环：数据驱动步长的解析循环、非典型循环、多变量非线性下标 | 关系型不变式 | —— | 部分 AI + 程序分析；其余不承诺 |
| **Lv9** | 断链：函数指针、外部 / 未定义函数、递归 | 调用边解析；模型表；强连通分量不动点 | 1 项尽力 | 静态可解析的归程序分析；运行期注册与外部模型归 AI + 程序分析 |
| **Lv10** | 业务语义与隐式契约 | 规格推断 | —— | AI + 程序分析 |

**四个问题的回答：**

1. **年内优先**：Lv1–Lv4 全部 + Lv5 的 S5.1，共 **26 个承诺场景**（§5.1）。
   承诺的含义：合成语料里这一类的 TP 全部报出、TN 零误报，并在 3 / 5 / 10 / 17 / 20 层链长上都成立。
   另有 6 个尽力场景（S3.3、S3.8、S5.3、S5.5、S6.1、S9.1），报出率统计但不设硬门槛。
2. **实现顺序**：Lv1 → Lv2 → 跨文件执行 → Lv3 → Lv4 → S5.1 与安全函数族收尾 → 真实代码（§5.2，对应计划 W04–W11）。
   每一级都是下一级的前置条件，所以顺序就是级别顺序；Lv3 排在 Lv4 前面，是因为真实代码几乎处处有校验，
   没有 Lv3 就上真实代码会误报成片。
3. **程序分析的覆盖上限**是四个条件同时成立的场景（§6）：
   ① 大小、访问量、校验都来自被分析的代码或已建模的函数；② 取值能用线性表达式和区间表达；
   ③ 链上的调用边能静态确定；④ 循环的影响能用区间 widening 概括。
   按级别说：Lv1–Lv5 全部，Lv6、Lv7 的大部分，Lv8 的一项尽力，Lv9 的三项。
4. **AI + 程序分析**：Lv10 全部，以及 Lv9 的运行期注册回调与外部函数行为、Lv8 的数据驱动解析循环、
   Lv6 的运行期全局不变式与配置值、所有"证明不了安全但来源不可信"的候选研判（§7）。
   分工是 **AI 补事实，程序分析做验证和多层传播**——在这些场景里摘要引擎仍然是主干，AI 提供的是它缺的那几条事实。

**实测校准**：§3 的 36 段最小代码都用 Infer 当标尺跑过（§3 末尾）。26 个承诺场景里 Infer 报出 15 个，
级别与本文的期望逐个一致；它漏掉的 10 个正好是安全函数、结构体成员、全局数组、掩码、带守卫的校验——本设计比 Infer 多做的部分。【实测数据】

---

## 1. 口径

### 1.1 基本要求

- **内存信息或访问信息至少有一侧跨 ≥ 3 层**（即超过现有规则的 inline 深度，默认 2）。两侧都在 2 层以内的是 **Lv0**，归现有规则。
  例外：CSA 已知盲区（`01-` §3.1 #18 行级、#21 指向数组中间的指针加符号下标，T0-1b 在 fork 上实测）1–2 层也报。
- **层数本身不算复杂度**。摘要方式下每个函数只分析一次，层与层之间是相加（Infer 实测 200 层 0.76 秒【实测数据】）；
  复杂度来自"链上每一层做了什么"。所以每个承诺场景都在 **3 / 5 / 10 / 17 / 20 层**上验收，层数不作为分级维度。

### 1.2 越界判定式与"两侧"

所有场景都换算成字节后归一到一个式子：

```text
0 ≤ off   且   off + size ≤ extent(base)
```

- **内存侧**：`base` 以及它有多大（`extent`）。
- **访问侧**：`off`（下标 × 元素大小、指针偏移）与 `size`（一次访问的字节数、拷贝长度）。
- **安全函数** `memcpy_s(dst, destMax, src, count)`：越界条件是 `destMax > extent(dst)` 或 `count > extent(src)`。
  `count > destMax` 会被运行时拦下，但出错路径上安全函数库会按 `destMax` 清零目的缓冲区（`ERANGE_AND_RESET`），
  所以 **`destMax` 报大了本身就是越界写风险**【推断，以你们使用的安全函数库源码为准】。

**汇合点**：两侧都变成已知、或者能相互比较的那一层函数。摘要做的事，就是把条件从访问点一层层搬到汇合点再判定。

### 1.3 分级与承诺等级

告警级别沿用 `01-` §3.6：`L1` 访问范围完全越界（确定），`L2` 部分重叠（可能），`S2` 含未知符号（默认不报），`StillOpen` 继续上传。
补一条口径：**取值完全没有约束的外部输入（消息字段、入口函数形参）停在 `S2` / `StillOpen`，不报 `L2`**；
`L2` 只来自代码里给出的有限上界（校验、掩码、分支常量、循环界）。否则每个外部长度都会变成一条告警。
这类"证明不了、来源又不可信"的，统一进 S10.4 的候选研判。

| 承诺等级 | 含义 | 验收 |
|---|---|---|
| **承诺** | 这一类的都要报；漏了是缺陷 | 合成语料 TP 100% 报出（L1 或 L2，按上面的定义），TN 0 误报；链长 3 / 5 / 10 / 17 / 20 |
| **尽力** | 常见写法能报，漏报记为已知限制 | 统计报出率，不设门槛 |
| **不承诺** | 程序分析不单独解决 | —— |
| **AI + 程序分析** | AI 补事实，程序分析验证与传播 | 另立精确率 / 召回率指标，不计入 12-15 验收 |

循环类越界按区间分级多数是 `L2`（访问范围 `[0, n]` 与大小 `n` 部分重叠；Infer 的口径也是这样），这是分级口径，不是漏报。

---

## 2. 维度：在你们给的基础上调整

### 2.1 调整了什么、为什么

| 原来的维度 | 调整 | 理由 |
|---|---|---|
| 越界类型：下标 / 指针偏移 / 内存拷贝 | 加 **字符串操作**（`strcpy_s` / `strcat_s` / `sprintf_s`） | 依赖源串长度，要另一种抽象（字符串长度），和 `memcpy_s` 不在一档 |
| 内存来源：静态数组 / 动态申请 / 柔性数组 / 外层传入 | 拆成 **内存形态** 和 **内存信息的传递路径** 两个维度 | "由外层调用传入"是路径不是形态，和"动态申请"可以同时成立；而路径（往下传 / 往上传 / 经字段 / 经全局 / 断链）才是摘要难度的主要来源 |
| 访问长度来源：本函数 / 外层传入 / 多层传递 | 和内存侧用同一套路径编码，补 **返回值或出参上传、经结构体字段、全局、外部输入（无约束）** | 两侧对称，才能描述"两侧分别从哪来"的组合 |
| 复杂因素"内存大小和访问长度分别沿不同链" | 提成独立维度 **两侧关系**：仅一侧跨层 / 同链 / 异链汇合 | 同链只需要把条件往上传；异链还要把返回值、出参、字段写入的效果也往上传——这是能力的分水岭 |
| 复杂因素"是否经过多层调用" | **去掉**，改为所有场景的基本要求 | 层数不是复杂度来源（§1.1） |
| 复杂因素"类型转换" | 细分：**标量指针强转**（`char*` ↔ `UINT32*`）/ **字节缓冲按结构体解释** | 前者只是乘除一个常量，Lv2 就能做；后者要字节级的内存建模，Lv7 |
| 复杂因素"指针 / 内存偏移" | 细分：**直线代码中的偏移** / **循环中的步进** | 前者是线性累加，Lv2；后者要"偏移量 = 循环次数 × 步长"这种关系，Lv7–Lv8 |
| 复杂因素"别名或中间变量" | 细分：**函数内中间变量** / **经结构体字段中转** / **真别名**（二级指针改写、两条路径指向同一块内存） | 函数内中间变量对流敏感分析是免费的（Lv1 就有）；字段中转是 Lv5；真别名要指向分析，不在本期 |
| 能力边界 (3)"移位 / 乘 / 除 / 取模 / 位运算无法精确求解" | 细分：**常量操作数** / **变量操作数** | `x & 0x3F`、无符号的 `x % 64` 自带上界，区间运算是精确的；`x >> 2`、`x / 4` 在 `x` 有界时也精确。真正失精度的只有两个变量相乘、按变量移位 |
| （新增） | **校验形态**：无 / 同函数 / 外层 / 封装在独立函数 / 校验了错字段 | 真实代码几乎都有校验，它决定误报；而"校验了错字段"正是隐式契约类的本质 |

### 2.2 维度编码（用于每个场景的"签名"）

| 维度 | 取值 |
|---|---|
| **A** 访问形式 | A1 下标 · A2 指针偏移后解引用 · A3 `memcpy_s` / `memmove_s` / `memset_s` · A4 字符串类安全函数 |
| **M** 内存形态 | M1 定长数组（局部 / static）· M2 全局定长数组 · M3 结构体数组成员 · M4 堆·常量长度 · M5 堆·变量长度 · M6 柔性数组成员 · M7 多维数组 · M8 分析范围外（入口形参、外部接口传入） |
| **P** 内存信息路径 | P0 本函数 · P1 形参逐层下传 · P2 返回值 / 出参上传 · P3 经结构体字段中转 · P4 经全局变量 · P5 断链（函数指针 / 未定义函数） |
| **V** 访问信息来源与路径 | V0 访问函数内的常量或常量约束 · V1 形参逐层下传 · V2 返回值 / 出参上传 · V3 经结构体字段 · V4 全局变量 · V5 外部输入（无约束） |
| **R** 两侧关系 | R1 仅一侧跨层 · R2 两侧同链下传 · R3 两侧异链汇合 |
| **X** 值变换 | X0 原样 · X1 线性（± 常量、× 常量含 `sizeof`）· X2 标量指针强转 · X3 常量操作数的非线性（`&`、`%`、`>>`、`/`）· X4 字节缓冲按结构体解释 · X5 变量操作数的非线性 |
| **K** 控制 | K0 直线 · K1 分支合并 · K2 典型循环 · K3 循环中累积偏移 / 拷贝 · K4 非典型或多变量嵌套循环 · K5 递归 |
| **G** 校验 | G0 无校验（访问量是常量）· G1 同函数校验 · G2 外层校验 · G3 校验封装在独立函数里 · G4 校验了错字段 / 错规格 |

**你们列的 7 条能力边界落在哪里：**

| 能力边界 | 编码 | 级别 |
|---|---|---|
| (1) 间接调用断链 | P5 | Lv9（S9.1–S9.3） |
| (2) 全局变量 | M2、P4、V4 | Lv1（S1.6 定义可见的全局数组）、Lv6 |
| (3) 约束求解精度 | V5、X3、X5 | X3 在 Lv2（S2.7，其实能精确）；X5 在 Lv7–Lv8；V5 在 Lv10（S10.4） |
| (4) 未定义函数 | P5、M8 | Lv9（S9.4） |
| (5) 业务逻辑 / 隐式契约 | G4 | Lv10 |
| (6) 内存建模精度 | M6、M7、X4、A4、循环步进 | Lv7 |
| (7) 循环 | K2–K4 | 典型的在 Lv3，复杂的在 Lv8（对照表见 Lv8 一节末尾） |

---

## 3. 场景分级（从简单到复杂）

代码约定：`L1 → L2 → L3` 是一条三层的直接调用链，`L1`、`L2` 原样转发，验收时中间层加到 20 层；
`S…` 是链顶（汇合点）。所有片段共用下面这组声明，并已用 clang 逐段做过语法检查（`-fsyntax-only -Wall`）：

```c
#include <stddef.h>
typedef unsigned int UINT32;
typedef int errno_t;
#define EOK 0
#define PID 0x100
/* 原型按 01- §3.7.1 的假设：第 3 个形参是字节数（下标待确认） */
void *VOS_MemAlloc(UINT32 pid, UINT32 ptNo, UINT32 size);
errno_t memcpy_s(void *dest, size_t destMax, const void *src, size_t count);
errno_t memset_s(void *dest, size_t destMax, int c, size_t count);
```

每个场景标注：**签名**（§2.2 编码）、**摘要长什么样**、**期望级别**、**年内**。

---

### Lv1 一侧跨层 · 原样传递 · 直接调用

这就是你们说的"最简单的场景"：只要沿调用链把内存的初始大小（或把访问点的常量）带到汇合点，不涉及路径爆炸。

**新增能力**：形参代换；常量界；数组、常量长度堆、全局数组的大小；层数过滤；逐层 trace。
**摘要形态**：`L3(p): requires extent(p) ≥ 9`；`L3(i): requires i ≤ 7`。

**S1.1 定长数组逐层下传，访问点常量下标越界** —— A1·M1·P1·V0·R1·X0·K0·G0 —— L1 —— **承诺**

```c
static void L3(char *p) { p[8] = 0; }        /* requires extent(p) >= 9 */
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S1_1(void) { char buf[8]; L1(buf); }    /* 8 < 9 -> L1，trace: S1_1 -> L1 -> L2 -> L3 */
```

**S1.2 同上，访问点是常量界的循环** —— A1·M1·P1·V0·R1·X0·K2·G0 —— L2 —— **承诺**

```c
static void L3(char *p) { for (int i = 0; i <= 8; i++) p[i] = 0; }   /* 访问范围 [0,8] */
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S1_2(void) { char buf[8]; L1(buf); }                            /* [0,8] 与 8 部分重叠 -> L2 */
```

**S1.3 常量长度的堆内存逐层下传（链顶直接调已配置的底层分配器）** —— A1·M4·P1·V0·R1 —— L1 —— **承诺**

```c
static void L3(char *p) { p[8] = 0; }
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S1_3(void) { char *p = VOS_MemAlloc(PID, 0, 8); if (p == NULL) return; L1(p); }
```

依赖 `config.ini` 里底层分配器的字节数下标（`MEM_ALLOC_SIZE`，`01-` §3.7.4）。

**S1.4 结构体数组成员的地址逐层下传** —— A1·M3·P1·V0·R1 —— L1 —— **承诺**

```c
typedef struct { UINT32 id; char name[8]; } NODE_S;
static void L3(char *p) { p[8] = 0; }
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S1_4(NODE_S *node) { L1(node->name); }   /* extent = sizeof(name) = 8：按成员（子对象）算，与现有规则口径一致 */
```

**S1.5 下标在链顶是常量，逐层下传，内存是访问函数自己的数组** —— A1·M1·P0·V1·R1 —— L1 —— **承诺**

```c
static void L3(UINT32 i) { char tbl[8]; tbl[i] = 0; (void)tbl; }   /* requires i <= 7 */
static void L2(UINT32 i) { L3(i); }
static void L1(UINT32 i) { L2(i); }
void S1_5(void) { L1(8); }
```

**S1.6 全局定长数组（本文件定义或带长度的 extern 声明），下标逐层下传** —— A1·M2·P0·V1·R1 —— L1 —— **承诺**

```c
static char g_tbl[8];
static void L3(UINT32 i) { g_tbl[i] = 0; }   /* requires i <= 7 */
static void L2(UINT32 i) { L3(i); }
static void L1(UINT32 i) { L2(i); }
void S1_6(void) { L1(8); }
```

**S1.7 `memcpy_s` 的目的缓冲区逐层下传，`destMax` 是访问函数里的常量且报大了** —— A3·M1·P1·V0·R1 —— L1 —— **承诺**

```c
static void L3(char *d, const char *s) { (void)memcpy_s(d, 64, s, 32); }   /* requires extent(d) >= 64 */
static void L2(char *d, const char *s) { L3(d, s); }
static void L1(char *d, const char *s) { L2(d, s); }
void S1_7(const char *s) { char buf[16]; L1(buf, s); }                       /* 16 < 64 -> L1 */
```

---

### Lv2 两侧同链 · 线性变换 · 指针偏移 · 标量强转

**新增能力**：线性表达式（`a·x + b`，系数是常量）；带偏移的基址（`base + off`）；按字节归一（元素大小换算）；常量操作数的区间运算。
**摘要形态**：`L3(p, n): requires extent(p) ≥ n + 1`；中间层加偏移后变成 `requires extent(p) ≥ n + 4`。

**S2.1 缓冲区与长度沿同一条链下传（长度在链顶是常量）** —— A3·M1·P1·V1·R2·X0 —— L1 —— **承诺**

```c
static void L3(char *d, UINT32 n, const char *s) { (void)memcpy_s(d, n, s, n); }   /* requires extent(d) >= n */
static void L2(char *d, UINT32 n, const char *s) { L3(d, n, s); }
static void L1(char *d, UINT32 n, const char *s) { L2(d, n, s); }
void S2_1(const char *s) { char buf[16]; L1(buf, 32, s); }
```

**S2.2 长度经线性变换（加头长、乘 `sizeof`、减一）** —— A1·M1·P1·V1·R2·X1 —— L1 —— **承诺**

```c
static void L3(UINT32 *p, UINT32 cnt) { p[cnt] = 0; }    /* requires extent(p) >= 4*cnt + 4 */
static void L2(UINT32 *p, UINT32 cnt) { L3(p, cnt); }
static void L1(UINT32 *p, UINT32 cnt) { L2(p, cnt); }
void S2_2(void) { UINT32 arr[8]; L1(arr, 8); }           /* 常见的差一：把"个数"当成了最后一个下标 */
```

**S2.3 指针偏移后下传（`buf + 4`、`&buf[k]`、直线代码中多次偏移）** —— A2·M1·P1·V1·R2·X1 —— L1 —— **承诺**

```c
static void L3(char *p, UINT32 n) { p[n - 1] = 0; }       /* requires extent(p) >= n */
static void L2(char *p, UINT32 n) { L3(p + 4, n); }       /* 偏移在中间层：requires extent(p) >= n + 4 */
static void L1(char *p, UINT32 n) { L2(p, n); }
void S2_3(void) { char buf[16]; L1(buf, 16); }            /* 16 < 20 -> L1 */
```

"指向数组中间的指针 + 符号下标"是 CSA 已知盲区（#21），这一类 1–2 层也报（`csaBlindSpot`）。

**S2.4 `memcpy_s(dst + off, 整体大小, …)`：`destMax` 没有扣掉偏移** —— A3·M1·P1·V1·R2·X1 —— L1 —— **承诺**

```c
static void L3(char *b, UINT32 off, const char *s, UINT32 n) { (void)memcpy_s(b + off, 64, s, n); }
                                                          /* requires extent(b) >= off + 64 */
static void L2(char *b, UINT32 off, const char *s, UINT32 n) { L3(b, off, s, n); }
static void L1(char *b, UINT32 off, const char *s, UINT32 n) { L2(b, off, s, n); }
void S2_4(const char *s) { char buf[64]; L1(buf, 8, s, 8); }   /* 64 < 72 -> L1；正确写法是 64 - off */
```

真实代码里最常见的安全函数误用之一。

**S2.5 变量长度的堆内存，申请与访问由同一个量派生** —— A1·M5·P1·V1·R2·X1 —— L1 —— **承诺**

```c
static void L3(char *p, UINT32 n) { p[n] = 0; }          /* requires extent(p) >= n + 1 */
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S2_5(UINT32 n) { char *p = VOS_MemAlloc(PID, 0, n); if (p == NULL) return; L1(p, n); }
/* extent(p) = n，要求 n >= n + 1：两边差一个常量 -1，对任何 n 都不成立 -> L1。不需要知道 n 是多少 */
```

**S2.6 标量指针强转（`char*` → `UINT32*`），元素单位换算** —— A1·M1·P1·V1·R2·X2 —— L1 —— **承诺**

```c
static void L3(char *raw, UINT32 k) { UINT32 *w = (UINT32 *)raw; w[k] = 0; }   /* requires extent(raw) >= 4k + 4 */
static void L2(char *raw, UINT32 k) { L3(raw, k); }
static void L1(char *raw, UINT32 k) { L2(raw, k); }
void S2_6(void) { char buf[16]; L1(buf, 4); }                                   /* 16 < 20 -> L1 */
```

**S2.7 常量操作数的"非线性"：掩码、无符号取模、有界量的移位与除法** —— A1·M1·P1·V1·R2·X3 —— L2 / 不报 —— **承诺**

```c
static void L3(char *p, UINT32 id) { p[id & 0x3F] = 0; }   /* id & 0x3F 取值 [0,63] -> requires extent(p) >= 64 */
static void L2(char *p, UINT32 id) { L3(p, id); }
static void L1(char *p, UINT32 id) { L2(p, id); }
void S2_7_TN(UINT32 id) { char buf[64]; L1(buf, id); }     /* 可证明安全：不报 */
void S2_7_TP(UINT32 id) { char buf[32]; L1(buf, id); }     /* [0,63] 与 32 部分重叠 -> L2 */
```

这一条是对能力边界 (3) 的修正：`&` 掩码、对无符号数取模自带上界；`>>`、`/` 常量在被操作数有界时也精确。
真正失精度的是 X5（两个变量相乘、按变量移位），在 Lv7（S7.6）。

---

### Lv3 控制流：校验、分支、典型循环

**新增能力**：

- 按分支条件收窄取值范围（`if (len > MAX) return;` 之后 `len ∈ [0, MAX]`），分支合并；
- 循环：WTO + 阈值 widening + narrowing，**narrowing 要支持符号界**（`i < n` 收窄出 `[0, n−1]`）；
- **条件带守卫**：`L3` 里先校验 `i < 32` 再访问，条件要写成 `i < 32 ⇒ 4i + 4 ≤ extent(p)`（或 Infer 式的 `min(i, 31)` 界），
  不能只留一个数值区间——否则链顶传常量 `3` 的调用点也会误报（见 S3.2 的 TN）；
- **访问点先符号化简、再代入区间**：`memcpy_s(b + off, total − off, …)` 两边的 `off` 先抵消，
  得到 `total ≤ extent(b)`，与 `off` 的循环区间无关（见 S3.7）。

后两条是这次分级时发现的设计缺口，已补进 `01-` §3.3 / §3.6（§9）。

**S3.1 外层（跨函数）校验后下传** —— A3·M1·P1·V1·R2·K1·G2 —— TN 不报 / TP L2 —— **承诺**

```c
static void L3(char *d, UINT32 n, const char *s) { (void)memcpy_s(d, n, s, n); }
static void L2(char *d, UINT32 n, const char *s) { L3(d, n, s); }
static void L1(char *d, UINT32 n, const char *s) { L2(d, n, s); }
void S3_1_TN(const char *s, UINT32 len) { char buf[16]; if (len > sizeof(buf)) return; L1(buf, len, s); }
void S3_1_TP(const char *s, UINT32 len) { char buf[16]; if (len > sizeof(buf) + 1) return; L1(buf, len, s); }
/* TN：len 取值 [0,16]，安全；TP：校验放行到 17 -> L2 */
```

**S3.2 访问函数自己有校验，但校验界大于多层之上决定的实际大小** —— A1·M1·P1·V1·R2·G1 —— TP L2 / TN 不报 —— **承诺**

```c
#define MAX_NUM 32
static void L3(UINT32 *p, UINT32 i) { if (i >= MAX_NUM) return; p[i] = 0; }   /* i < 32 => 4i + 4 <= extent(p) */
static void L2(UINT32 *p, UINT32 i) { L3(p, i); }
static void L1(UINT32 *p, UINT32 i) { L2(p, i); }
void S3_2_TP(UINT32 i) { UINT32 tbl[16]; L1(tbl, i); }   /* 校验按 32 放行，实际只有 16 个 -> L2 */
void S3_2_TN(void)     { UINT32 tbl[16]; L1(tbl, 3); }   /* 3 < 32 且 16 <= 64：不报。只留数值区间 [0,31] 会在这里误报 */
```

"校验界与实际大小不一致"是真实缺陷里的高发形态，也是多层场景的典型：数组定义和校验常量隔着好几层。

**S3.3 分支给出不同长度后合并（其中一支越界）** —— A3·M1·P1·V1·R2·K1 —— L2 —— **尽力**

```c
static void L3(char *d, UINT32 n, const char *s) { (void)memcpy_s(d, n, s, n); }
static void L2(char *d, UINT32 n, const char *s) { L3(d, n, s); }
static void L1(char *d, UINT32 n, const char *s) { L2(d, n, s); }
void S3_3(const char *s, int big) { char buf[16]; UINT32 n = big ? 32 : 8; L1(buf, n, s); }   /* n 取值 [8,32] -> L2 */
```

区间合并只能给 L2；要给 L1 需要少量路径分离（保留两个分支各自的值），年内不做。

**S3.4 典型循环，循环界来自多层之上** —— A1·M1·P1·V1·R2·K2 —— L2 —— **承诺**

```c
static void L3(char *p, UINT32 n) { for (UINT32 i = 0; i < n; i++) p[i] = 0; }   /* requires extent(p) >= n */
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S3_4(void) { char buf[16]; L1(buf, 17); }
```

**S3.5 循环退出后使用计数器（填完再补结尾）** —— A1·M1·P1·V1·R2·K2 —— L1 —— **承诺**

```c
static void L3(char *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) p[i] = 'a'; p[i] = 0; }
                                                        /* 退出时 i == n -> requires extent(p) >= n + 1 */
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S3_5(void) { char buf[16]; L1(buf, 16); }
```

**S3.6 规则的嵌套循环（各维的界是常量）** —— A1·M1·P1·V0·R1·K2 —— L2 —— **承诺**

```c
#define ROWS 4
#define COLS 8
static void L3(char *m) { for (int r = 0; r <= ROWS; r++) for (int c = 0; c < COLS; c++) m[r * COLS + c] = 0; }
static void L2(char *m) { L3(m); }
static void L1(char *m) { L2(m); }
void S3_6(void) { char mat[ROWS * COLS]; L1(mat); }   /* r*COLS + c 取值 [0,39]，大小 32 -> L2 */
```

`r * COLS` 里 `COLS` 是常量，区间乘法是精确的；`COLS` 换成变量就变成 S7.6。

**S3.7 循环内按"剩余长度"拷贝（安全写法）** —— A3·M1·P1·V1·R2·K3 —— 不报 —— **承诺（TN）**

```c
static void L3(char *b, UINT32 total, const char *s, UINT32 n)
{
    UINT32 off = 0;
    while (off + n <= total) {
        if (memcpy_s(b + off, total - off, s, n) != EOK) break;
        off += n;
    }
}
static void L2(char *b, UINT32 total, const char *s, UINT32 n) { L3(b, total, s, n); }
static void L1(char *b, UINT32 total, const char *s, UINT32 n) { L2(b, total, s, n); }
void S3_7(const char *s) { char buf[64]; L1(buf, sizeof(buf), s, 8); }
/* total - off <= extent(b) - off 先化简成 total <= extent(b)，与 off 的循环区间无关 -> 不报 */
```

报文组装代码里大量是这种写法。不先化简、直接拿 `off` 的区间 `[0, +∞)` 去比，这里会误报成片。

**S3.8 循环内累积偏移，`destMax` 却用整体大小（缺陷）** —— A3·M1·P1·V1·R2·K3 —— L2 —— **尽力**

```c
static void L3(char *b, UINT32 total, const char *s, UINT32 n)
{
    UINT32 off = 0;
    while (off + n <= total) {
        if (memcpy_s(b + off, total, s, n) != EOK) break;   /* 化简成 off <= extent(b) - total = 0 */
        off += n;
    }
}
static void L2(char *b, UINT32 total, const char *s, UINT32 n) { L3(b, total, s, n); }
static void L1(char *b, UINT32 total, const char *s, UINT32 n) { L2(b, total, s, n); }
void S3_8(const char *s) { char buf[64]; L1(buf, sizeof(buf), s, 8); }   /* 第二轮起 off > 0 -> L2 */
```

**S3.9 调用链在循环里：循环变量逐层下传到访问点** —— A1·M1·P1·V1·R2·K2 —— L2 —— **承诺**

```c
static void L3(char *p, UINT32 i) { p[i] = 0; }   /* requires extent(p) >= i + 1 */
static void L2(char *p, UINT32 i) { L3(p, i); }
static void L1(char *p, UINT32 i) { L2(p, i); }
void S3_9(void) { char buf[16]; for (UINT32 i = 0; i <= 16; i++) L1(buf, i); }   /* 调用点 i 取值 [0,16] -> L2 */
```

---

### Lv4 内存来自下层：多层封装的分配器、出参、getter

内存大小的信息先**往上**走（从分配器一层层返回），再**往下**走到访问点——"V 形"链。

**新增能力**：返回值效应 / 出参效应（新申请的堆，大小 = 形参表达式，`01-` §3.7）；返回值的取值范围；底层分配器配置。

**S4.1 多层封装的分配器（返回值）** —— A1·M4·P2+P1·V0 —— L1 —— **承诺**

```c
static void *Pool_Alloc(UINT32 sz) { return VOS_MemAlloc(PID, 0, sz); }   /* ReturnEffect: fresh heap, extent = sz */
static void *Mod_Alloc(UINT32 sz)  { return Pool_Alloc(sz); }
static char *Buf_New(void)         { return Mod_Alloc(16); }              /* extent = 16 */
static void L3(char *p) { p[16] = 0; }
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S4_1(void) { char *p = Buf_New(); if (p == NULL) return; L1(p); }
```

**S4.2 带头部的分配器（实际申请 `n + HDR`，返回 `p + HDR`）** —— A1·M5·P2+P1·V1 —— L1 —— **承诺**

```c
typedef struct { UINT32 magic; UINT32 len; } HDR_S;
static void *Hdr_Alloc(UINT32 n)
{
    char *raw = VOS_MemAlloc(PID, 0, n + (UINT32)sizeof(HDR_S));
    return raw ? raw + sizeof(HDR_S) : NULL;                       /* extent(返回值) = n */
}
static void L3(char *p, UINT32 n) { p[n] = 0; }
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S4_2(void) { char *p = Hdr_Alloc(16); if (p == NULL) return; L1(p, 16); }
```

靠形参名猜长度在这里必错，靠摘要推导是对的（`01-` §3.7.1）。

**S4.3 出参分配器** —— A1·M5·P2+P1·V1 —— L1 —— **承诺**

```c
static UINT32 Obj_Create(char **out, UINT32 n) { *out = VOS_MemAlloc(PID, 0, n); return (*out != NULL) ? 0 : 1; }
                                                         /* OutParamEffect(arg#0): fresh heap, extent = n */
static void L3(char *p, UINT32 n) { p[n] = 0; }
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S4_3(void) { char *p = NULL; if (Obj_Create(&p, 16) != 0) return; L1(p, 16); }
```

**S4.4 长度来自 getter 的返回值（最简单的"异链"）** —— A1·M1·P1·V2·R3 —— L1 —— **承诺**

```c
static UINT32 Cfg_GetMaxUsers(void) { return 32; }         /* ensures ret == 32 */
static void L3(char *p, UINT32 n) { p[n - 1] = 0; }
static void L2(char *p, UINT32 n) { L3(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S4_4(void) { char tbl[16]; L1(tbl, Cfg_GetMaxUsers()); }
```

**S4.5 分配器的函数体不可见，只出现在 `config.ini` / `alloc_free_identity` 结果里** —— 规则性处理

知道是堆、不知道多大 → 大小记为符号，**不报、记"大小未知"原因**；配了 `MEM_ALLOC_SIZE` 则按配置（`01-` §3.7.2）。
不单列承诺，但要保证**不误报**。

---

### Lv5 经结构体字段中转；两侧异链汇合

**新增能力**：字段级基址（`arg#0->buf`）；帧内字段存储跟踪（链顶 `ctx.buf = tmp` 之后知道 `ctx.buf` 指向 `tmp`）；
写效应摘要（被调函数改了哪些字段、改成什么）；出参与字段的取值范围。

**S5.1 链顶填好上下文结构体，逐层传指针，底层按字段访问** —— A3·M1·P3·V3·R2 —— L1 —— **承诺**

```c
typedef struct { char *buf; UINT32 len; } CTX_S;
static void L3(CTX_S *c, const char *s) { (void)memcpy_s(c->buf, c->len, s, c->len); }  /* requires extent(c->buf) >= c->len */
static void L2(CTX_S *c, const char *s) { L3(c, s); }
static void L1(CTX_S *c, const char *s) { L2(c, s); }
void S5_1(const char *s) { char tmp[16]; CTX_S ctx = { tmp, 32 }; L1(&ctx, s); }       /* 代换到链顶：16 < 32 -> L1 */
```

上下文结构体一路下传是业务代码里最普遍的传参方式，所以这一项提前到年内。

**S5.2 上下文在独立的 Init 函数里填（写效应）** —— A3·M5·P3+P2·V3·R3 —— L1 —— **2027H1**

```c
typedef struct { char *buf; UINT32 len; } CTX_S;
static void Ctx_Init(CTX_S *c, UINT32 n) { c->buf = VOS_MemAlloc(PID, 0, n); c->len = n + 1; }
                                           /* modifies: c->buf = fresh(n), c->len = n + 1 */
static void L3(CTX_S *c, const char *s) { (void)memcpy_s(c->buf, c->len, s, c->len); }
static void L2(CTX_S *c, const char *s) { L3(c, s); }
static void L1(CTX_S *c, const char *s) { L2(c, s); }
void S5_2(const char *s) { CTX_S ctx; Ctx_Init(&ctx, 16); if (ctx.buf == NULL) return; L1(&ctx, s); }
```

**S5.3 内存沿一条链下传，长度沿另一条链多层返回后汇合** —— A3·M1·P1·V2·R3·X3 —— L2 —— **尽力**

```c
typedef struct MSG MSG_S;
UINT32 Msg_Parse(const MSG_S *m);
static UINT32 Msg_Field(const MSG_S *m)   { return Msg_Parse(m) & 0xFF; }   /* ensures ret in [0,255] */
static UINT32 Msg_BodyLen(const MSG_S *m) { return Msg_Field(m); }
static void L3(char *d, UINT32 n, const char *s) { (void)memcpy_s(d, n, s, n); }
static void L2(char *d, UINT32 n, const char *s) { L3(d, n, s); }
static void L1(char *d, UINT32 n, const char *s) { L2(d, n, s); }
void S5_3(const MSG_S *m, const char *s) { char buf[128]; L1(buf, Msg_BodyLen(m), s); }   /* [0,255] 与 128 部分重叠 -> L2 */
```

机制和 S4.4 相同（返回值取值范围自底向上），难点在链上的精度，所以是尽力。

**S5.4 同一结构体的字段在不同层被改写（缓冲区在一层换、长度在另一层改）** —— P3·V3·R3 —— **2027H1**

**S5.5 `alloc_free_identity` 里带 `offset` 的字段级申请（申请结果写进 `p->field`）** —— M5·P3+P2 —— **尽力**

对应计划 P0-5 的"带 `offset` 场景"，也是计划 §6 砍项顺序的第 3 项；`offset` 的 ABI 问题见 `01-` §3.7.2。

---

### Lv6 全局状态

**S6.1 `extern` 不完整声明的全局数组（定义在另一个文件）** —— A1·M2·P4·V1 —— L1 —— **尽力**

```c
extern char g_cellTbl[];   /* 本文件看不到大小；另一个文件里是 char g_cellTbl[64]; */
static void L3(UINT32 i) { g_cellTbl[i] = 0; }
static void L2(UINT32 i) { L3(i); }
static void L1(UINT32 i) { L2(i); }
void S6_1(void) { L1(64); }
```

需要跨文件的全局大小表（计划 P0-4，砍项顺序第 1 项）。

**S6.2 全局长度变量（只在初始化函数里写常量）** —— V4 —— **2027H1**

```c
static UINT32 g_maxCell;
void Cell_Init(void) { g_maxCell = 64; }
static void L3(char *p) { for (UINT32 i = 0; i < g_maxCell; i++) p[i] = 0; }   /* 取全部写入值的并集：[0,64] */
static void L2(char *p) { L3(p); }
static void L1(char *p) { L2(p); }
void S6_2(void) { char buf[32]; L1(buf); }
```

**S6.3 运行期申请的全局资源，长度来自跨函数变量** —— M5·P4·V4·G4 —— **2027 程序分析验证 + AI 提不变式**

```c
static UINT32 *g_tbl;
static UINT32 g_num;
void Tbl_Init(UINT32 n) { g_tbl = VOS_MemAlloc(PID, 0, n * (UINT32)sizeof(UINT32)); g_num = n; }
static void L3(UINT32 i) { if (i > g_num) return; g_tbl[i] = 0; }   /* 应为 >=；要知道 extent(g_tbl) = 4 * g_num */
static void L2(UINT32 i) { L3(i); }
static void L1(UINT32 i) { L2(i); }
void S6_3(UINT32 i) { L1(i); }
```

判定需要一条**全局不变式** `extent(g_tbl) = 4·g_num`。程序分析能验证"所有写 `g_tbl` / `g_num` 的地方都维持了它"，
但从代码里自动归纳出它（哪个全局量是哪块全局内存的长度）不可靠——这一步交给 AI 提候选，程序分析验证后使用。

**S6.4 访问约束来自运行期可配置的全局值**（`g_cfg.maxPort` 由配置文件或命令设置） —— V4 —— **AI + 程序分析**

取值范围是业务给的，代码里没有。

---

### Lv7 内存建模精度

| 编号 | 场景 | 签名 | 归属 |
|---|---|---|---|
| **S7.1** | 柔性数组成员：`l = Alloc(sizeof(LIST_S) + n * sizeof(UINT32))`，多层后访问 `l->item[n]` | A1·M6·P2+P1 | 2027H1（计划 P1）。`extent(l->item) = 分配大小 − offsetof(item)`，与 S2.5 一样可以符号判定 |
| **S7.2** | 多维数组行级越界（`x.m[i][j]`，`i` 越界但总偏移未越界） | A1·M7 | 计划 P1。CSA 盲区 #18，1–2 层也报 |
| **S7.3** | 字节缓冲按结构体解释：`MSG_S *m = (MSG_S *)raw; m->data[k]` | A1·M1·X4 | 2027H1。要求 `extent(raw) ≥ offsetof(MSG_S, data) + k + 1` |
| **S7.4** | 循环中指针步进：`while (n--) *p++ = 0;` | A2·K3 | 2027H1。要"`p − base` = 已迭代次数"这类关系 |
| **S7.5** | 字符串：`strcpy_s` / `strcat_s` / `sprintf_s` | A4 | `destMax ≤ extent(dest)` 那一半与 `memcpy_s` 相同，计划 P1；依赖源串长度的那一半要字符串长度的抽象，2027H2 |
| **S7.6** | 变量操作数的非线性下标：`a[i * stride + j]`（`stride` 是变量）、`len << shift` | X5 | **不承诺**。各自有界时给 L2 上界 |
| **S7.7** | 联合体 / 类型双关；真别名（二级指针改写、两条路径指向同一块内存） | —— | **不承诺**（要指向分析） |

---

### Lv8 复杂循环

| 编号 | 场景 | 签名 | 归属 |
|---|---|---|---|
| **S8.1** | **数据驱动步长的解析循环**（TLV、消息分段）：每轮的偏移增量是从缓冲区里读出来的长度 | A3·K3·V5 | "本轮访问被本轮校验覆盖"的写法：程序分析尽力（相对条件，不需要跨轮不变式）；依赖跨轮累计关系的：**AI 提不变式 + 程序分析验证** |
| **S8.2** | 非典型循环：`while (*p)`、多出口、循环体内改计数器、`goto` 构成的环 | K4 | 尽力。widening 保证收敛，多数只能给 L2 或不报 |
| **S8.3** | 多层嵌套、下标由多个循环变量非线性组合（`a[i * n + j]`，`n` 是变量） | K4·X5 | **不承诺** |

**你们列的循环子项落在哪里：**

| 循环子项 | 场景 |
|---|---|
| 典型循环 `for (i = 0; i < max; i++)` | S1.2（常量界）、S3.4（界来自多层之上） |
| 循环外的内存访问越界 | S3.5 |
| 多层嵌套循环中访问内存 | S3.6（各维常量界，承诺）/ S8.3（变量步长，不承诺） |
| 循环中进行内存偏移和拷贝 | S3.7（剩余长度写法，TN）/ S3.8（`destMax` 用整体大小，TP）/ S7.4（指针步进） |
| 循环周期性的偏移和拷贝导致的越界 | 固定步长：S3.6 / S3.8（线性，程序分析）；数据驱动步长：S8.1 |
| 非一般常见的循环 | S8.2 |
| 调用链本身在循环里 | S3.9（新增，承诺） |

---

### Lv9 断链：函数指针、外部 / 未定义函数、递归

**S9.1 链上的函数指针在同一函数内被赋常量**（或查常量表） —— P5 —— **尽力**（计划 P1）

```c
static void L3(char *p) { p[8] = 0; }
static void L2(char *p) { void (*fn)(char *) = L3; fn(p); }
static void L1(char *p) { L2(p); }
void S9_1(void) { char buf[8]; L1(buf); }
```

**S9.2 静态操作表：全局 const 结构体初始化的函数指针字段** —— P5 —— **2027H1 程序分析；目标过多时 AI 做精度过滤**

```c
typedef struct { void (*proc)(char *, UINT32); } OPS_S;
static void L3(char *p, UINT32 n) { p[n] = 0; }
static const OPS_S g_ops = { .proc = L3 };
static void L2(char *p, UINT32 n) { g_ops.proc(p, n); }
static void L1(char *p, UINT32 n) { L2(p, n); }
void S9_2(void) { char buf[8]; L1(buf, 8); }
```

按"类型.字段"收集所有赋给它的函数（字段级解析），多个目标时逐个代换取最坏。
目标集合太大时，用 LLM 做分发表识别与精度过滤、再把调用边回注给程序分析——正是你们调研过的那一路。

**S9.3 运行期注册的回调**（`Register(id, fn)`、按消息号查分发表） —— P5 —— **AI + 程序分析**

AI 从注册点和分发逻辑恢复"哪个消息号走哪个函数"，作为外部调用边注入；程序分析照常代换。

**S9.4 未定义 / 外部函数读写传入的内存**（库函数、没进编译数据库的模块） —— P5·M8 —— **模型表（程序分析）+ AI 生成模型草稿**

常用库函数进模型表（`01-` §3.7）；其余由 AI 从头文件和接口文档生成模型草稿，人工确认后入表。

**S9.5 递归** —— K5 —— **2027H1**（强连通分量内迭代到不动点）；年内按断链记原因。

---

### Lv10 业务语义与隐式契约 → AI + 程序分析

| 编号 | 场景 | 签名 | AI 做什么 | 程序分析做什么 |
|---|---|---|---|---|
| **S10.1** | 结构体的缓冲区字段与长度字段对应关系用错（用 `dataLen` 校验 `ext`） | G4·P3·V3 | 推断字段配对（`ext` ↔ `extLen`） | 按配对生成条件，在所有访问点多层传播检查 |
| **S10.2** | 函数的指针入参与长度入参（`pBuf` ↔ `ulBufLen`）没被调用方遵守 | G4·P1·V1 | 推断参数配对 | 在所有调用点（多层）检查 `extent(pBuf) ≥ ulBufLen` |
| **S10.3** | 注释或接口文档写明的取值约束（"`ulPortId` 取值 0 ~ `MAX_PORT − 1`"）没被满足 | G4·V1 | 抽取为前置条件 | 检查调用链上是否满足 |
| **S10.4** | 外部输入的长度未经校验一路下传到访问点 | V5·G0 | 读逐层 trace 与上下文，研判上层框架是否已统一校验 | 给出候选（"证明不了安全、来源不可信"）和逐层 trace |

S10.1 的一个细节：**如果那块缓冲区的大小在汇合点是已知的**（比如 `ext` 实际指向一个 16 字节数组），
用错长度字段校验其实会被 Lv5 直接查出来（校验界与实际大小不一致，同 S3.2）。
真正需要 AI 的，是缓冲区大小在分析范围里不可知、只能靠"哪个字段是它的长度"这条业务知识的那部分。

```c
typedef struct { char *data; UINT32 dataLen; char *ext; UINT32 extLen; } PKT_S;
static void L3(PKT_S *pkt, UINT32 off) { if (off >= pkt->dataLen) return; pkt->ext[off] = 0; }   /* 用 dataLen 校验 ext */
static void L2(PKT_S *pkt, UINT32 off) { L3(pkt, off); }
static void L1(PKT_S *pkt, UINT32 off) { L2(pkt, off); }
void S10_1(PKT_S *pkt, UINT32 off) { L1(pkt, off); }
```

---

### 附：用 Infer 当标尺跑了一遍【实测数据】

本节 36 段代码用 `probes/scenarios/snippets.py` 抽出，Infer v1.3.0 `--bufferoverrun-only` 逐段跑
（`--shim` 把 `VOS_MemAlloc` 换成 `malloc`、把 `memcpy_s` / `memset_s` 换成按 `destMax` 写的 `memset`，因为 Infer 没有这两类模型）。
链长都是 3 层。

| 结果 | 场景 |
|---|---|
| **Infer 报出，级别与本文期望一致（15）** | S1.1 L1、S1.2 L2、S1.3 L1、S1.5 L1、S2.2 L1、S2.3 L1、**S2.5 L1**（`n` 对 `n + 1` 的符号判定）、S3.4 L2、S3.5 L1、S3.6 L2、S3.9 L2、**S4.1 / S4.2 / S4.3 L1**（多层封装、带头部、出参分配器）、S4.4 L1 |
| **Infer 不报，而本设计要报（年内承诺里有 10 个）** | S1.4 结构体成员地址下传、S1.6 全局数组、S2.6 标量强转、S2.7 掩码、S3.2 校验界大于实际大小；以及全部安全函数场景 S1.7、S2.1、S2.4、S3.1、S5.1 |
| TN 无误报 | S2.7_TN、S3.1_TN、S3.2_TN、S3.7 |
| 其余不报（本就不在 Infer 能力内，或属于 2027 / AI + 程序分析） | S3.3、S3.8、S5.2、S5.3、S6.1–S6.3、S9.1、S9.2、S10.1 |

安全函数那一行单独核对过：`char buf[16]; memset(buf, 0, 64);` 在同一个函数里 Infer 报 L1，
**同样的 `memset` 挪进被调函数、由调用方传入 `buf` 就不报了**——至少在 v1.3.0 的这个用法下，Infer 的 `memset` / `memcpy` 模型没有变成被调函数摘要里的条件往上传。

两点结论：

1. **26 个承诺场景里，Infer 报出 15 个，级别和本文的期望逐个对得上**。Lv1–Lv4 的核心形态用摘要方式能做、分级口径也站得住，Infer 就是现成的证明。
2. **Infer 漏掉的 10 个，正好是本设计比 Infer 多做的那几项**：按字段类型算大小、全局数组、按字节归一、掩码区间、带守卫的条件、安全函数条件上传。
   这也是"不直接用 Infer"的又一条实据——它漏掉的恰是你们代码里最常见的写法（安全函数、结构体成员、全局表）。

复现：

```bash
cd 08-summary2-design/probes/scenarios
python3 snippets.py check --clang <任意 clang>                       # 36 段语法检查
python3 snippets.py infer --infer <infer>/bin/infer --shim             # 标尺对照表
```

---

## 4. 总表（从简单到复杂）

| 序 | 编号 | 场景 | 签名 | 级别 | 年内 | 最终归属 |
|---|---|---|---|---|---|---|
| 1 | S1.1 | 定长数组下传，访问点常量下标 | A1·M1·P1·V0·R1·X0·K0·G0 | Lv1 | **承诺** | 程序分析 |
| 2 | S1.2 | 定长数组下传，访问点常量界循环 | A1·M1·P1·V0·R1·X0·K2·G0 | Lv1 | **承诺** | 程序分析 |
| 3 | S1.3 | 常量长度堆内存下传 | A1·M4·P1·V0·R1 | Lv1 | **承诺** | 程序分析 |
| 4 | S1.4 | 结构体数组成员的地址下传 | A1·M3·P1·V0·R1 | Lv1 | **承诺** | 程序分析 |
| 5 | S1.5 | 链顶常量下标下传，访问函数自己的数组 | A1·M1·P0·V1·R1 | Lv1 | **承诺** | 程序分析 |
| 6 | S1.6 | 定义可见的全局数组，下标下传 | A1·M2·P0·V1·R1 | Lv1 | **承诺** | 程序分析 |
| 7 | S1.7 | `memcpy_s` 目的缓冲区下传，`destMax` 报大 | A3·M1·P1·V0·R1 | Lv1 | **承诺** | 程序分析 |
| 8 | S2.1 | 缓冲区与长度同链下传 | A3·M1·P1·V1·R2·X0 | Lv2 | **承诺** | 程序分析 |
| 9 | S2.2 | 长度线性变换 | A1·M1·P1·V1·R2·X1 | Lv2 | **承诺** | 程序分析 |
| 10 | S2.3 | 指针偏移后下传 | A2·M1·P1·V1·R2·X1 | Lv2 | **承诺** | 程序分析 |
| 11 | S2.4 | `memcpy_s(dst + off, 整体大小)` | A3·M1·P1·V1·R2·X1 | Lv2 | **承诺** | 程序分析 |
| 12 | S2.5 | 变量长度堆，申请与访问同源 | A1·M5·P1·V1·R2·X1 | Lv2 | **承诺** | 程序分析 |
| 13 | S2.6 | 标量指针强转 | A1·M1·P1·V1·R2·X2 | Lv2 | **承诺** | 程序分析 |
| 14 | S2.7 | 掩码 / 取模 / 有界移位 | A1·M1·P1·V1·R2·X3 | Lv2 | **承诺** | 程序分析 |
| 15 | S3.1 | 外层校验后下传 | A3·M1·P1·V1·R2·K1·G2 | Lv3 | **承诺** | 程序分析 |
| 16 | S3.2 | 访问函数校验界大于实际大小 | A1·M1·P1·V1·R2·G1 | Lv3 | **承诺** | 程序分析 |
| 17 | S3.3 | 分支合并后一支越界 | A3·M1·P1·V1·R2·K1 | Lv3 | 尽力 | 程序分析 |
| 18 | S3.4 | 典型循环，界来自多层之上 | A1·M1·P1·V1·R2·K2 | Lv3 | **承诺** | 程序分析 |
| 19 | S3.5 | 循环退出后使用计数器 | A1·M1·P1·V1·R2·K2 | Lv3 | **承诺** | 程序分析 |
| 20 | S3.6 | 规则嵌套循环 | A1·M1·P1·V0·R1·K2 | Lv3 | **承诺** | 程序分析 |
| 21 | S3.7 | 循环按剩余长度拷贝（TN） | A3·M1·P1·V1·R2·K3 | Lv3 | **承诺** | 程序分析 |
| 22 | S3.8 | 循环累积偏移，`destMax` 用整体大小 | A3·M1·P1·V1·R2·K3 | Lv3 | 尽力 | 程序分析 |
| 23 | S3.9 | 调用链在循环里 | A1·M1·P1·V1·R2·K2 | Lv3 | **承诺** | 程序分析 |
| 24 | S4.1 | 多层封装分配器（返回值） | A1·M4·P2+P1·V0 | Lv4 | **承诺** | 程序分析 |
| 25 | S4.2 | 带头部的分配器 | A1·M5·P2+P1·V1 | Lv4 | **承诺** | 程序分析 |
| 26 | S4.3 | 出参分配器 | A1·M5·P2+P1·V1 | Lv4 | **承诺** | 程序分析 |
| 27 | S4.4 | 长度来自 getter 返回值 | A1·M1·P1·V2·R3 | Lv4 | **承诺** | 程序分析 |
| 28 | S4.5 | 分配器函数体不可见 | M5·P2 | Lv4 | 规则性处理（不误报） | 程序分析 |
| 29 | S5.1 | 上下文结构体在链顶填好后下传 | A3·M1·P3·V3·R2 | Lv5 | **承诺** | 程序分析 |
| 30 | S5.2 | 上下文在 Init 函数里填（写效应） | A3·M5·P3+P2·V3·R3 | Lv5 | 2027H1 | 程序分析 |
| 31 | S5.3 | 长度沿另一条链多层返回后汇合 | A3·M1·P1·V2·R3·X3 | Lv5 | 尽力 | 程序分析 |
| 32 | S5.4 | 字段在不同层被改写 | P3·V3·R3 | Lv5 | 2027H1 | 程序分析 |
| 33 | S5.5 | 带 `offset` 的字段级申请 | M5·P3+P2 | Lv5 | 尽力 | 程序分析 |
| 34 | S6.1 | `extern` 不完整声明的全局数组 | A1·M2·P4·V1 | Lv6 | 尽力 | 程序分析 |
| 35 | S6.2 | 全局长度变量（初始化写常量） | V4 | Lv6 | 2027H1 | 程序分析 |
| 36 | S6.3 | 运行期申请的全局资源 | M5·P4·V4·G4 | Lv6 | 2027 | 程序分析验证 + AI 提不变式 |
| 37 | S6.4 | 运行期可配置的全局值 | V4 | Lv6 | —— | AI + 程序分析 |
| 38 | S7.1 | 柔性数组成员 | A1·M6·P2+P1 | Lv7 | 2027H1（P1） | 程序分析 |
| 39 | S7.2 | 多维数组行级越界 | A1·M7 | Lv7 | P1 | 程序分析 |
| 40 | S7.3 | 字节缓冲按结构体解释 | A1·M1·X4 | Lv7 | 2027H1 | 程序分析 |
| 41 | S7.4 | 循环中指针步进 | A2·K3 | Lv7 | 2027H1 | 程序分析 |
| 42 | S7.5 | 字符串类安全函数 | A4 | Lv7 | 2027H2（`destMax` 一半 P1） | 程序分析 |
| 43 | S7.6 | 变量操作数的非线性下标 | X5 | Lv7 | —— | 不承诺（有界时 L2） |
| 44 | S7.7 | 联合体 / 真别名 | —— | Lv7 | —— | 不承诺 |
| 45 | S8.1 | 数据驱动步长的解析循环 | A3·K3·V5 | Lv8 | —— | 部分程序分析尽力；其余 AI + 程序分析 |
| 46 | S8.2 | 非典型循环 | K4 | Lv8 | —— | 程序分析尽力 |
| 47 | S8.3 | 多变量非线性下标的嵌套循环 | K4·X5 | Lv8 | —— | 不承诺 |
| 48 | S9.1 | 同函数内常量赋值的函数指针 | P5 | Lv9 | 尽力（P1） | 程序分析 |
| 49 | S9.2 | 静态操作表 | P5 | Lv9 | 2027H1 | 程序分析（AI 精度过滤） |
| 50 | S9.3 | 运行期注册的回调 | P5 | Lv9 | —— | AI + 程序分析 |
| 51 | S9.4 | 未定义 / 外部函数 | P5·M8 | Lv9 | —— | 模型表 + AI 生成模型 |
| 52 | S9.5 | 递归 | K5 | Lv9 | 2027H1 | 程序分析 |
| 53 | S10.1 | 缓冲区与长度字段配对用错 | G4·P3·V3 | Lv10 | —— | AI + 程序分析 |
| 54 | S10.2 | 指针入参与长度入参不匹配 | G4·P1·V1 | Lv10 | —— | AI + 程序分析 |
| 55 | S10.3 | 注释 / 文档约束未满足 | G4·V1 | Lv10 | —— | AI + 程序分析 |
| 56 | S10.4 | 外部输入长度未经校验 | V5·G0 | Lv10 | —— | AI + 程序分析（候选研判） |

合计 56 个场景：**年内承诺 26**、年内尽力 6、规则性处理 1（S4.5）、2027 程序分析 11（含 S8.2 尽力）、
AI + 程序分析 9（含 S6.3、S8.1 两个"程序分析验证、AI 补事实"的）、不承诺 3。

---

## 5. 年内优先与实现顺序

### 5.1 年内承诺（26 个）与尽力（6 个）

| 级别 | 承诺 | 尽力 |
|---|---|---|
| Lv1 | S1.1–S1.7（7） | —— |
| Lv2 | S2.1–S2.7（7） | —— |
| Lv3 | S3.1、S3.2、S3.4、S3.5、S3.6、S3.7、S3.9（7） | S3.3、S3.8 |
| Lv4 | S4.1–S4.4（4）；S4.5 保证不误报 | —— |
| Lv5 | S5.1（1） | S5.3、S5.5 |
| Lv6 | —— | S6.1 |
| Lv9 | —— | S9.1 |

### 5.2 实现顺序（对应计划周次）

| 顺序 | 内容 | 周 | 检查点 |
|---|---|---|---|
| 1 | Lv1（同文件）：形参代换、常量界、数组 / 常量堆 / 全局数组大小、层数过滤、trace | W04–W05 | —— |
| 2 | Lv2：线性表达式、带偏移的基址、字节归一、常量操作数区间运算 | W05 | —— |
| 3 | 跨文件执行（多轮扫描 + 工作表） | W06 | **G1（10-30）**：Lv1 + Lv2 全部 14 个场景在 3 / 5 / 10 / 17 / 20 层报出，≤ 2 层不报，三文件跨文件用例报出 |
| 4 | Lv3：条件收窄、合并、循环（含符号 narrowing）、**条件带守卫**、**访问点先化简** | W07 | —— |
| 5 | Lv4：返回值 / 出参效应、返回值范围、分配器配置 | W08 | —— |
| 6 | S5.1、安全函数族收尾；尽力项 S3.8、S6.1、S5.5 | W09 | **G2（11-20）**：26 个承诺场景全部通过，TN 零误报；尽力项给出报出率 |
| 7 | 真实代码子集 → 全量；**按级别统计告警与断链原因** | W10–W11 | **G3（12-04）** |

**为什么是这个顺序**：每一级只多要一种能力，而且是下一级的前置条件
（Lv3 的符号循环界要用 Lv2 的线性表达式；Lv4 的"大小 = 形参表达式"要用 Lv2；Lv5 的字段代换要用 Lv4 的效应）。
Lv3 排在 Lv4 前面，是因为真实代码几乎处处有校验，没有 Lv3 就上真实代码会误报成片；
Lv4 排在 Lv5 前面，是因为分配器封装在你们的代码里是固定形态、有现成的配置和识别结果可用。

### 5.3 2027 路线

| 时间 | 程序分析 | AI + 程序分析 |
|---|---|---|
| 2027H1 | Lv5 其余（S5.2、S5.4）；S6.2；S7.1–S7.4；S9.2；S9.5；增量分析 | 接口联调（§7 的四个注入点）；S9.3 调用边恢复；S10.1 / S10.2 配对 |
| 2027H2 | S7.5 字符串 | S6.3 / S6.4 不变式；S8.1；S10.3；S10.4 研判闭环 |

---

## 6. 程序分析的覆盖上限

**一句话：事实都在被分析的代码里，能用线性表达式和区间表达，调用边能静态确定，循环的影响能用区间 widening 概括。**
四条同时成立，程序分析就承诺（或计划承诺）；任何一条不成立，就不由程序分析单独承诺。

| 条件 | 不成立时的典型场景 | 去向 |
|---|---|---|
| ① 大小、访问量、校验都来自被分析的代码或已建模的函数 | S9.4 外部函数、S6.4 运行期配置、S10.x 业务约束 | AI 补事实 |
| ② 取值能用线性表达式（含常量操作数的非线性）和区间表达 | S7.6、S8.3 变量乘积 | 不承诺（有界时 L2） |
| ③ 链上的调用边能静态确定 | S9.3 运行期注册 | AI 补调用边 |
| ④ 循环的影响能用区间 widening 概括（界线性、步长固定） | S8.1 数据驱动步长 | AI 提不变式 / 不承诺 |

**按级别说**：Lv1–Lv5 全部；Lv6 除运行期配置值（S6.4）外；Lv7 除 S7.6、S7.7 外；Lv8 的 S8.2 尽力、S8.1 的相对条件写法尽力；
Lv9 的 S9.1、S9.2、S9.5。这是程序分析这条线**最终**计划做到的复杂度，年内做到 Lv4 + S5.1。

---

## 7. AI + 程序分析：分工与接口

**原则**：AI 补"代码里没有、或者程序分析推不出来的事实"，程序分析负责**验证 + 多层传播 + 给出逐层证据**。
所有 AI 提供的事实带来源标注（`llm`），**人工确认前只参与 L2，不参与 L1**——与 `01-` §3.7.2 对启发式分配器大小的处理是同一条规则。

| 缺的事实 | 场景 | AI 做什么 | 注入到摘要引擎的哪里 | 程序分析接着做什么 |
|---|---|---|---|---|
| 调用边 | S9.2（精度过滤）、S9.3 | 分发表 / 注册点识别，给出函数指针 → 目标集合 | SummaryDB 的**外部调用边表** | 按调用边代换、多层传播 |
| 函数行为 | S9.4 | 从头文件、接口文档生成模型草稿 | `summary_models.yaml`（来源 = `llm`，待确认） | 当作模型使用 |
| 缓冲区 ↔ 长度配对 | S10.1、S10.2 | 字段配对、参数配对 | **规格表**：`extent(buf) ≥ len` 作为契约 | 在所有访问点、调用点检查 |
| 取值约束 / 不变式 | S10.3、S6.3、S6.4、S8.1 | 从注释、接口文档、初始化代码提出候选不变式 | **规格表**：前置条件 / 全局不变式 | 先验证不变式在代码里被维持，再用来判定 |
| 告警研判 | S10.4、所有 L2 | 读逐层 trace 与上下文，判断是否已被框架统一校验 | 告警 → 研判队列 | 研判结论回写（抑制 / 升级） |

**年内要做的只有一件事：把这四个注入点的表结构预留好**（外部调用边表、模型表的来源字段、规格表、断链 / 未证明原因的导出），
在 W02 建 SummaryDB 表结构时一并建，约 0.5 人日。AI 侧 2027 再接。
这些表加上每个函数的摘要和逐层 trace，本身就是"程序分析给 Agent 提供的查询接口"——
字段配对（S10.1）和函数指针目标恢复（S9.2 / S9.3）与你们已经在做的 LLM + 静态分析那两条线是同一种分工，可以直接对接。

---

## 8. 验收语料怎么组织（取代"3–5 个真实样例"作为验收基准）

- **每个承诺场景至少一对 TP / TN**。TN 是把越界改成安全的最小改动（校验改对、长度改对、`destMax` 扣掉偏移）。
- **链长变体**：Lv1、Lv2 的 14 个场景全部做 3 / 5 / 10 / 17 / 20 层；Lv3–Lv5 每级选 2 个代表做全链长。
- **每个 TP 附期望告警**：级别、访问点行号、逐层 trace。
- **规模**：26 × 2 + 链长变体约 60 ≈ **110 个用例文件**；由我按 `csa-checker-dev` 的正交用例约定写，W02–W03 交第一批（Lv1–Lv2）。
- **真实样例改为可选**：不再是 G0 的前置输入。有的话用来检验"真实漏报落在哪一级"；
  W10 跑真实代码时，每条告警和每条已知漏报都按本文件的级别归类，用分布反过来校准 2027 的优先级。

---

## 9. 对设计与计划的影响（已同步）

**分级过程中发现的两处设计缺口**（已补进 `01-architecture.md`）：

1. **条件要带守卫**（S3.2）：访问前的校验不能只变成一个数值区间，要保留"守卫 ⇒ 条件"或 `min(i, 31)` 这样的符号界，
   否则链顶传常量的调用点会误报。补在 §3.3 BoundExpr。
2. **访问点先符号化简、再代入区间**（S3.7）：同一局部变量在条件两边出现时先抵消，
   否则"按剩余长度拷贝"这种最常见的安全写法会误报成片。补在 §3.6 Applier。

**计划**（`00-implementation-plan.md` v3.10）：P0 八项不变；G1 / G2 的过关标准改按级别；
输入清单里"真实样例"改为可选、"现有 checker 源码"取消（注册、CMake、告警格式按 `csa-checker-dev` skill 的约定写）；
W02 增加 AI + 程序分析注入点的表结构预留。
