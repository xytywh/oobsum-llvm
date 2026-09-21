# 待验证假设与实测清单

> **按"能不能推翻设计"排序，不是按"好不好测"排序。**
> Tier 0 的四项加起来大约**半天到一天**，但它们能决定：
> 方案 C 要不要做、怎么做、工作量是不是我估的那个量级。
>
> **测完 Tier 0 再开工。** 把结果贴给我，我会据此修正
> `01-architecture.md` 的架构决策与 `02-effort-and-milestones.md` 的数字。

---

## 怎么用这份清单

每项给了：**要回答的问题 / 怎么测 / 结果怎么解读 / 会改变什么**。
结果请按这个格式回给我，便于我直接改文档：

```
T0-1: <一句话结论> + <关键数字/输出片段>
T0-2: ...
```

---

# Tier 0 —— 能推翻设计的，先测（半天～一天）

## T0-1 · CSA 的 extent 能力基线【验证假设 A1】

**要回答**：你们的 LLVM 14 fork 里，CSA 能不能对**结构体字段数组、全局、堆**
拿到正确的 extent？这决定 `ExtentModel` 是"做映射"（10 人日）还是"重写"（16 人日）。

**怎么测**：写一个最小的一次性 checker（或改一个现有的），在 `checkLocation` 里打印：

```cpp
void checkLocation(SVal Loc, bool IsLoad, const Stmt *S, CheckerContext &C) const {
  const MemRegion *MR = Loc.getAsRegion();
  if (!MR) return;
  const MemRegion *Base = MR->getBaseRegion();
  SValBuilder &SVB = C.getSValBuilder();

  // ★ 这个 API 在你们 fork 里叫什么、存不存在，正是要确认的
  DefinedOrUnknownSVal Size =
      MR->getMemRegionManager().getStaticSize(Base, SVB);

  llvm::errs() << "[EXT] " << MR->getKindStr()
               << "  base=" << Base->getKindStr()
               << "  size=" << Size << "\n";
}
```

跑在这段 C 上（**就是本包 `07-capability-boundary/corpus/probe/` 的等价物**）：

```c
struct S { int n; int d[8]; };
struct Out { struct S in; };
int g_noinit[10];
int g_init[10] = {0};
struct S gs;

int f(int i, struct S *p) {
    struct S s;  static struct S st;  struct S arr[4];  int a[10];
    struct Out o;
    int *heap = (int*)malloc(4 * sizeof(int));
    return a[i] + s.d[i] + st.d[i] + gs.d[i] + arr[1].d[i] + o.in.d[i]
         + p->d[i] + g_noinit[i] + g_init[i] + heap[i];
}
```

**要回答的具体问题**：

| 问 | 记录什么 |
|---|---|
| `getStaticSize` 这个名字在你们 fork 里存在吗？ | 存在 / 叫 `getExtent` / 都没有 |
| 对 `s.d[i]`（局部 struct 字段）返回什么？ | 具体值 8 / Unknown |
| 对 `gs.d[i]`（全局 struct 字段）？ | |
| 对 `g_noinit[i]`（全局无初始化器）？ | **这是 Infer 漏报的那一项** |
| 对 `arr[1].d[i]`、`o.in.d[i]`？ | |
| 对 `p->d[i]`（形参 struct 指针）？ | |
| 对 `heap[i]`？ | |

**解读**：

* **大部分返回具体值** → A1 成立。**这是方案 C 最有力的支撑**：
  你们在"内存建模覆盖面"这个最决定召回率的维度上**起点比 Infer 高**，
  ExtentModel 变成"包一层 + 补 #12–#17"，省 4–6 人日。
* **多数 Unknown** → A1 不成立。ExtentModel 要从 `ASTContext` 的声明类型自己算，
  +4–6 人日，且 `01-architecture.md` §3.1 的"可以借用 CSA region 机制"那段要删掉。

---

## T0-2 · 现有越界告警的"单函数内可判定"比例

**要回答**：你们现有的 TOP23 越界规则报出来的告警里，有多大比例**其实只靠单函数内
信息就能判定**？这直接决定**跨过程摘要的收益上限**。

**怎么测**：取最近一次全量扫描的告警（或抽 100 条），人工/脚本分类：

| 类别 | 判据 |
|---|---|
| A. 单函数内可判定 | 数组声明、下标计算、访问点都在同一函数 |
| B. 跨 1 层 | 下标或 extent 来自直接调用者/被调者 |
| C. 跨 ≥2 层 | 需要更深 |
| D. 需要全局/堆的跨 TU 信息 | |

**解读**：

* **A 占 > 70%** → 跨过程摘要的增量收益有限，
  **应该优先做 ExtentModel（对 A 类也有直接提升），摘要框架降级。**
