# 调用层次、分析范围与可量化边界

审计基准：2026-11-10实际源码，SPEC-0135。这里讨论当前实现；06-spec中规划但未实现的选项不能当作现有能力。业务统计留在公司，本文只记录自有合成实验。

## MaxBlocks到底限制什么

`lower/Lowering.cpp::LowerBody`检查的是当前函数的`CFG::getNumBlockIDs()`：`maxBlocks > 0 && blocks > maxBlocks`才跳过该函数体。
默认5000，恰好5000允许，5001超限；0关闭这个保护。它不累计调用者/被调者的块数，不限制函数总数，也不是调用栈层数。
超限函数以`cfg_too_large`标记、一个占位块导出；`core/Interp.cpp::Analyzer::Run`返回`skipped:cfg_too_large`，引擎记录`SUSPECT:unanalyzed:cfg_too_large`。该条账本不等于已枚举函数内每一次真实访问。

5000对大量手写小函数可能不触发，但不能根据经验认定它永远无效。宏展开、生成代码、大switch/大量条件都能产生大CFG；先统计这批输入的标记命中数。
MaxBlocks主要在export/inline降级时生效。若FIR已经用占位块替代大函数，仅在solve里改成6000或0不会补回函数体；必须用新参数重新export到新WorkDir。
命中0只说明**已读FIR中的函数未因该限制跳过**，不说明缺失TU已覆盖、其它模型均精确或业务漏洞召回率100%。

## 有没有另外一个调用深度参数

当前没有“最多展开N层”的摘要调用深度截断。调用点代换Req/效果、向上传递，有限的非递归直接链可以继续计算；没有每个入口逐层执行整条路径。
`InlineDepth`默认0，是已得判断的报告过滤阈值；设置2会把浅层DEFINITE/POSSIBLE留日志而不发warning，SUSPECT不按层过滤。它不是引擎停止于第2层。
公司命令中的`ipa=none`和`-analyzer-max-loop 3`控制CSA ExprEngine，不能据此推断本规则只分析0层或循环3次；本规则使用自己的摘要引擎。

“没有显式深度截断”不等于无限资源或任意深都精确。调用图/CFG遍历有递归实现，仍受宿主栈限制；FIR/状态/摘要/Req/证据链占内存，密集图、广分支及无法合并的要求会增大成本。极端输入仍可超时、耗尽内存或栈，不能承诺一律优雅变成SUSPECT。
递归函数另有模型边界：SCC环内调用按未知处理并记`recursion`，不求递归语义的不动点，不保证任意递归次数的精确结论。

## 当前能调的选项分别影响哪一层

以下均可在Checkers.td与注册代码核对，数值以本轮Dump=options为准。

| 项 | 当前默认/范围 | 影响 | 不应怎样理解 |
|---|---|---|---|
| Phase | inline；export/solve/report | inline只用本TU；三阶段可跨成功导出的TU | inline是TU内摘要，并非打开CSA内联 |
| MaxBlocks | 5000；0不限 | 单函数CFG过大则skipped/SUSPECT | 不是调用链累计块数/深度 |
| ModelFile | 空=内置表 | 可提供分配/访问/free/pure契约，需与源码真实行为匹配 | 未知函数不能随便设pure |
| CharAlias | false/true/verify | 外部char/void写的别名失效假设及核验；影响精度与健全性 | false可能把旧值错误用于证明安全；true也不是全语言无漏报保证 |
| AssumeNoWrap | false | true启用不回绕假设及局部关系推导，可能更精确 | 检查本身可回绕时，true可能漏问题 |
| Subobject | suspect/label/off | 内层子对象边界的处理；off只查整个对象 | off不等于内层安全，可能失去内层问题 |
| Policy | recall/balanced | balanced不发SUSPECT，判断仍保留日志 | 少warning不代表多证明了安全 |
| InlineDepth | 0 | 过滤已经得到的浅层D/P warning | 不是分析最大层数 |
| SuspectReasons | 空=全部 | 按原因选择SUSPECT warning | 未选原因不代表已支持/已安全 |
| FlexibleArrayMax / ReportFlexibleArray | 4 / false | 尾数组约定的报告策略，原判断留存 | 约定可能过滤真实小分配越界，不是SAFE证明 |
| ReportSafe | false | SAFE是否也发warning | SAFE日志本来就保留 |
| Dump / DumpFilter | 空 | 观察中间事实/保存范围 | 不改变逻辑判断；IO和资源成本仍存在 |

