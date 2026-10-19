# OobSummaryChecker

> package / 运行名：`rb.enhance.ipa.OobSummary`（跨函数规则）
> 设计与每一步修改的依据：本仓库 `oobsum/06-spec.md`（主规格）与 `oobsum/specs/SPEC-NNNN-*.md`（增量规格）。

## 1. 规则目的

检测 **跨多层调用** 的内存越界（C 语言）：缓冲区在一个函数里定义，下标 / 长度 / 指针偏移经过三层及以上调用传下去，在最底层越界。
CSA 现有规则靠路径敏感的 inline，只覆盖 1–2 层；本规则用 **函数摘要**：自底向上为每个函数算出"访问要满足的条件"（Req），
在每个调用点把实参代进去判定，判不了就继续往上传，直到能判或到达入口。

## 2. 风险说明

越界写破坏相邻数据（结构体相邻成员、栈上其它变量、堆块头），越界读泄露数据或读到垃圾值，都可能导致崩溃或安全漏洞。
嵌入式业务代码里常见的形态：消息 / TLV 解析时长度来自报文、securec 函数（`memcpy_s` 等）的 `destMax` 或 `count` 用错、循环界写成 `<=`、
二维数组或结构体成员数组的一维越界（越出一行 / 一个成员，但仍在整个对象里）。

## 3. 检测策略

- [ ] AST-only checker
- [ ] 路径敏感 CSA checker
- [ ] LibTooling 预扫描 + CSA
- [x] 混合：只挂 `check::ASTDecl<TranslationUnitDecl>`（不挂任何路径敏感回调，不跑 ExprEngine），自己做过程内抽象解释 + 过程间摘要

流程：Clang AST / CFG → 中间表示 FIR → 每个函数的抽象解释（整数区间、界是形参等接口符号的线性式、守卫、内存格、子对象窗口）→
函数摘要（访问要求 Req、副作用 effects、调用点记录）→ 自底向上在调用点代换判定 → 标签 → CSA 告警。
默认单 TU 内推到底（`Phase=inline`）；跨 TU 用三阶段 `export → solve → report`（驱动 `oobsum/tools/oobsum_run.py`）。

每条判定带四种标签之一（告警前缀 `[OOB-<标签>[:<原因>]]`）：

| 标签 | 含义 |
|---|---|
| `DEFINITE` | 一定越界（最小的访问末端也超出大小） |
| `POSSIBLE` | 按代码给出的界（循环条件、检查、分支常数）可能越界 |
| `SUSPECT:<原因>` | 证明不了安全（原因如 `entry_unproven`、`type_range_only`、`untracked_pointer`、`subobject_overflow`、`alias_unverified`…） |
| `SAFE` | 证明了安全，默认不报，只进判定日志 |

**所有判定**（报了的、没报的及没报的原因）写进 `WorkDir/decisions/<tu>.jsonl`，供统计与大模型复核。

## 4. 支持场景

1. 下标 / 偏移 / 长度经多层调用传下去：数组下标、指针加偏移、结构体字段里的长度（含"调用方先写字段、被调者再读"）。
2. 库函数模型：securec `memcpy_s` / `memmove_s` / `memset_s`（`destMax` 规则、`count > destMax` 不写）、`strcpy_s` 族（至多写 `destMax`）、libc `memcpy` 等；
   分配器（返回的堆对象大小）；可用模型文件扩充（`ModelFile`）。
3. 循环：计数循环、递减循环、`!=` 结束、同余步长、指针步进、长度先夹到上限再循环（SPEC-0110）。
4. 守卫：`if (n > MAX) return;` 这类检查推出的条件随 Req 上传，调用方的值让它为假时访问走不到。
5. 效果：被调者写字段 / 全局、返回指针（含指向成员数组）、返回新堆对象、只在某个返回值下成立的效果。
6. 内层界（用户决定 D22 / D24）：多维数组的每一维、成员数组、结构体数组元素里的成员——越出内层、没越出整个对象的报 `SUSPECT:subobject_overflow`
   （选项 `Subobject=label` 时按常规判 DEFINITE / POSSIBLE）。
7. 全局数组与全局变量、静态函数同名、函数指针（目标唯一时）、递归（记账）、跨 TU（三阶段）。

## 5. 检测边界与取舍（TP/TN/FP/FN）

召回优先：**拿不准就报 SUSPECT（带原因），只有证明了安全才不报**；精度上的取舍都做成了选项（用户决定 D25），默认取检出更多的那一边。

