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

## T0-1 · CSA 的 extent 能力基线【验证假设 A1】 ✅ **对象层已完成（2026-09-22）** · 字段层见 T0-1b

**要回答**：你们的 LLVM 14 fork 里，CSA 能不能对**结构体字段数组、全局、堆**
拿到正确的 extent？这决定 `ExtentModel` 是"做映射"（10 人日）还是"重写"（16 人日）。

### 原始探针（v3.3 及以前的写法，保留作对照）

```cpp
void checkLocation(SVal Loc, bool IsLoad, const Stmt *S, CheckerContext &C) const {
  const MemRegion *MR = Loc.getAsRegion();
  if (!MR) return;
  const MemRegion *Base = MR->getBaseRegion();
  SValBuilder &SVB = C.getSValBuilder();

  // ★ 这个 API 在你们 fork 里叫什么、存不存在，正是要确认的
  DefinedOrUnknownSVal Size =
      MR->getMemRegionManager().getStaticSize(Base, SVB);

  llvm::errs() << "[EXT] " << MR->getKindStr()        // ← LLVM 14 没有这个函数，见下
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

### 实测结果（使用方 fork，2026-09-22）

**编译**：`MemRegion::getKindStr()` 在你们的 fork 里不存在。**上游 LLVM 14 也没有**
（`release/14.x` 的 `MemRegion.h` 里没有这个函数，上游 main 里才有），是我写探针时用了新版 API。
使用方改成从 `Regions.def` 用 X-macro 生成名字 —— 这个改法是对的，而且不依赖版本，已并入 T0-1b 的探针。
【源码确认】

**`getStaticSize` 存在**，签名与上游一致（`MemRegionManager::getStaticSize(const MemRegion *, SValBuilder &)`）。【实测数据】

输出 22 行，其中 12 行是标量读取（`NonParamVarRegion size=4` 是读 `i`，`size=8` 是读 `p` / `heap` 这两个指针变量本身），
10 行是数组访问。把 10 个数组访问按顺序对上：

| 访问 | 打印的是哪一层 | fork 实测 | 上游 14.0.6 实测 | 判读 |
|---|---|---|---|---|
| `a[i]` | 变量 `a` | 40 | 40 | ✓ |
| `s.d[i]` | 变量 `s`（**整个 struct**） | 36 | 36 | 对象层 ✓；**不是字段 `d` 的 32** |
| `st.d[i]` | 变量 `st` | 36 | 36 | 同上 |
| `gs.d[i]` | 变量 `gs` | 36 | 36 | 同上 |
| `arr[1].d[i]` | 变量 `arr` | 144 | 144 | 同上 |
| `o.in.d[i]` | 变量 `o` | 36 | 36 | 同上 |
| `p->d[i]` | `p` 指向的符号区域 | `extent_$4{SymRegion{reg_$3<struct S * p>}}` | 同（仅编号不同） | 符号，符合预期 |
| `g_noinit[i]` | 变量 `g_noinit` | 40 | 40 | ✓ **Infer 漏的那一项，CSA 拿到了** |
| `g_init[i]` | 变量 `g_init` | 40 | 40 | ✓ |
| `heap[i]` | malloc 返回的符号区域 | `extent_$5{SymRegion{conj_$1{void *, …}}}` | 同 | 符号；**而且是 `SymRegion` 不是 `HeapSymRegion`** → 这次运行里 malloc **没有被建模** |

两个顺带的判读：

* **为什么形参 `i` / `p` 显示成 `NonParamVarRegion`**：`ipa=none` 下每个函数都是顶层分析，没有调用点；
  LLVM 14 的 `MemRegionManager::getVarRegion` 只在有调用点时才给形参建 `ParamVarRegion`，
  否则走普通 `VarRegion`。不是 bug。【源码确认】
* **`heap` 为什么是 `SymRegion{conj…}`**：`SymbolicRegion::dumpToStream` 在区域属于堆空间时会打印
  `HeapSymRegion{…}`。打的是 `SymRegion` 说明 malloc 的返回值只是个普通的 conjured 指针，
  **大小（16 字节）根本没进状态**。要让它进状态，得开 `unix.DynamicMemoryModeling`（见 T0-1b 对照组 1）。
  【源码确认 + 实测数据】

### 结论

1. **对象层 8/8 具体值全部正确**，包括 Infer 漏报的"全局无初始化器"。
   **fork 的 10 行输出与上游 LLVM 14.0.6 逐行一致**（除了符号编号）。【实测数据】
2. **但这个探针测错了层 —— 这是我写探针的问题。** 假设 A1 问的是 **`FieldRegion`**，
   而探针打印的是 `getBaseRegion()`：它会一路剥到最外层变量，所以对 `s.d[i]` 打出来的是
   整个 `struct S` 的 36，而不是字段数组 `d` 的 32。
   CSA 自己的 `ArrayBoundCheckerV2` 用的是"只剥掉 `ElementRegion` 之后的第一层"
   （LLVM 14 `RegionRawOffsetV2::computeOffset`），对 `s.d[i]` 就是 `FieldRegion d`。【源码确认】
   这两层的差别在 `struct S { int n; int d[8]; }` 上看不出来（`d` 恰好是末尾字段，
   `d[8]` 越出 `d` 也就越出了 `s`），**但字段不在末尾时就是"检得出"和"检不出"的区别**（T0-1b 的 B1）。
3. 所以 **A1 本身还没有在 fork 上测到**。上游 14.0.6 上已经实测成立（见下面 T0-1b 的期望输出），
   而 fork 在对象层与上游逐行一致 —— **大概率同样成立，但需要 T0-1b 花 5 分钟复核。**

---

## T0-1b · 字段层 extent，以及 CSA 的几处盲区【复核假设 A1】 ⏳ 待在 fork 上跑

**两个文件直接覆盖，注册名不变，`Checkers.td` / `CMakeLists` 都不用动**：

| 本包文件 | 覆盖到 |
|---|---|
| `08-summary2-design/probes/t0-1b/StructArrayHeapTestChecker.cpp` | `/usr1/BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/StructArrayHeapTestChecker.cpp` |
| `08-summary2-design/probes/t0-1b/TC_StructArrayHeapTestChecker.c` | `/usr1/BXPlugin/xir/llvm_ast/TestCase/StructArrayHeapTestChecker/TC_StructArrayHeapTestChecker.c` |

探针相对 T0-1 的改动：

* 同时打印**两层**：`array=`（剥掉 `ElementRegion` 后的第一层，= `ArrayBoundCheckerV2` 的比较基准）
  和 `outer=`（`getBaseRegion()`，= T0-1 打印的那层）；
* 用 `getDynamicExtent` 取大小（先查状态里的 `DynamicExtentMap`，malloc 的大小只存在这里；
  查不到再退回 `getStaticSize`）——**`ArrayBoundCheckerV2` 用的就是它**；
* 打印元素个数（`getDynamicElementCount`）、内存空间、源码行列号与表达式原文；
* **只打印数组访问**，`i` / `p` 这类标量读取不再输出，一行对应一个访问。

测试用例 = **T0-1 原样的 A 部分** + 新增 B1–B12，覆盖：非末尾字段数组、二维数组字段、
柔性数组成员三种写法、形参裸指针、`extern` 不完整数组、`long` 数组、malloc、指向数组中间的指针再下标。

**跑三次**（命令就是你 T0-1 用的那条，后两次在 `-extra_args` 里多加一个参数）：

```bash
BXEN=/usr1/BXPlugin/cmake-build-release-release/bxen
TC=/usr1/BXPlugin/xir/llvm_ast/TestCase/StructArrayHeapTestChecker/TC_StructArrayHeapTestChecker.c
EXTRA="-analyzer-config ipa=none -analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"

