# SPEC-0132 同名文件的分配note不可按指针哈希首个选择

状态：已实现
来源：用户重复全量三阶段结果有差异；固定自有FIR实验证明report的allocation note有两种位置，主warning与decisions各只有一种。

## 1. 问题
Reporter::LocateBase遍历SourceManager fileinfo哈希表，按basename取第一个文件；指针地址随进程变化，同名头文件会随机选错。
这项只解释附加note层差异；export同Tu编译变体覆盖造成DEFINITE/SAFE变化另列T2，不能混为同一根因。

## 2. 设计
完整路径可精确定位时按路径；只有basename时收集所有可见候选并按规范路径去重。仅唯一候选才定位；多候选或缺失不猜位置，保留主warning和已有文本中的分配来源。
不以字典排序挑一个“稳定但可能错误”的来源，不修改判断或指纹。
沿用Dump=diagnostics：分配note定位结果（unique / ambiguous / missing、候选数）提供可落盘的调试记录，按函数名筛选；默认不开启时不加调试噪声。

## 3. 召回与精度
主判断、标签、warning、判定日志都不因省略一个无法证明正确位置的note而消失；只移除错误定位。唯一匹配的原分配note保留。

## 4. 实现
report/Reporter及checker的diagnostics落盘接线；自有同名头文件lit回归。

## 5. 验证
固定FIR重复report得到一致文本；两个同名头文件不发猜测的allocation note，唯一候选仍有正确note；Dump开关不改判断与warning。
完整构建、lit、core、replay、csa、check-tree，所有旧金标准保持。

## 6. 主规格
F-33/N-06：附加位置必须唯一可确定；不能依赖SourceManager指针表迭代顺序。T2覆盖/输入竞态不在本提交宣布解决。

## 7. 公司侧
COMPANY新清单增加分阶段核对需求；只带回计数，不带业务路径、函数名或报告正文。

验收：构建通过；lit117/117，replay一致，csa回退0/TN强告警0，check-tree一致；约定31通过/1既有提示/0失败。固定FIR的多次report与Dump开关不改主告警及判断。