| 象限 | 场景 | 处理 | 理由 |
|---|---|---|---|
| TP | 三层调用传下去的下标越界（`Tc_Engine_TP`） | 报 DEFINITE | 实参代进来后最小末端也超出 |
| TP | 循环界 `<=` 写错、经多层调用 | 报 POSSIBLE | 上界来自代码给出的循环条件 |
| TP | `a[0][5]`（`UINT8 a[2][4]`）、`st.aucBuf[20]`（成员 16 字节） | 报 SUSPECT:subobject_overflow | 越出一行 / 成员，没越出整个对象（D22） |
| TN | 同一调用链下标在界内（`Tc_Engine_TN`） | 不报（日志 SAFE:within） | 证明了安全 |
| TN | 调用方给的值让守卫为假 | 不报（日志 SAFE:guard_false） | 访问走不到 |
| FP | 入口函数自己的形参决定的访问、未跟踪的指针（数组元素里的指针、超过两层的字段）、只有类型范围的偏移 | 报 SUSPECT（原因写明） | 证明不了安全；交大模型复核，可用 `SuspectReasons` 按原因筛 |
| FP | `do { … } while (i < n)`（先访问后检查） | 被调者里报 SUSPECT | 头上的不变式 `i ≤ max(n − 1, 0)` 线性界表示不了（S7c-2） |
| FN | 1–2 层就能判定的 DEFINITE / POSSIBLE | 默认不报（日志 `inline_depth`） | 交给 CSA 现有规则（F-20，`InlineDepth=2`）；内层判定不受这条限制 |
| FN | 经 `char *` / `void *` 写改到别的类型的对象（`F((UINT8 *)&st, &st)`） | 默认可能判 SAFE | 取舍见 `CharAlias`：`true` 不漏但界丢得多，`verify` 在调用点核对（D20 / D25） |
| FN | `AssumeNoWrap=true` 时检查本身被整数回绕绕过 | 可能不报 | 默认关；只在确认不会回绕时打开（SPEC-0108） |
| FN | `Subobject=off` 时只越出内层的访问 | 不报 | 默认 `suspect` 报；`off` 只给"业务代码就是这样写"时用（D24） |

不支持场景：

> 不做跨循环的不变式推导（例如"第一遍校验过的总长 = 第二遍写入的总长"，LV8-02）；这类只记账，不判 POSSIBLE。
> 函数指针目标不唯一时不展开（记账 `fptr`）；浮点、内联汇编、`setjmp` 等不支持的构造给出的值当未知（记账）。

## 6. 示例

### 6.1 违规示例

```c
static void Tc_Leaf(TC_UINT8 *pucBuf, TC_UINT32 ulIdx) { pucBuf[ulIdx] = 0; }
static void Tc_Mid(TC_UINT8 *pucBuf, TC_UINT32 ulIdx)  { Tc_Leaf(pucBuf, ulIdx); }
static void Tc_Up(TC_UINT8 *pucBuf, TC_UINT32 ulIdx)   { Tc_Mid(pucBuf, ulIdx); }

void Tc_Engine_TP(void)
{
    TC_UINT8 aucBuf[16];
    Tc_Up(aucBuf, 16); /* 越界 1 字节，三层 */
}
```

预期告警：

```text
TC_OobSummaryChecker.c:79:1: warning: [OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain: up to byte 17 of 'aucBuf' (16 bytes) is accessed
TC_OobSummaryChecker.c:79:1: note: Call chain: Tc_Engine_TP (TC_OobSummaryChecker.c:79) -> Tc_Up (TC_OobSummaryChecker.c:73) -> Tc_Mid (TC_OobSummaryChecker.c:68) -> Tc_Leaf (TC_OobSummaryChecker.c:63)
```

### 6.2 合规示例

```c
void Tc_Engine_TN(void)
{
    TC_UINT8 aucBuf[16];
    Tc_Up(aucBuf, 15); /* 在调用点证明安全：不报，判定日志 "label":"safe","reason":"within" */
}
```

### 6.3 边界示例

```c
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
static void Fill(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { p[i] = 0; } }
void Arg_Tp(void) { CTX_S st; Fill(st.aucBuf, 20); }  /* 越出 aucBuf、没越出 st */
```

预期告警：

```text
warning: [OOB-SUSPECT:subobject_overflow] Cannot prove the write through a 1-level call chain stays within the array member or row (16 bytes) of 'st': up to byte 20 of it may be accessed
```

## 7. 测试覆盖

| 场景 | 预期 | 在哪 |
|---|---|---|
| 直接违规 (TP)、安全反例 (TN)、层数过滤 (B1)、引擎三层链 (E1 / E2) | 4 条告警、7 条判定（`Dump=stats`：`decisions=7 emitted=4`） | 本目录 `TC_OobSummaryChecker.c` |
| 各 SPEC 的功能与回归 | 103 个 lit 用例全过 | 本仓库 `clang/test/Analysis/oobsum/` |
| 67 个分级用例（Lv1–Lv10，TP / TN 成对） | 通过 108、通过+S 20、级别不同 2、超出预期 4；TN 入口零 DEFINITE / POSSIBLE | `oobsum/cases/`，`python3 oobsum/cases/run.py csa` |
| 摘要金标准（16 个用例） | 引擎推出的告警与金标准逐条一致 14 个（`AssumeNoWrap=true` 时 15 个） | `oobsum/cases/summaries/`，`oobsum/tools/engine_check.py` |
| core 单元测试 | 158 个全过 | `oobsum/unittests/`，`oobsum/devenv/core-tests.sh` |

