# SPEC-0123 求解进度、实时日志、可落盘的两种摘要与报告路径

状态：已实现
来源：用户 2026-11-02 全板反馈：solve 约 20 分钟没有进度；桩 TU 的作用不清楚；report 传目录崩溃；要求看真实函数摘要。

## 1. 要解决什么
solve 的驱动用 capture_output 结束后才写日志；引擎没有阶段进度；全局求解的 Dump=summary 只写 stderr，没有 JSON 文件。

## 2. 设计
core 的只读进度回调描述 analyze / propagate / decisions，各阶段 completed / total。checker 增加 read / write / done。
有 WorkDir 时每阶段开始、结束和至少一秒间隔原子更新 dump/solve/progress.json；Dump=progress 同时向 stderr 打 [oobsum:progress]。
只给阶段内分数，不假装是时间百分比或准确 ETA；最慢函数内不会更新，配合进程 CPU / RSS 观察。
驱动逐行读取合并的 stdout/stderr，立即打印并 flush 到 logs/solve.log；不再等退出才看到结果。
Dump=summary 按 DumpFilter 输出两个可读视图：local（实际函数解释结果、propagate 的输入），derived（用 Solver 结果组装的传递闭包，含上传 Req / 判定链）。
文件名用函数键哈希，索引含函数名、键、TU、相对文件名，防重载与 operator/ 文件名冲突。保持 stderr 的 summary 行协议兼容 engine_check。
report 接受 JSON 文件，或在目录中只存在一个 *report.json 时自动选；多个候选时报清楚的错误并要求给文件，不猜、不 Traceback。兼容统计旧包报告。

## 3. 对召回与误报的影响
无。回调只观察，启用 / 关闭的 solved 字节一致；摘要两个视图不能回灌覆盖实际引擎结果。

## 4. 实现位置
Engine / Solver 进度回调；checker::solveAll；DumpSink；oobsum_run.py run / status / report / explain。

## 5. 测试
lit：阶段与持久 JSON、开关结果逐字一致；两个摘要可读、derived 含链；报告单文件目录 / 多候选 / 无效路径。
lit 110/110、core 161/161、replay 一致、csa 回退 0 / TN 零强告警、engine_check 维持 14/16，check-tree 一致。
两次真实跨 TU solve 的 solved 目录逐字相同；文件握手验证子进程未退出时日志已可读。

## 6. 调试
Dump=progress+summary，DumpFilter 只选待查链；dump/solve/progress.json、dump/solve/summary/index.json 与 logs/solve.log。

## 7. 对主规格的修订
I-21 加 progress；N-09 / X-10：阶段进度持久化、实时日志；F-42 明确局部摘要与闭包视图。

## 8. 公司侧验证
COMPANY.md §0 增加观察命令与选两条已确认问题查摘要的过程。9 秒 / 257 MiB 是旧子系统的测量值；同一全板最新基准约 20 分钟 / 29.4 GiB，不能按文件数线性承诺。

## 9. 待定 / 以后
混目标隔离另写 SPEC；并行与增量不在本步。
