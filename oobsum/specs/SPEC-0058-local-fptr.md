# SPEC-0058 S9.1：只被赋过函数地址常量的局部函数指针当直接调用；多个目标时只判 POSSIBLE

状态：已实现
来源：06- A-60 ①（间接调用 → 被调表达式是定义式为 `faddr` 常量的局部时当直接调用，S9.1 尽力）；`run.py csa` 最后一个漏报 LV9-01（年内尽力，期望 L2）：

```c
CAP_PROC_FUNC pfnProc = (ulRat == RAT_NR) ? Nr_SaveCap : Lte_SaveCap;
pfnProc(pucBuf, ulLen);
```

用例头：`pfn` 在本函数里只可能是 `{Nr_SaveCap, Lte_SaveCap}` → 两个摘要都代入；TP 在 NR 分支上越界 → L2（依赖制式）。SPEC-0057 之后这里记 `fptr`。

## 2. 设计

### 2.1 解析（`Analyzer::Run` 前的预扫描）

每个局部的全部赋值（`set`）都是函数地址常量（`FAddr`，可隔着类型转换）或已解析的局部 → 它可能指向的函数集合（不动点）；被 `load` / `call` / `havoc`
赋过、或是形参的局部不解析。间接调用的被调表达式（可隔着类型转换）是已解析的局部 → 目标集合。

### 2.2 调用点

* **一个目标**：当成对它的直接调用（摘要、效果、mod-ref、调用点记录全部照旧）。
* **多个目标**：对每个目标各记一条调用点记录，标 `alt: true`；状态上只按各目标的 `mods` 失效（有目标没摘要时照旧保守），**不应用**返回值 / 出参效果（不知道执行的是哪个），
  返回值未知。目标里有模型函数（`memcpy` 等）时不按多目标处理，照旧 `fptr`。
* 解析不出：照旧记 `fptr`（SPEC-0057）。
* 解析出来的调用点记录都带 `ind: true`（只给调试与量尺看，不影响判定）。
* 自底向上的顺序（`Engine.cpp` 的 `BottomUp`）：函数里取了地址的函数也先算，这样解析出的被调者已有摘要（顺序只影响精度：没摘要时按保守失效）。
* 空指针初值（`pfn = NULL; if (x) pfn = A;`）不算目标：经空指针调用是未定义行为，与越界无关。

### 2.3 `alt` 调用的判定（A-62）

代换时把末端**下界当成 0**（这次可能执行的是另一个目标，访问根本不发生）：于是不会判 L1，末端上界越界且来自代码给出的界时判 L2，上传的 Req 下界也是 0。
C++ `apply::Classify` 与 `replay.py` 的 `classify` 同步。调用点记录的 JSON 加 `alt`、`ind`（schema、读写）。
这样判出的 L2 来源标签是 `exact`（实参确切，只是不一定执行这个目标），告警文本"the bound comes from the call arguments"（`Alert.cpp` 与 `replay.py` 的短语表同步）。

### 2.4 量尺（`run.py csa`，SPEC-0002 §2.4 修订）

基线里调用点的 `fptr` 记账这次不再报出、而同一函数同一行有带 `ind` 的调用点记录（解析出了被调者，目标里的访问改由调用链判定）→ 不算回退。

## 3. 对召回与误报的影响

**召回**：本函数里按条件选处理函数的写法，目标函数的越界判到调用链上（原来只有 `fptr` 记账）。不会把"只在某个目标里越界"报成 DEFINITE。

## 4. 实现位置

`core/Interp.cpp`（`ResolveFnLocals`、`TargetsOf`、`AltCall`、`Clobber`（从调用点失效的代码提出来）、`RecordCall`）、`core/Summary.{h,cpp}`（`Call.alt` / `ind`）、
`core/Apply.cpp`（`Classify`）、`core/Engine.cpp`（`BottomUp`）、`core/Alert.cpp`（短语）、`replay.py`、`summary.schema.json`、`oobsum/cases/run.py`（§2.4）。

## 5. 测试

* lit `local-fptr.test`：两个目标（LV9-01 同形）→ TP POSSIBLE、TN SAFE，`Dump=summary` 的两条 `alt` / `ind` 调用点记录、没有 `fptr`；一个目标（带空指针初值）→
  当直接调用（DEFINITE）；对照：函数指针来自形参 → 照旧 `fptr`。
* 单测 `Interp.LocalFnPtrResolved`：两个目标（隔着中间局部）、一个目标（空指针初值 + 类型转换）、形参与 load 来的（`fptr`）。
* `run.py csa`：LV9-01 从"漏报"变成"通过"（TP POSSIBLE、TN SAFE），漏报 0；回退 0（§2.4）。`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`calls[].alt`）、`Dump=apply`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| A-60 ① | 间接调用 → 定义式为 `faddr` 常量的局部时当直接调用 | 目标多于一个时每个目标一条 `alt` 调用点记录，判定时末端下界当 0（只判 L2） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 结构体里的函数指针字段、全局函数表（`g_astOps[i].pfn`）的解析。
* 调用只在函数指针非空时发生（`if (pfn != NULL)`）这类指针守卫不进调用点守卫：判定时按调用会发生（偏多报，同直接调用）。
