# SPEC-0136 按函数与调用目标回译来源，避免同basename串错调用链

状态：已实现
来源：继续开发；STATUS已记录SiteMap按basename取首的定位风险。自有root→b/common.h的Mid→a/common.h的Leaf实测，Mid note及判断chain误指到a/common.h，主DEFINITE仍正确。

## 1. 目标与范围
只改诊断位置归属，不改FIR/摘要中的短位置表达、推理公式、标签、指纹或函数目标解析策略。
保持普通唯一basename路径及旧FIR可读；不能通过改所有摘要位置字符串而无意改金标准或指纹。

## 2. 设计
新增纯core SourceMap：以实际选中的函数键及其模块/FIR loc索引定位；模块中basename唯一时直接用文件表，同名时按函数声明/指令位置核验候选。
调用链从已知上下文函数键出发，按已有summary.calls的site/callee_key向下约束；已知最终accessKey优先；旧数据无法沿边解析时按显示函数名候选核验，不选字典首个目标。
多函数候选只有定位到同一个规范路径时才确定。缺失/歧义用保留文件名的诊断占位路径<oobsum-unresolved>/basename表示，reporter显式拒绝把此占位当真实文件载入；warning仍报告，不能定位的note省略。
占位只写最终诊断Site，永不写入规范FIR/摘要语义。它不是新增判断标签。sink的diag编号映射仍沿用SPEC-0125，可覆盖为已知准确位置。
旧FIR或缺少diag编号时，按短位置找访问元数据的兜底也须检查候选文件唯一；跨文件歧义时不取首个候选充当sink，保留原判断及来源不可用说明。

## 3. 调试
沿用Dump=diagnostics，提供source-locations.jsonl，记录context/function/role/source/status/candidates/resolved；DumpFilter按上下文或当前函数筛选。
core仅在请求回调时构造调试事件；默认不保存额外轨迹。stderr仍以diagnostics开头，独立schema区别于判定。

## 4. 召回与精度
主判断、原始指纹和报出策略不因位置不明而消失；无从定位时保留warning并说明不可用，不伪造文件路径。普通唯一位置回归不变，已错指文件的位置有意修正。
这是诊断归属修复，不实现多产品同名符号解析，不宣布公司全部差异已解决。

## 5. 验证
纯core单测：同名文件不同函数、函数内片段、规范路径别名、缺失/歧义不猜、函数名重复/函数键消歧。
lit：同TU两个同名头文件与跨TU同名源文件、相同函数名不同键的链；主标签/指纹保持，所有可证位置正确；旧数据/真正歧义不发错误note，Debug/Filter可落盘且不改结果。
构建、全lit、core、replay、csa、check-tree，金标准不降低。
结果：构建成功；lit121/121、core191/191、replay0处不一致、csa回退0/TN强告警0；check-tree29源/30头一致。模拟落地幂等，conventions31PASS/1既有WARN/0FAIL。

## 6. 主规格
D-06/A-91/F-33/N-06：诊断路径必须基于函数/FIR来源，不能按全局basename首个回译；不能确定的位置显式标记，不用任意首项。

## 7. 公司
COMPANY更新版本及输入/输出预期，合成回归先通过；业务原始JSON和路径留公司，只反馈计数/异常类型。