* **B+C 占 > 40%** → 方案 C 的 ROI 成立，按 `02-effort-and-milestones.md` 走。

---

## T0-3 · 真实越界的调用深度分布【验证假设 A4】

**要回答**：报告 §15 证明了"200 层 0.76 秒"，但**你们的越界实际需要几层？**
如果 90% 在 2 层内，"把深度从 2 提到十几层"这个立项前提就不成立。

**怎么测**：两个来源，哪个方便用哪个——

1. **历史 DTS 单**：抽 30–50 个已确认的真实越界缺陷，人工数"从数组声明处到越界访问处，
   跨了几个函数"。
2. **用 Infer 反查**：在你们代码上跑 infer，看报出来的告警里
   `by call to 'X'` 出现的嵌套层数（`report.json` 的 `bug_trace` 长度）：

```bash
python3 - <<'PY'
import json, collections
r = json.load(open("infer-out/report.json"))
c = collections.Counter()
for x in r:
    if "BUFFER_OVERRUN" not in x["bug_type"]: continue
    # bug_trace 里 level 的最大值 ≈ 跨过程深度
    c[max((t.get("level", 0) for t in x.get("bug_trace", [])), default=0)] += 1
print("跨过程深度分布:", sorted(c.items()))
PY
```

**解读**：

* **深度 ≥ 2 的占 > 30%** → A4 成立，方案 C 的核心前提成立。
* **深度 ≤ 1 占 > 85%** → **A4 不成立**。这时我建议走
  `02-effort-and-milestones.md` §5 那个"便宜一个数量级"的选项：
  只做 ExtentModel + 把 inline 深度从 2 提到 4–5，**15–20 人日拿 70% 收益**。
  我会据此重写推荐结论——**这是我最希望你先测的一项。**

---

## T0-4 · unsupported 的根因分布【验证假设 A3】

**要回答**：你们现在说的"遇到 Loop 就 unsupported"，循环到底占多大比例？
这决定 `01-architecture.md` §1 那条修正（"AI 主、CSA 副"）成不成立。

**怎么测**：在现有 checker 里，凡是走到 `unsupported` / 放弃分析的分支，
加一行分类打印，然后全量跑一次：

```cpp
// 每个放弃点都带一个 reason enum
enum class GiveUpReason {
  Loop, PointerArith, FnPtr, Recursion, InlineDepth,
  BlockVisitLimit, UnknownCall, Aggregate, Other
};
llvm::errs() << "[GIVEUP] " << reasonStr(R) << " @ " << funcName << "\n";
```

然后 `sort | uniq -c | sort -rn`。

**解读**：

* **Loop > 40%** → A3 成立，§1 的"AI 主、CSA 副"是对的。
* **Loop < 20%，UnknownCall / InlineDepth 占大头** → **A3 不成立**。
  这说明瓶颈是**跨过程**而不是**循环**，那么：
  - `01-architecture.md` §1 的修正可以撤销，回到"CSA 主采事实 + 小 FixpointEngine"，
  - SummaryGen 的规模大幅缩水（M1 省 15+ 人日），
  - 但 SummaryManager / Applier 的重要性上升。
  **这会显著改变工作量分布，请务必测。**

---

# Tier 1 —— 影响参数与优先级（1–2 天）

## T1-1 · Infer 在你们代码上的 capture 真实覆盖率 ✅ **已完成（2026-09-20）**

```bash
# 分母要用【唯一源文件数】，不是 cdb 记录数——infer 按源文件去重落库
python3 -c "import json,os;c=json.load(open('compile_commands.json'));print(len(set(os.path.normpath(e['file']) for e in c)))"
sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM source_files;"
sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM procedures;"
```

**实测结果**（1383 全量工程，跨编译 aarch64_be-ilp32 配置）：

| 指标 | 数值 |
|---|---|
| cdb 记录数 | 10640 |
| cdb 唯一源文件数 | 10232 |
| capture.db 源文件数 | **10232** |
| capture.db 过程数 | **328602** |
| **按唯一源文件的成功率** | **100.0%** |

**结论**：**报告 §31 列为第一风险的那一项（"Infer 能不能吃下你们的代码"）已经过关。**
代价是一组人为改写目标宏的参数（`-U__LP64__ -D__ILP32__=1`），
过程见 `06-practice/03-cross-toolchain-capture-fix.md`。

**因此下一步变成两件事**：

1. **必须先跑能力探针**：`cd ../07-capability-boundary && ./run-boundary.sh tp default`，
   预期 18/22。因为 `__SIZEOF_POINTER__` 仍是 8（clang builtin，`-D` 改不了），
   与真实 ilp32 的 4 字节指针不一致。**对不上就说明目标模型的改写影响到了分析本身，
   全量告警的漏报要打折看。**
