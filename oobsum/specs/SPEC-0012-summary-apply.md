# SPEC-0012 摘要的读写（`core/Summary`）与调用点判定（`core/Apply`），在 104 份金标准摘要上对照 `replay.py`

状态：§2.1（读写）已实现（S8a-1）；§2.2–§2.4（调用点判定、核对、`Dump=apply` 的文本）随 S8a-2
来源：`STATUS.md` S8a；06- §5.3（D-20…D-30，摘要格式 `oobsum-summary/1`，D-30：机器字段与 `doc`）、§6.6（A-60…A-64 调用点五步）、
F-10…F-22（判定）、T-06（金标准：`cases/summaries/` 去掉 `doc` 的机器版）；参考实现 `replay.py`（`load_case` / `strip_doc` / `check_shape`、
`build_map`、`subst_obj`、`eliminate_locals`、`classify`、`rep_of`、`apply_eff`、`replay` 的逐调用点核对）。SPEC-0011 §1 的顺序调整。

## 1. 要解决什么

跨函数判定的核心是"被调者的摘要代进调用点之后怎么判"。`replay.py` 已在 104 份手写的摘要上把它验证过（"① 逐调用点核对：0 处不一致"）。
把它移植成 C++，并在同样的 104 份摘要上**逐调用点得出同样的结论**，引擎（S7 起）产出摘要后就能直接接上；这一步不需要解释器，先把最核心的
判定逻辑钉死。

## 2. 设计

### 2.1 摘要的读写（S8a-1）

* `summary::Summary` 等结构逐字段对应 `summary.schema.json`（只含机器字段）；表达式、条件仍存成字符串（规范形式由 `LinExpr` 负责，比较时解析）。
* `ReadSummary(json, out, error)`：**整棵忽略键名为 `doc` 的内容**（D-30）；其余严格——缺必有键、多出不认识的键、类型不对、表达式 / 条件解析不了
  （`ParseLin` / `ParseCond`）都报错，错误带位置（`calls[1].args[0].have: …`），不 `assert`（A-96）。可选键记下"有没有"。
* `SummaryToJson(s)`：写回机器字段，可选键只在读入时有的情况下写。**读入再写出，与去掉 `doc` 的原文件（解析后）完全相同**——这保证结构没有漏字段。
* 文件读写：`ReadSummaryFile` / `WriteSummaryFile`（先写 `.tmp` 再改名）。

### 2.2 调用点判定（S8a-2，`core/Apply`）

逐项移植 `replay.py`：`BuildMap`（实参 → 代换表：整数实参、指针实参的 `ext`、字段、别名、`eff_depth`）、`SubstObj`（`*pK` / `*pK->f` / `pK->f`
→ 调用方的对象与 `via`）、`EliminateLocals`（调用方局部换成它在调用点的区间，收集标签与非空约束）、`Classify`（守卫在此为假 → 安全；
末端常数 → 安全 / L1 / L2 / 记账；守卫蕴含 → 安全；守卫界判定 F-12b；全是接口符号 → 上传；否则记账 `entry_unproven`）、`RepOf`（判定记录与指纹 F-34）、
`ApplyEff`（`ret_new` / `ret_int` / `ret_cond` / `field`）。

### 2.3 逐调用点核对

`CheckCase(摘要们)`：按 `replay.py` 的 `topo` 顺序，对每个调用点、被调者的每条 Req / 效果，算出结论并与调用方 JSON 的 `results` 比：结局、上传的 Req
（`have` / `end` / 守卫 / `depth` / 对象）、判定记录的机器字段、账本原因、应用后的效果；入口函数的 Req 要有账本。返回不一致的清单。

### 2.4 `Dump=apply` 的文本

每个调用点、每条结论一行，**与 `replay.py --dir <机器版> <用例> -v` 的输出逐行相同**（SPEC-0001 §6.1："同 `replay.py -v` 的格式"）。
引擎接上后 `Dump=apply` 打的就是它。

## 3. 对召回与误报的影响

与 `replay.py` 逐项一致；它的判定规则就是 06- F-10…F-22 + SPEC-0001 的标签（`open` 以后对应 SUSPECT）。本步不接进 checker。

## 4. 实现位置

`OobSummary/core/Summary.{h,cpp}`（S8a-1）、`OobSummary/core/Apply.{h,cpp}`（S8a-2）；不依赖 Clang。

## 5. 测试

* S8a-1 单元测试 `SummaryTest.cpp`：读入 `cases/summaries/` 的全部 104 份摘要（`doc` 被忽略）、再写出，与去掉 `doc` 的原文（解析后）相同；
  坏输入逐条报错（缺键、多键、类型不对、表达式 / 条件解析不了、实参形状不对）；文件读写。
* S8a-2 单元测试 `ApplyTest.cpp`：16 个用例逐调用点核对 0 处不一致；`Dump=apply` 的文本与 `replay.py -v`（机器版）逐行相同（金标准由脚本生成）。

## 6. 调试

S8a-2 的核对失败时打印与 `replay.py -v` 同格式的行与不一致的字段；`python3 replay.py <用例> -v` 对照。

## 7. 对主规格的修订

无（实现 D-20…D-30 与 A-60…A-64 的既定写法）。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* S8b："从局部事实推到底"（`replay.py` 的 `derive`：自底向上重推全部上传 Req、判定、入口账本，最终告警与 `alerts.json` 逐条相同，T-06 后一半）。