# ① 基线
$BXEN -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=$TC \
      -checker=rb.enhance.core.StructArrayHeapTest "-extra_args=$EXTRA" 2>&1 | grep '^\[EXT\]' > t01b-base.txt

# ② 打开 malloc 建模：看 B9 / heap 的大小能不能进状态
$BXEN -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=$TC \
      -checker=rb.enhance.core.StructArrayHeapTest "-extra_args=$EXTRA -analyzer-checker=unix.DynamicMemoryModeling" 2>&1 \
      | grep '^\[EXT\]' > t01b-malloc.txt

# ③ 把单元素末尾数组当柔性数组：看 B5
$BXEN -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=$TC \
      -checker=rb.enhance.core.StructArrayHeapTest "-extra_args=$EXTRA -analyzer-config consider-single-element-arrays-as-flexible-array-members=true" 2>&1 \
      | grep '^\[EXT\]' > t01b-fam.txt

wc -l t01b-*.txt        # 上游：每个都是 19 行
```

> ② 如果 bxen 不接受 `-analyzer-checker` 这种追加方式【推断：bxen 的 `-extra_args` 是透传给前端的】，
> 就看输出里 `heap[i]` / `h[i]` 那两行：出现 `HeapSymRegion` 和 `dynamic=16` 就说明 malloc 建模已生效，
> 仍是 `SymRegion` 就说明没生效。

### 期望输出（上游 LLVM 14.0.6 实测，x86-64 目标）【实测数据】

我用 LLVM 14.0.6 官方发布版，把上面**同一份** checker 源码编成插件跑了这份用例。
为了好读，下表省略了 `S64b` 类型后缀和符号编号：

| # | 访问 | `array=`（比较基准） | static | count | `outer=` | outer static | space |
|---|---|---|---|---|---|---|---|
| A | `a[i]` | VarRegion | 40 | 10 | VarRegion | 40 | StackLocals |
| A | `s.d[i]` | **FieldRegion** | **32** | **8** | VarRegion | 36 | StackLocals |
| A | `st.d[i]` | **FieldRegion** | **32** | **8** | VarRegion | 36 | StaticGlobal |
| A | `gs.d[i]` | **FieldRegion** | **32** | **8** | VarRegion | 36 | GlobalInternal |
| A | `arr[1].d[i]` | **FieldRegion** | **32** | **8** | VarRegion | 144 | StackLocals |
| A | `o.in.d[i]` | **FieldRegion** | **32** | **8** | VarRegion | 36 | StackLocals |
| A | `p->d[i]` | **FieldRegion** | **32** | **8** | SymbolicRegion | `extent{p}` | Unknown |
| A | `g_noinit[i]` | VarRegion | 40 | 10 | VarRegion | 40 | GlobalInternal |
| A | `g_init[i]` | VarRegion | 40 | 10 | VarRegion | 40 | GlobalInternal |
| A | `heap[i]` | SymbolicRegion | `extent{conj}` | `extent/4` | SymbolicRegion | 同左 | Unknown |
| B1 | `t.d[i]`（非末尾字段） | **FieldRegion** | **32** | **8** | VarRegion | 36 | StackLocals |
| B2 | `x.m[1][j]`（二维字段） | — | — | — | — | — | **没有输出** |
| B3 | `f0->d[i]`（`d[]`） | FieldRegion | **Unknown** | Unknown | SymbolicRegion | `extent{f0}` | Unknown |
| B4 | `fz->d[i]`（`d[0]`） | FieldRegion | **Unknown** | Unknown | SymbolicRegion | `extent{fz}` | Unknown |
| B5 | `f1->d[i]`（`d[1]`） | FieldRegion | **4** | **1** | SymbolicRegion | `extent{f1}` | Unknown |
| B6 | `q[i]`（`int *q`） | SymbolicRegion | `extent{q}` | `extent/4` | 同左 | 同左 | Unknown |
| B7 | `ext_arr[i]`（`extern int []`） | VarRegion | **Unknown** | Unknown | VarRegion | Unknown | GlobalInternal |
| B8 | `lg[i]`（`long`） | VarRegion | 32 | 4 | VarRegion | 32 | StackLocals（`elem=long/8B`） |
| B9 | `h[i]`（malloc） | SymbolicRegion | `extent{conj}` | `extent/4` | 同左 | 同左 | Unknown |
| B10 | `p = &a[2]; p[j]` | — | — | — | — | — | **没有输出** |
| B11 | `p = a + k; p[j]` | — | — | — | — | — | **没有输出** |
| B12 | `p = &a[2]; *(p + j)` | VarRegion | 40 | 10 | VarRegion | 40 | StackLocals |

两个对照组只改变这几行：

| 对照组 | 变化 |
|---|---|
| ② `unix.DynamicMemoryModeling` | `heap[i]`、`h[i]`：`HeapSymRegion{…}`、**`dynamic=16 count=4`**、space=`HeapSpaceRegion` |
| ③ `consider-single-element-arrays-as-flexible-array-members=true` | `f1->d[i]`：`static=Unknown count=Unknown` |

### 你只需要回我四件事

```
T0-1b: ① 7 行字段数组（A 组 6 行 + B1）的 array= 是否都是 FieldRegion / 32 / 8
       ② B2、B10、B11 是否也没有输出
       ③ 对照组 ② 里 heap 是否变成 dynamic=16
       ④ 其余行与上表是否一致（不一致的贴原文）