2. 然后才是 `infer analyze`，拿 analyze 阶段的数字（耗时、内存、告警数、抽样 TP/FP）。

**遗留的已知损失**（不阻塞，但要记在账上）：
17 个文件的部分方法被 infer 前端 assert 崩溃跳过（`cAst_utils.ml:249` 断言失败，
是 infer 自身的 bug，没有 flag 可绕），这些方法的分析结果是空的。

## T1-2 · 有多少函数是"根本没看"

```bash
grep -c "Skipped large procedure" infer-out/logs
grep   "Skipped large procedure" infer-out/logs | sort -u | head -20
```

**解读**：这个数不为 0，说明 Infer 报出来的"干净"里有一部分是"没看"。
也直接告诉我们 `--bo-max-cfg-size` 该不该调，以及**我们自己的引擎要不要设这个阀**。

## T1-3 · 内存访问经由内部封装函数的比例【验证假设 A2】

```bash
# 在你们代码里统计裸下标访问 vs 封装函数调用的大致比例
grep -rInE '\b(VOS_MemCpy|VOS_MemSet|RB_[A-Za-z]*Cpy|memcpy|memmove|strcpy|strncpy)\s*\(' --include=*.c . | wc -l
grep -rInE '\w+\s*\[\s*\w+\s*\]' --include=*.c . | wc -l
```

粗糙但够用。**解读**：封装函数占比高 → 模型表（`01-` §3.7）提到 P1，性价比极高。

## T1-4 · 循环形态分布

抽 50 个含数组访问的循环，分类：

| 形态 | 例子 | AI 能不能处理 |
|---|---|---|
| 下标 = 循环变量线性 | `for(i=0;i<n;i++) a[i]` | ✅ 阈值 widening 就够 |
| 下标 = 线性 + 偏移 | `a[i*4+3]` | ✅ |
| 下标由循环体内条件改写 | `if(x) i+=2;` | ⚠️ join 后变粗 |
| 指针步进 | `p++; *p` | ✅ 但要 ArrayBlk.offset 建模 |
| 双重循环耦合 | `a[i*W+j]` | ⚠️ 非线性项保留才行 |
| 下标来自函数调用 | `a[next()]` | ❌ 除非有被调摘要 |

**解读**：前两类占 > 60% → M1 的目标可以定成"只覆盖线性"，省时间。

## T1-5 · 两条通道的告警重叠度【验证假设 A6】

拿 Infer 的告警和你们现有 CSA 告警做一次匹配（按 文件+函数+行 粗匹配），
看重叠比例。**解读**：重叠高 → 必须提前设计去重层（`01-` §3.10 的告警指纹）；
重叠低 → 两条通道互补性好，这本身就是方案 C 的卖点。

---

# Tier 2 —— 细化设计用（可以边做边测）

| 编号 | 测什么 | 用途 |
|---|---|---|
| T2-1 | 全局数组 / struct 字段数组在你们代码里的占比 | 验证 ExtentModel 20 种形态的优先级排序 |
| T2-2 | 函数指针调用占全部调用的比例 | 决定"同函数内常量赋值"这个简化版够不够 |
| T2-3 | 函数规模分布（CFG 节点数的 P50/P95/P99） | 决定要不要设 max-cfg-size、设多少 |
| T2-4 | 平均/最大调用图出入度、SCC 规模 | 估算摘要框架的内存与耗时 |
| T2-5 | 一次典型 MR 改动涉及的函数数 | 估算增量分析的实际收益 |

---

# 我拿到数据之后会改什么

明确承诺，免得你觉得测了没用：

| 你给我 | 我会改 |
|---|---|
| T0-1 | `01-` §3.1 的实现路径（借用 CSA region vs 自己从 AST 算）；`02-` 的 ExtentModel 人日 |
| T0-2 | 推荐结论的强度；可能把 ExtentModel 从"P-1 组件"提升为"独立立项" |
| T0-3 | **可能整体降级方案 C**，改推 `02-` §5 那个 15–20 人日的选项 |
| T0-4 | `01-` §1 的架构主副关系；SummaryGen 的规模与 M1 人日 |
| T1-1 | ✅ 已完成，已回填进 `07-capability-boundary/` §7 与 `06-practice/01-…` |
| T1-2 | `07-capability-boundary/` 的"真实规模数字"那一节（analyze 阶段仍是空的） |
| T1-3 | 模型表的优先级 |
| T1-4 | M1 的验收标准（"覆盖哪些循环形态"） |
| T1-5 | `01-` §3.10 是否需要独立的去重组件 |

**任何一项测出来跟我的假设相反，我会直接说"我错了，方案应该改成 X"，
而不是找补。** 这份设计的价值在于它是**可证伪的**，不在于它看起来完整。