**源码核对说明：** 当前注册表没有`TimeLimitMs`、`MaxConds`、`MaxGuards`、`MaxInlineDepth`、`AllocConfig`、`AllocIdentity`、`EvalMode`、`ReportL2`、`Verbose`、`CallTrace`、`Emit`等调参入口。
06-spec的TimeLimitMs=5000、MaxConds=64是规划值，当前不能传入调节，也没有“每函数5秒到点自动停止”的实现。不能据规划的64条要求上限估算当前内存。

计算期选项（模型、CharAlias、AssumeNoWrap、Subobject等）改变后须重新solve/inline；仅重新report不能改写既有判断。报告过滤选项主要在report/inline生效，可以复用相同solved观察报出变化。

## 不可直接调的主要预算

这些单位与调用层数不同；用源码函数/常量名定位，避免把一个局部限制理解成全函数停止。

| 实现位置 | 当前值/条件 | 实际效果 |
|---|---|---|
| Interp `kMaxHeadUpdates` | 每循环头状态更新超过20次 | `partial:iter_limit`及SUSPECT账本；不是源循环最多执行20次 |
| Interp `kNarrowPasses` | 2遍 | 加宽后收窄精度受限，不是只分析2层函数 |
| Interp `kMaxGuards` / AddGuard | 每状态8条，超出移除最旧 | 丢约束，可能更难证明；不限制源码条件数/路径数 |
| Interp `kMaxCases` | 每值4个情形 | 条件返回值/分支值精度可能合并退化，不是全程序只看4条路径 |
| Interp `kMaxRels` | 8条局部线性关系 | 关系域有界；不表示其它普通区间/线性式都停止 |
| Interp `kMaxSubs` | 每指针最多4个内层窗口 | 丢较外层子对象窗口，全对象仍查；不能宣称任意多维每层边界都精确 |
| LinExpr `kMaxOpaqueDepth` | 不透明项嵌套深度4 | div/mod/wrap/mul嵌套超限可未知；不是调用深度 |
| Interp `SymOf` / `SymPtr` | 深度计数>8返回None | 关系推导的符号提取失败；不是所有深表达式都不求值 |
| Interp `ScalarLeaves` | 递归计数>4停止枚举 | 结构体整体拷贝的部分字段值效果不精确；整体访问检查仍有 |
| Lowering `kMaxInitStores` | 每对象最多64次显式标量初始化store | 后续/隐式零/字符串内容不逐项跟踪，值可未知；不是只查前64个元素访问 |

还有整数位宽/系数溢出、不透明非线性、可表达的字段/指针模式等域边界。并非每个未知都对应一个预算命中；`type_range_only`不能反推“必定因为守卫超过8条”。
字段跟踪支持形参对象、部分指针字段目标等有限模式；不是任意多次解引用的通用堆图。深字段链、数组元素中的指针、C++的this等可落入untracked_pointer/其它SUSPECT，局部已知值仍可能证明一些访问。
缺函数体/模型、无法解析的间接调用、C++成员/构造/虚调用、复杂别名、数据驱动循环等都可能成为精度边界。未知接口的SUSPECT是风险账本，不是已经分析出被调者内部每个sink。

## 输入组织本身也是覆盖前提

有效TU必须成功导出；不同ABI独立求解；同Tu不同有效FIR在SPEC-0133中被拒绝，不能任意选一个。它也可能是合法的同程序多变体构建，拒绝表示当前存储/解析模型不支持，不代表业务程序错误。若变体属于同一程序并互相调用，不能任意拆开后就宣布完整覆盖，需独立设计多变体支持。
跨文件同名外部函数/全局的当前实现仍有first-wins选择，不能把不同产品的定义混成一个程序后承诺正确；源文件不同并不意味着已自动识别真实链接域。
来源定位已由SPEC-0136改为按函数键、调用目标及FIR位置核验，避免Engine::SiteMap同basename取首串错文件；T1另处理附加分配note。
这仍不是全FIR来源字符串完整路径化：旧短位置或同一函数中同名片段的相同行仍可能无法消歧，会显式标记来源不可用、保留主判断并省略错误note；Dump=diagnostics可在inline/solve留存来源轨迹。
规范D-07的最长公共目录解析尚未完整实现；这是实际覆盖边界，不能拿计划文字当已经具备。CDB完整不等于调用目标/模型契约完整。

