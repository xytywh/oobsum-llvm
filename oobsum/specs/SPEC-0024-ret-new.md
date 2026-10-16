# SPEC-0024 返回新对象的效果（`ret_new`）：自底向上解释，封装分配器逐层推出

状态：已实现（S7d-3）
来源：`STATUS.md` S7d-3 / S8c-2；06- D-24（效果 `ret_new`）、D-33（分配器 / 封装分配器的返回值：模型表的 `size` 代入实参，封装函数由 `ret_new` 逐层推出）、
A-63…A-65（效果的代入）、A-80（自底向上）；金标准 `LV4-01`（`Pub_MemAlloc` → `Cell_MemAlloc` → `Ue_AllocTbl`，`eff_depth` 1 / 2 / 3）。

## 1. 要解决什么

业务代码里分配几乎都包了几层（`Ue_AllocTbl` → `Cell_MemAlloc` → `Pub_MemAlloc` → `VOS_MemAlloc`）。S7d-1 只认模型表里的分配器，
经封装函数拿到的指针是"未知"，访问一律记账；LV4-01 TP（512 字节的表按 64 项初始化）报不出 L2。

## 2. 设计

### 2.1 效果的产生（被调者）

函数解释完（记录那一遍）看它所有的 `ret`：返回值要么是**同一个堆对象**（基址 `heap@…`、偏移 0、大小已知），要么是空指针常量——
这个函数的效果是 `{kind: ret_new, size: <堆对象的大小，用本函数的接口符号写>, maybe_null: true, eff_depth: <堆对象的 eff_depth> + 1}`。
堆对象来自模型表的分配器时 `eff_depth` 为 0（`Pub_MemAlloc` 的效果是 1）。其余情形没有 `ret_new`。

### 2.2 效果的代入（调用方）

调用一个本 TU 里定义了的函数、它有 `ret_new`：返回值是新的堆对象 `heap@<本调用点>`，大小 = `size` 代入实参（区间取下界，同 SPEC-0018 §2.3），
`eff_depth` 取效果的值。指针实参是这种堆对象时调用点记录写 `eff_depth`（D-25，金标准同）。

### 2.3 自底向上

`RunInline` 按 FIR 的调用图（直接调用）自底向上解释：先被调者、后调用方（DFS 后序；递归成环的按任意顺序，环里还没算出的被调者当没有效果）。
`Dump=summary` / `Dump=state` 改为取 `RunInline` 的结果（与判定用的是同一份摘要）。

## 3. 对召回与误报的影响

经封装分配器得到的对象从"未知"（记账 `untracked_pointer`，SUSPECT）变成大小已知：能判 L1 / L2 / SAFE。只有"每条返回路径都是同一个堆对象或空指针"才成立，
其余照旧未知。

## 4. 实现位置

`core/Interp.{h,cpp}`：`PtrVal::effDepth`、`FunctionFacts::effs`、`AnalyzeFunction` 的 `callees`、`Ret` 的记录与效果、调用点代入、`RecordCall` 的 `eff_depth`；
`core/Engine.cpp`：自底向上顺序、`traces`；`OobSummaryChecker.cpp`：`Dump=summary` / `state` 用引擎的结果。

## 5. 测试

* 单元测试 `InterpTest.RetNewEffect`：模型分配器 → `ret_new`（`eff_depth` 1）；有空指针分支的仍是 `ret_new`；返回两个不同对象的没有。
* 单元测试 `EngineTest`：两层封装后访问越界 → L1。
* `engine_check.py`：LV4-01 一致（告警与 `alerts.json` 相同）。
* lit `ret-new.c`：`Dump=summary` 的 `effs` 与调用点的 `eff_depth`。
* `run.py csa`：LV4-01 TP → POSSIBLE（通过）、TN → SAFE；LV4-02 TP → DEFINITE（通过）；LV7-01（柔性数组）TP → DEFINITE（`later=L1`）、TN → SAFE，
  `List_New` 第 37 行写表头原来是 `untracked_pointer`，现在当场证明安全。**对照尺子的修订**（SPEC-0002 §2.4）：没有 kind 的账本判定，
  同一处有任意 kind 的 SAFE 就算证明了（账本判定不带 kind，SAFE 带）。

## 6. 调试

`Dump=summary` 的 `effs`；调用点实参里的 `heap@…`、`eff_depth`。

## 7. 对主规格的修订

无（按 D-24 / D-33 实现）。

## 8. 公司侧验证

I5（`VOS_MemAlloc` 的字节数是第几个形参）仍待确认；本步按 06- 的默认 `p2`。

## 9. 待定 / 以后

* `ret_cond`（校验函数的返回值，LV4-06）、`ret_int`、`field`（LV5-01）效果；分配器的出参（`result` 不是 `ret` 的）。
