# SPEC-0129 摘要零匹配可诊断、函数选择可预检

状态：已实现
来源：用户实际执行后 dump/solve/summary 只有空 index.json，无法查看摘要；此前文档的占位 DumpFilter 和无反馈的筛选容易误用。

## 1. 要解决什么
全板已算摘要，但没有函数通过 DumpFilter 时静默写 []；用户需等完整 solve 后才能知道选错。

## 2. 设计
保留 DumpFilter 语义，不在零匹配时自动导出全量。
Dump=summary 输出 available / matched / saved / write_failures 计数，持久化 status.json；零匹配明确提示筛选问题，零可用摘要提示检查 FIR 加载。
索引仅列两种视图都写成功的函数。调试写入错误要提示，但不改变求解的标签、solved 结果或 checker 退出策略。
驱动新增 summary --list，从 FIR 读取可用函数，支持 --contains、--limit 和 --counts-only；summary --function NAME --view local/derived 读取已保存的摘要。
solve --summary-function NAME 可重复，按实际显示函数名精确预检，未知名在启动 bxen 前失败；完成后核对本次更新的索引、所选目标与函数键、两种视图，不能误用上次的产物；驱动生成 LLVM POSIX ERE 的字面量过滤器，转义运算符和空白，避免手写正则。
显式函数选择会设置 Dump=progress+summary 与生成的 DumpFilter，优先于 --extra 中的对应调试选项；其它模型与策略选项保持原样。
旧的 --extra DumpFilter 接口继续有效；新查询工具支持旧 checker 生成的索引，因此当前恢复只需更新 Python 工具并复用 FIR，不必等待重编 BXPlugin。
explain 对未保存的链上函数明确提示，不把旧 TU 的残留局部摘要冒充这次全局摘要。

## 3. 对召回与误报的影响
无；只改摘要选择、保存反馈和查询。开关调试与筛选错误不改变实际判断。

## 4. 实现位置
checker::solveAll 摘要计数 / status；oobsum_run.py inventory / summary / solve / explain；COMPANY.md 恢复流程。

## 5. 测试
lit：零匹配索引与明确提示，status 计数；与有匹配时 solved 字节一致；未知函数预检不启动 bxen；函数盘点、两种视图查询、歧义及空索引错误；含 C++ 运算符的过滤器转义；索引 / 视图写盘失败，残留索引不冒充本次成功。
完整构建通过；lit 116/116、core 172、replay 一致、csa 回退0 / TN零强告警、check-tree一致、冒烟7/7、约定31通过/1既有警告/0失败。engine_check维持14/16（LV3-07 / LV8-01既有关系域限制），无需修改金标准或报告策略。

## 6. 调试
Dump=summary 的 stderr 计数与 dump/solve/summary/status.json，Dump=options 查看实际筛选选项；summary --list 在公司本地查看名字，带回仅用 --counts-only。

## 7. 对主规格的修订
I-21 / I-22 / F-42 / N-09：摘要筛选零匹配必须可见，增加只读函数盘点与精确函数选择工具；不改变摘要粒度。

## 8. 公司侧验证
COMPANY.md §0 新版本将空索引恢复放在前面，保留尚待验证步骤；只重跑 solve 就能生成摘要，不要求再次 export/report。

## 9. 待定 / 以后
FIR 盘点不验证每条指令的语义合法性，仍以 checker 加载结果和 status 计数为准。