公司侧用 bxen 跑 67 个用例并与本地对照：`oobsum/COMPANY.md` §3.1。

## 8. 注册与构建

由本仓库的 `oobsum/devenv/land.py` 同步（不要在 xir 里直接改，下次同步会覆盖）：

- 源码：`xir/llvm_ast/StaticAnalyzer/Checkers/OobSummaryChecker.cpp` 与 `OobSummary/**`
- 源码 CMakeLists：`xir/llvm_ast/StaticAnalyzer/Checkers/CMakeLists.txt` 的 `add_library()` 末尾追加
- Checkers.td：`xir/include/clang/StaticAnalyzer/Checkers/Checkers.td` 的 `rb.enhance.ipa` 包块最后（`def OobSummaryChecker`）
- 测试目录：`xir/llvm_ast/TestCase/OobSummaryChecker/`，`TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(OobSummaryChecker)`

```bash
python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
```

改了 `Checkers.td` 后新选项不生效，多半是 `Checkers.inc` 没重新生成：做一次完整编译。

## 9. 运行方式

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c \
  -checker=rb.enhance.ipa.OobSummary \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/oobsum-tc -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

本规则**不加** `-analyzer-config ipa=none`（它不走 CSA 的 inline，只挂 TU 级回调）。多文件用 `-analysis_folder=<目录>`。
`-analyzer-config` 的列表值用 `+` 连接（`Dump=options+stats`）。

选项（`-analyzer-config rb.enhance.ipa.OobSummary:<名>=<值>`）：

| 选项 | 默认 | 作用 |
|---|---|---|
| `Phase` | `inline` | `inline`（单 TU 推到底）/ `export` / `solve` / `report`（跨 TU 三阶段） |
| `WorkDir` | 空 | 判定日志、FIR、摘要、dump 的目录；三阶段必填 |
| `InlineDepth` | `2` | DEFINITE / POSSIBLE 层数 ≤ 它的不报（交现有规则）；`0` 全报 |
| `ReportSafe` | `false` | 也把 SAFE 当告警打出来（调试用） |
| `Policy` | `recall` | `balanced` 时 SUSPECT 不报 |
| `SuspectReasons` | 空 | 只报这些原因的 SUSPECT（`+` 连接）；空 = 全报 |
| `Dump` / `DumpFilter` | 空 | 中间数据：`options`、`fir`、`state`、`summary`、`apply`、`decisions`、`stats`…；按函数名正则过滤 |
| `ModelFile` | 空 | 业务的分配器、拷贝封装、纯函数（YAML，示例 `oobsum/config/oob_models.example.yaml`） |
| `MaxBlocks` | `5000` | CFG 块数超过它的函数不分析（记 `cfg_too_large`） |
| `CharAlias` | `false` | `char` / `void` 写是否可能改到别的类型的对象：`false` / `true` / `verify`（SPEC-0103 / 0106） |
| `Subobject` | `suspect` | 只越出内层的访问：`suspect` / `label` / `off`（SPEC-0105） |
| `AssumeNoWrap` | `false` | 检查与地址计算按数学整数、记局部线性关系（SPEC-0108） |

看结果：

```bash
python3 oobsum/tools/decisions.py <WorkDir>                         # 按标签 / 原因计数
python3 oobsum/tools/oobsum_run.py stats --workdir <WorkDir>          # 标签 × 层数、原因、热点函数
python3 oobsum/tools/oobsum_run.py explain --workdir <WorkDir> <函数名或指纹>   # 一条判定的来龙去脉（带 Dump=summary 跑可看链上摘要）
python3 oobsum/tools/decisions.py <WorkDir> --emitted --jsonl review.jsonl     # 给大模型复核
```

## 10. 备注

- 分层：`OobSummary/core/` 不依赖 Clang（FIR、抽象域、摘要、代换、求解、判定），`lower/` 只做 AST → FIR，checker 入口与 `report/` 两边都看。
- CSA 已知限制：上游 clang 14 不支持 aarch64 ILP32，本地用 `armebv7-linux-gnueabi`（4 字节指针 + 大端）近似；公司 fork 上的字节布局由 I0 探针确认（`oobsum/probes/i0-integration/`）。
- 后续：`do { … } while` 的头不变式（S7c-2）、跨循环关系（LV8-02，需 AI 辅助）、驱动的 `sqlite`；各选项的默认值按公司业务代码实测再定（`oobsum/STATUS.md`）。