所以专家讨论应分别列：输入覆盖（有效文件/变体/ABI/定义归属）、建模覆盖（访问/对象/效果/约束）、结果精度（D/P/S/SAFE）、报告范围（被过滤的判断）与资源可完成性。
已有自有基准中run.py csa保持回退0，并不代表所有人工金标准均已达到：engine_check仍为14/16（LV3-07/LV8-01已有关系域精度缺口）。不能把回退0当作业务检出率100%。
只有真实问题标注和正反例才能估计检出率/误报率。SUSPECT比例、场景等级、图最长路径、MaxBlocks未命中都不能代替业务漏洞召回率。

## 已完成的自有量化实验

自有bench清除子进程继承的OOBSUM_CONFIG，避免被业务Phase/WorkDir重定向；仅使用明确的测试配置。
同一LLVM14 Release摘要引擎inline模式，armebv7大端/4字节指针；2026-11-10单次观察。耗时含解析、分析、IO与结果统计，不是纯solve，也不与覆盖不同的CSA路径执行直接算加速倍数。

| 合成形态 | MaxBlocks | 最大已降级单函数CFG / 总块数 | 结果 | 观察墙钟 |
|---|---:|---:|---|---:|
| 1条调用边 | 3 | 3 / 6 | DEFINITE，depth=1 | 0.017s |
| 16条调用边 | 3 | 3 / 51 | DEFINITE，depth=16 | 0.018s |
| 64条调用边 | 3 | 3 / 195 | DEFINITE，depth=64 | 0.034s |
| 256条调用边 | 3 | 3 / 771 | DEFINITE，depth=256 | 0.069s |
| 浅链、单函数67块 | 16 | 大函数被跳过，真实块数不在该份FIR中 | SUSPECT cfg_too_large | 0.017s |
| 同一67块源码 | 512 | 67 / 70 | DEFINITE，depth=1 | 0.017s |
| 浅链、单函数5203块 | 5000 | 大函数被跳过 | SUSPECT cfg_too_large | 0.067s |
| 同一5203块源码 | 6000 | 最大5203块 | DEFINITE，depth=1 | 0.333s |

前四行证明该简单形状至少可到256层，且**总块数大于3不会触发单函数MaxBlocks=3**；不是任意256层复杂业务的全覆盖证明，也不是引擎上限256。
后四行把调用层次固定很浅，只改变单函数块数/阈值，明确二者独立。5000块确实可触发，不代表它在业务中常见。
本轮计数观察保存在[benchmarks/scope-20261110.json](benchmarks/scope-20261110.json)，是历史测量而非需逐时间字节匹配的金标准。
原始自有源、每次命令、stderr、FIR、判断和results.json由bench落盘；脚本可以重复测，但每次使用新输出目录，超时/失败不能算SAFE。

## 公司如何量化这批数据

完整命令在COMPANY.md §0。`scope_audit.py stats --fir-only`先只统计FIR：

* raw按原始函数定义（含重复）统计，unique按目标/函数键取首去重；max/hist采用raw中已降级函数。重复外部键：后者是需检查的信号，同一头文件/ODR相同定义重复也可能正常，不直接认定不同实现。
* cfg_too_large/no_cfg分别计数；max/hist只计已降级函数，跳过的占位块不混入。旧FIR没有被跳过函数的原始CFG大小。
* 显式直接图的SCC/最长边数、间接调用点和缺FIR体调用点；缺体可能已有模型，间接可能被引擎解析，不能把两者计为漏报。
* 默认再读decisions时，给出标签、SUSPECT原因、未报原因、实际depth/eff_depth最大值；这只是已有判断的观察口径。未知类别归other，不输出业务名。

统计逐TU处理，图只保留键/边，不常驻全部FIR；但全量仍有IO/内存成本。--out保存计数JSON可带回，业务原FIR和判断正文不带回。
`audit_complete`只表示统计读取成功，不表示函数都精确分析或所有业务TU都导出了；输入完整性先按CDB与FIR核对。inline实验留存FIR时pending可以为true；公司三阶段正常完成solve后应为false，不能用统计成功来替代阶段完成。