```

**解读**：

* **① 全是 FieldRegion / 32 / 8** → **A1 成立**。ExtentModel 走"借 CSA 的规则、包一层"路线，
  按 `02-` 的新数字（10–12 人日）执行。
* **① 出现 Unknown 或打的是 36** → A1 在你们 fork 上不成立（fork 改过 `getStaticSize` 的
  `FieldRegion` 分支）。ExtentModel 回到 16 人日上限，`01-` §3.1 的"借用 CSA"那段要删。
* **② 与上游一致** → 下面三处盲区在你们 fork 上同样存在，设计里必须自己补（已写进 `01-` §3.1 的表）。

### 上游实测已经暴露的四处盲区（不管 fork 结果如何，设计都要处理）

**(a) 指向数组中间的指针，再用符号下标：CSA 连"访问的是哪块内存"都给不出。**【实测数据 + 源码确认】

B10 `p = &a[2]; p[j]` 和 B11 `p = a + k; p[j]` **一行输出都没有**——不是 extent 拿不到，
而是 checker 的 `checkLocation` 根本没被调用。根因在 LLVM 14 `StoreManager::getLValueElement`
（`Store.cpp`）：基址已经是一个 `ElementRegion`（`&a[2]` 就是 `Element{a,2}`），再加一个**非常量**偏移时，
直接返回 `UnknownVal()`；而 `ExprEngine::evalLocation` 对 Unknown 地址开头就 `return`，**不通知任何 checker**。
语义完全相同的 B12 `*(p + j)` 却正常——因为指针算术走的是 `SValBuilder::evalBinOp`，那条路会把两个下标加起来。

用 CSA 自带的 `alpha.security.ArrayBoundV2` 交叉验证：当 `j` 被约束成具体值（`if (j != 9) return 0;`）时
`p[j]` **能报**——所以受影响的恰恰是"下标还是符号"的情形。
**而那正是摘要要做的事**：`int g(int j) { int *p = &a[2]; return p[j]; }` 需要推出 `requires j ≤ 7`，
CSA 在这里什么也没给。→ AccessPath 必须自己把 `p = &a[2]` 表示成 `(a, +2)`，不能读 ExprEngine 的地址。

**(b) 二维数组：只到"整个数组"这一级，行级越界检不出；符号内层下标连地址都没有。**【实测数据】

B2 `x.m[1][j]` 也没有输出（同 (a) 的根因：`x.m[1]` 是 `Element{m,1}`，再加符号 `j`）。
内层下标是常量时有地址，但比较基准是整个 `m`（48 字节）：`ArrayBoundV2` 对 `x.m[0][5]`
（越出第 0 行、没越出整个 `m`）**不报**，对 `t.d[8]`（非末尾字段越界）**能报**。【实测数据】
→ `01-` §3.1 表里 #18"多维：逐维"这一行，**只能自己做，不能借 CSA**。

**(c) 柔性数组成员：三种写法三种结果。**【实测数据 + 源码确认】

`d[]` 和 `d[0]` → Unknown；`d[1]` 默认按"真的只有 1 个元素"处理（count=1），
只有打开 `consider-single-element-arrays-as-flexible-array-members` 才变 Unknown。
嵌入式老代码里用 `d[1]` 当变长尾巴的写法并不少见【推断】：默认配置下每个 `d[k>0]` 都会被当越界 → 误报；
打开选项 → 真正只有 1 个元素的数组又检不出。**更根本的是**：柔性数组的真实长度写在**调用者**的
`malloc(sizeof(S) + n * sizeof(T))` 里 —— 这是一个天然的跨过程问题，恰好是摘要能补、单函数分析补不了的。

**(d) malloc 的大小要开 `unix.DynamicMemoryModeling` 才进状态。** 你 T0-1 那次运行里没开（见上面 `SymRegion` 的判读）。
如果你们现有的越界规则想用到堆大小，先确认生产配置里这个 modeling checker 是开着的。

### T0-1c · 顺带确认一下：你们 fork 的 clang 认不认识 aarch64 ILP32（1 分钟，可选）

T0-1 输出里读指针 `p` 的那行是 `size=8`——那是因为 `-cc_json` 指向的是 BXPlugin 自己的构建目录（x86-64 主机）。
**真正分析业务代码时**，extent 是按 cdb 里的目标 ABI 算的。我在上游 clang 14.0.6 上实测：

```
-triple aarch64_be-unknown-linux-gnu_ilp32   →  __SIZEOF_LONG__ 8, __SIZEOF_POINTER__ 8   （按 LP64 处理）
```

也就是说**上游 LLVM 14 不认识 aarch64 的 ILP32 数据模型**——跟你在 `06-practice/03-…` §2.3 里
对 Infer 自带 clang-21 的结论一样。如果你们的 fork 构建出了 clang 可执行文件，跑一行：

```bash
<fork 的 clang> --target=aarch64_be-linux-gnu_ilp32 -E -dM -x c /dev/null | grep -E '__SIZEOF_(LONG|POINTER)__'
```

* 是 `4 / 4` → fork 带了 ILP32 补丁，没问题。
* 是 `8 / 8` → 你们**现有的** CSA 规则分析业务代码时也是按 LP64 布局算的。
  **影响面比听上去小**【推断】：数组下标 vs 元素个数的比较不受影响（两边都按同一个错误布局算，比值不变）；
  受影响的是**含指针 / `long` / `size_t` 的结构体字节布局**，以及源码里写死字节数、
  经 `char *` 按字节偏移访问、`memcpy_s(dst, 20, …)` 这类场景。这件事跟方案 C 无关，但值得知道。

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
| T0-1 | ✅ **对象层已完成**：已改 `01-` §3.1（新增 #21–#22、改写 #12/#17/#18 的 CSA 现状）、§5 A1 状态、§7；`02-` ExtentModel 10–16 → **10–12**、AccessPath 5–7 → **6–9** |
| T0-1b | 字段层复核。若 ① 不成立，ExtentModel 回到 16 人日、`01-` §3.1 借用 CSA 那段删除 |
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
