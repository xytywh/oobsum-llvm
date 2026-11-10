# SPEC-0135 分析层次与范围的源码审计和定量实验

状态：已实现
来源：用户要求明确MaxBlocks之外还有哪些参数/取舍影响调用层次和范围，而非只给概念性保证。

## 1. 设计
按实际已注册选项/源码常量/建模限制/资源区分，不能把主规格规划值当已经实现的参数。
MaxBlocks在export/inline检查单函数CFG getNumBlockIDs，0不限；只在solve修改不能恢复已跳过的函数体；不是整条链总块数。没有显式调用层数截断，InlineDepth是报告阈值。
增加scope_audit.py stats：逐TU读取FIR，只输出函数/块数分布、cfg_too_large/no_cfg计数、显式调用图/SCC与缺定义调用点、判断标签/原因/实际depth统计；不输出业务名字。可--fir-only减少IO。
增加bench：生成自有深链与宽CFG，使用同一引擎inline模式，独立WorkDir保存源/FIR/判定/选项stderr/计数JSON；对比深链每函数小但总块数大、单函数跨阈值时标签变化。统计墙钟含解析/IO，不伪称纯solve时间，不外推业务覆盖率。
自有bench显式inline并清除子进程OOBSUM_CONFIG继承，不污染业务WorkDir。
不修改算法、上限、默认策略或用例金标准。

## 2. 数据口径
FIR里被MaxBlocks跳过的函数只有一个占位块，不能把len(blocks)=1当真实CFG；flags单列，max/hist仅统计已lower函数。旧FIR无法恢复被跳过的原始块数。
调用图按目标和函数键去重，仅显式直接调用边；缺FIR函数体可能有模型，间接调用可能已被引擎解析，不能据这两个计数断言漏报。重复外部键只是风险信号，不必然不同定义。
SCC缩图最长边数不是递归运行时栈上限；无环直接图的最长边数也只是这份观察图。判断depth最大值不是整个程序最大调用栈。audit_complete只表示读入统计成功，不是精确分析覆盖率。
默认无业务名/路径/告警正文，--out落计数；错误不把丢文件当安全。业务实际覆盖范围仍需有效CDB/TU完整性及问题基准验证。

## 3. 源码审计项
已注册选项：Phase/MaxBlocks/模型和别名/回绕等改变分析集合或精度；InlineDepth/Policy/SuspectReasons/ReportFlexibleArray等改变报出；Subobject改变内层检查/定级；Dump不改变判定。
硬编码预算：循环头20更新、2遍收窄、守卫8、值情形4、局部关系8、子对象窗口4、不透明表达式深度4、SymOf/SymPtr关系推导递归深度8、结构拷贝标量叶枚举深度4、初始化最多64次显式标量store。均不是调用层数参数，效果按具体分支说明。
TimeLimitMs/MaxConds/AllocConfig/AllocIdentity/EvalMode/ReportL2/Verbose/CallTrace/Emit等主规格规划项当前未注册/未实现，不得建议用它们调当前checker。
递归环按unknown/recursion，缺定义/模型/未知目标、有限字段/别名/C++支持、混配置/重复外部定义、图/条件规模和宿主栈/内存/时间共同影响精度与可完成性；不承诺无限深或任意极端输入都完成。

## 4. 验证
统计合成FIR图（DAG/循环/多目标/重复键/占位CFG/间接及缺体调用），判断日志、错误/隐私输出；bench深链与宽CFG配对，用小规模lit守住机制，手工量化到256调用及实际5000阈值附近。
每次命令和stderr落盘，结果含实际CFG/flags/标签/depth与耗时。构建/lit/core/replay/csa/check-tree均通过后独立提交。

## 5. 规格与公司
I-18/N-01/N-09补充口径和测量入口；现有数值不变。COMPANY增加只计数的业务审计和自有实验命令，结果留公司，带回统计模板。
真实业务skip比例/最长已观察图/原因分布需用户运行统计，不能从缺少MaxBlocks命中推导100%覆盖或无漏报。

验收：实际简单链1/16/64/256调用边均DEFINITE；单函数5203块在阈5000为SUSPECT、6000为DEFINITE。计数观察保存benchmarks/scope-20261110.json。构建通过、lit120/120、core187、replay一致、csa回退0/TN强告警0、check-tree一致；engine_check保持14/16既有关系域差异。
