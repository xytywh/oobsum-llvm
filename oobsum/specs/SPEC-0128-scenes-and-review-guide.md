# SPEC-0128 场景证据标签、统计脚本与分析说明

状态：已实现
来源：用户与领域专家讨论，要求按 Lv1..Lv10 统计真实告警、查真实摘要，并解释复杂度与摘要中的调用链 / reps。

## 1. 要解决什么
访问种类和场景复杂性是两维。真实代码同时包含多个因素，不能凭调用层数或用例文件名给它编造唯一的业务场景。

## 2. 设计
Decision.scenes 保存已观察到的能力特征，warning 主行加 [scene=LVn+LVm]；没有证据时 UNCLASSIFIED。
基于源 Req 的接口符号与实际诊断信息：双侧接口 / 算术2，代码检查 / 循环3，返回效果4，字段值 / 指针字段5，动态全局6，尾数组 / 内层 / 下溢 / 布局不足7，非线性下标8，未解析调用 / 递归 / C++断链9；简单可解释访问1。
Lv10 的业务隐式契约不能从现有抽象事实自动推断，不自动标10；保留未分类，可在公司用指纹 / 告警分组的人工标注做统计。
这是 observed_features 分类，不宣称精确恢复 04- 中每个子场景，也不代表某一级全部支持。
独立 scene_stats.py 使用 argparse，默认只输出计数，支持 WorkDir JSONL / clang_check 报告；主类取最高已观察等级，--multi 同时计多标签。
默认按物理 report_group 合并统计，--unit decisions 可看原判断；无新字段的旧报告列未分类；人工 annotations 可补 Lv10，不能修改判断或报告策略。

## 3. 对召回与误报的影响
不参与证明、代换或报告策略，不按场景过滤；日志保留全部标签与已观察特征。

## 4. 实现位置
core/Diagnostics 的纯分类函数；Engine 取原 Req 特征；DecisionLog / Reporter；tools/scene_stats.py；分析说明与 COMPANY.md。

## 5. 测试
core 特征组合与未知边界；lit warning / JSON 都有同样标签；脚本验证原判断 / 物理合并、多标签 / 主类、旧报告未分类、人工 Lv10、错误输入。
构建通过，lit115/115、core172、replay一致、csa108/20/2/3/策略过滤1、TN零强告警、回退0，engine_check维持14/16；check-tree一致。

## 6. 调试
Dump=diagnostics 的 scenes / scene_method 与 warning 主行对照；同一 report_group 的多种场景取并集。

## 7. 对主规格的修订
F-32 增加场景证据标签，D-26 判定附 scenes；不是 L1/L2 标签，也不是调用深度。
N-09 统计加场景分布，原安全 / 召回与日志要求不变。

## 8. 公司侧验证
COMPANY.md 最终清单合并本轮全部待验证步骤，增加场景脚本与两条问题的 local / derived 摘要复核流程。只带回数字与抽象写法。

## 9. 待定 / 以后
精确的业务场景 / Lv10 由专家研判，自动化业务契约推断另立规格。
