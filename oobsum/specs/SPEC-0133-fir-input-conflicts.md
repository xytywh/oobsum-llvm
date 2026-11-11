# SPEC-0133 阻止FIR编译变体静默覆盖

状态：部分由SPEC-0139取代（三阶段不同内容固定选择并留存，用户接受漏未选变体）；inline及真正输入失败保护保持。发布观察另由SPEC-0137修订
来源：T2自有CAP=8/32并行export实验证明同一路径TuId覆盖后可在DEFINITE与SAFE之间变化；SPEC-0118原子rename仅防半文件，SPEC-0124明确多变体边界尚未处理。

## 1. 目标与边界
正常单变体仍用原TuId和fir/<tu>.json，不改变函数键/判定/金标准，也不增加一套全量FIR副本。
不同编译变体不能互相覆盖，更不能通过字典首选一个变体掩盖丢失。当前求解域没有多产品/多配置的完整调用解析，本步检测冲突并拒绝继续，而非伪称能同时正确分析所有配置。

## 2. 不可覆盖发布
core新增独立的exclusive文件安装助手：createUniqueFile创建同目录完整候选，关闭后create_hard_link到目标；目标存在返回Existing并保留候选，禁止rename覆盖降级。Installed后只unlink候选，不能修改inode；错误保留候选及错误信息。
FIR写者遇Existing时解析并规范化已有FIR，与本次序列化比较：相同则幂等去重并移除候选；不同则先建立fir-conflicts/<tu>/持久目录，再按内容hash保存已有/新变体快照；空冲突目录也属于错误。完成快照后可清理候选。
任何创建/比较/保存/清理失败均报错，不静默继续。未能保存完整冲突证据时保留候选；hash目标存在且内容不同不得覆盖。

## 3. 阶段保护
每次导出FIR前标记根目录inputs-pending目录；solve单进程检查冲突目录、fir中遗留*.tmp及扫描错误，异常则failed快照+编译诊断错误，不运行引擎、不写新solved。
完整solve且solved写入成功后才移除inputs-pending。report用O(1)检查pending/冲突根目录，存在则错误，不读旧solved；避免每个report TU扫描全部FIR。
阶段必须串行：等待全部export任务成功才solve；同一批所有进程使用同一版本；不支持export/solve/report阶段重叠。解析AST前就失败的任务仍须由clang_check成功/失败统计保证完整性。
旧干净FIR保持可读，但无法恢复旧版本已覆盖的变体。源码/宏/ABI/选项/版本变化或已有冲突时使用新WorkDir，禁止删冲突后沿用某个winner。

## 4. 可观测
Dump=inputs及必要错误时记录输入状态，落WorkDir/dump/inputs.json：状态、冲突TU数、遗留候选数、待求解标记等；不复制源码。冲突FIR仅在公司打开，不带回。
错误必须让原生clang返回非0；驱动不以普通warning或done冒充成功。保留进度failed与原始输入证据。

## 5. 召回
不改变有效单变体域的判定。混合输入属于未成功分析的错误，不输出/宣称SAFE；按真实产品/构建配置拆分后重新export。完整多变体并存分析需独立设计，不能只改文件名。

## 6. 实现与验证
AtomicFile helper纯逻辑单测：新安装、目标已存在不覆盖、唯一候选、失败/清理行为；checker writeFir及solve/report阶段门禁。
lit合成同文件不同CAP，正反导出次序均保留冲突证据并阻止solve/report；相同FIR重复导出正常，已知越界不丢；候选残留/空冲突目录/pending门禁。T1同FIR再导出后需重新solve清除pending，符合串行协议。
正常单变体的FIR/solved/decisions与原行为保持，完整构建/lit/core/replay/csa/check-tree。

## 7. 修订
X-01/D-05/N-06修订：路径身份不变，FIR发布不可覆盖，不同有效内容是显式输入冲突；X-03/X-04校验输入阶段；F-34/F-35不变。

## 8. 公司
COMPANY新清单说明新WorkDir/同版本/阶段串行与冲突处理；反馈只含冲突TU数、cdb变体数、候选数、阶段/退出码。
不同条目属于哪些产品/构建配置需公司确认，不代用户任意挑选。T3将独立比较各阶段产物，实际全部差异仍需公司核对。

验收：构建通过，lit118/118、core187、replay一致、csa回退0/TN强告警0、check-tree28源/29头一致；约定31通过/1既有提示/0失败。没有WorkDir的export由提示升级为错误，engine.c负例明确要求非0；其余原告警断言保持。
