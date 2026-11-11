# SPEC-0137 并发export只观察标记，保留本次发布结果

状态：已实现
来源：2026-11-11公司export反馈11个冲突TU，诊断扫描同时观察到4–8个临时候选，并出现candidate消失的ENOENT。只记录计数，不录业务日志或文件名。

## 1. 问题与复现
PublishFir已确认本次内容与canonical不同才返回conflicting FIR variants；随后writeFir调用validateInputs(false)，误把solve的全量静态扫描用于仍有并行写者的export。
自有复现：另一写者的候选存在时，成功export仍被调试输出标为incomplete_export；枚举与stat之间正常unlink还可产生cannot inspect FIR candidate。它不是本次变体冲突的原因，不能因此关掉冲突保护。

## 2. 设计
沿用InspectFirInputs的两种模式，只调整调用阶段：仅Phase=solve完整扫描冲突子目录及*.tmp；export/inline的发布观察和report只检查根标记，不枚举活跃候选。
浅观察明确details_scanned=false，conflicting_tus/incomplete_files=-1表示未统计，不把未知写成0；有pending时标pending_solve，不暗示全批导出完成。
发布观察额外记录tu、publication=published/failed、publication_error；workdir的error字段仍只表示标记检查错误。本次失败必须可见，正常其它TU的成功不因同批其它冲突伪装成本TU失败。
发布诊断落dump/<tu>/inputs-<pid>.json，避免并发TU覆盖同一dump/inputs.json；进程号只用于调试文件名，不进入FIR、判断、指纹。solve/report继续使用dump/inputs.json作为该阶段快照。
不开Dump时正常发布不加输出；失败始终输出并落盘。Dump开关不改变PublishFir或任何推理结果。

## 3. 边界与召回
不改变不可覆盖发布、不同内容保留两份快照并非0退出；不忽略solve时真正遗留的候选、I/O错误、空冲突目录或pending报告保护。
不支持阶段重叠；必须全部export结束且成功后solve。浅观察是并发时点的标记状态，不证明输入齐全；公司11个真实内容冲突仍需核对来源，不能认定全部由宏或本次扫描造成。
不改标签/金标准/报告策略；不把输入失败标SAFE或随意丢变体。

## 4. 实现位置
OobSummaryChecker::writeFir/validateInputs；FirStore.h模式说明；现有Dump=inputs。

## 5. 测试
core：在WriteFileExclusive真实候选仍打开时，浅检查不枚举；完整检查仍拒绝未完成候选，结束后可通过。
lit export-inputs.test：活跃候选下成功与冲突发布、非正常候选类型、实际并行导出、Dump开关相同canonical；浅观察未知计数、本次结果、按TU/PID落盘；导出结束的solve严格拒绝遗留及冲突，report仍受pending保护。
全构建/lit/core/replay/csa/check-tree，无金标准修改。
结果：构建成功，lit122/122、core192/192、replay一致、csa回退0/TN强告警0、check-tree29源/30头一致。模拟落地幂等，conventions31PASS/1既有WARN/0FAIL；公司只读盘点在自有冲突/空目录验证，命令块语法通过。

## 6. 调试与公司
Dump=inputs仍打印[oobsum:inputs]，schema oobsum-inputs/1新增发布字段；发布文件与solve全量快照用途区分。
COMPANY新清单先保留既有冲突WorkDir，等写者全部结束后只读盘点cdb与快照；反馈冲突/候选计数、差异类型及构建域。未确认前不删证据、不继续solve/report、不强制盲跑全量。

## 7. 主规格修订
X-01/X-03/I-21/N-06：发布时浅观察，solve时完整验证；完整扫描不得用于并发export的诊断。替代SPEC-0133中发布错误时全量统计的实现细节，其余保护保持。
