# SPEC-0130 摘要保存模式与免盘点入口

状态：已实现
来源：用户2026-11-04确认两份摘要已保存；全板盘点与预检慢，要求解释每一步、允许手动打开文件、换函数时避免反复全量 solve。

## 1. 要解决什么
--summary-function 只挑选调试产物，却容易被理解成只求解指定函数。未保存的完整 local/derived 无法仅从 solved 判断反推。
需要让一次全量运行能够选择保存全部，以后只读查看；不能为此把26万份JSON再逐条打到终端和日志。

## 2. 设计
* 新 Dump=summary-files（仅 solve）：按 DumpFilter 保存相同的local/derived/index/status，不输出逐函数JSON；旧summary保持stderr协议与落盘兼容。
* solve --summary-all 与 --summary-function 互斥。all 不预读 FIR，设置 Dump=progress+summary-files、将DumpFilter覆盖为匹配全部的 `.*`，覆盖extra中的对应调试值，不改变其它策略。
* all 完成后检查本次索引更新与status：available=matched=saved=索引数、write_failures=0、index_written=true；不重复解析全体摘要文件。
* --skip-summary-precheck 仅配 --summary-function，跳过Python全量FIR盘点，仍按函数名字面量生成筛选器，完成后核对请求函数与视图；错名要等solve后才发现。默认预检不变。
* --summary-function 保持原summary方式，可配旧checker；全量保存才用新summary-files，避免不必要的stderr大JSON。
* 保存期间增加summary阶段进度；仍先完整求解再保存。三种保存方式的solved字节一致，不更改标签、日志或报告过滤。

## 3. 对召回与误报的影响
无。只控制已计算摘要的额外持久化与读取；不缩小分析集合，不新增SAFE或抑制判断。

## 4. 实现位置
DumpSink种类登记、checker::solveAll摘要落盘与进度、oobsum_run.py solve参数与产物核验、COMPANY.md统一流程与说明。

## 5. 测试
复用summary-selection.test合成两TU：全量保存计数、两种视图可查、无逐函数stderrJSON、清除旧Filter、跳过预检不调用inventory、错名在运行后失败、参数互斥。
比较无摘要/选函数/全部保存的solved字节一致；删除FIR后仍可查不同函数的local/derived；部分摘要写入失败时全量模式明确失败。
完整重编通过；lit116/116、core172、replay一致、csa回退0/TN零强告警、check-tree一致、冒烟7/7、约定31通过/1既有提示/0失败。engine_check维持14/16（LV3-07/LV8-01既有差异）。新Python配旧checker的SPEC-0129路径与跳过预检也通过。

## 6. 调试
Dump=progress+summary-files，dump/solve/progress.json的summary阶段；summary/status.json持久计数。summary命令与直接打开JSON均为只读，不会补生成摘要。

## 7. 主规格修订
F-42/I-21/I-22/N-09增加全量持久化、可跳过预检与保存阶段；不改变函数摘要粒度。

## 8. 公司验证与文档
COMPANY.md清单改为2026-11-04-1统一流程，移除空索引临时恢复段；开头仅路径变量，需要时再填自有示例函数名（无尖括号/反引号，单引号可用）。
盘点可选，已知对应FIR可直接查看funcs[].name/key。解释阶段/产物/local与derived/fp与函数名；提供三种保存方式和日后直接读文件流程。
不要求重跑已完成的两函数solve；新全量保存功能须同步编译后在下次计划中的全量运行选择验证，带回仅计数。

## 9. 边界与代价
仍是每函数两份JSON，26万函数约53万份文件，增加磁盘空间、inode和写盘时间；不承诺耗时或体积，保留只存少量/不存选项。
未保存的函数不能事后从solved补出完整摘要；需在未来一次solve保存。已保存的任意函数可在进程结束后查看，无需重算。
