# SPEC-0126 默认在 sink 报告，分段说明访问与容量，同一行合并

状态：已实现
来源：用户全板告警复核与 clang_check 三行解析反馈。

## 1. 要解决什么
默认 warning 定位到下标、解引用、成员访问或内存函数；说明是哪种访问、元素类型和大小、可用容量、有效位置与越界量。
调用链必须在 warning 主行里完整出现；同一行多条 warning 被 Clang 省略源码行会破坏收集器。

## 2. 设计
DiagnosticStyle=sink 为默认；legacy 保留旧的调用点格式，便于对照与兼容历史快照。
按实际访问函数所属 TU 路由 solve 判定；报告加载其它源码文件的 FileID，保证跨 TU 的 sink 位置有效。
同一目标、文件、sink 行的候选合成一条物理 warning，主标签取 DEFINITE > POSSIBLE > SUSPECT > SAFE；不同说明与上下文保留。
所有参与判定仍按原标签 / reason 写日志，emitted=true 表示包含在告警中；report_group 与 diagnostic_primary 标出物理告警分组。
主行用分号区分 Access / Object / Layout / Range / Reason / Context / Call chain，保持一个物理文本行，后接源码和 caret。
字节末端明确为 exclusive end；只有能可靠换算时才显示相对于检查指针的元素位置，实际对象大小和剩余可用大小分开。
未知信息明确说 unknown，不用问号或占位符；type_range_only 的人类说明改为“没有可用的代码约束”，机器原因名保持兼容。
同一源行的不同访问按 diag 区分，不能在进入报告层前误当作重复访问丢掉。

## 3. 对召回与误报的影响
不降低标签、不删判定，不因合并把已报判定改成不报；只改变物理 warning 的数量与位置。
报告策略仍独立应用；明确保留每个原始判断的上下文，合并主标签不表示所有上下文都确定越界。

## 4. 实现位置
core/Diagnostics、DecisionLog、Engine 的展示上下文；Reporter 的定位 / 分组；checker 路由、选项和 Dump=diagnostics；工具统计与 explain。

## 5. 测试
core 文案：20 字节结构体数组、4 字节字段、容量 3 / 有效位置 0..2 / 可能访问 3；未知全局指针、无约束、负偏移。
lit：跨 TU 精确 sink 与三行格式，同一行多种理由合一且日志全保留；MIN(未知数,96) / MIN(未知数,36) 对照、i<n / i<=n。
历史文本快照显式 legacy，新用例核查默认；构建通过，lit 113/113、core 168、replay 一致、csa 回退 0 / TN 零强告警、check-tree 一致。
三行格式在真实跨 TU 输出和同一行多理由中逐条核查；不对缩进位置加退化 token range（Clang 14 高亮空白会断言）。

## 6. 调试
Dump=diagnostics（stderr + dump/<tu>/diagnostics.jsonl）；sink / site / context_operation / accesses / report_group 对照。

## 7. 对主规格的修订
F-30 / D12 由默认汇合点改为 sink，原 site 保存证明上下文；F-32 增加访问 / 布局 / 范围分段；F-33 主行始终带链；F-35 按 sink 排序。
I-21 增加 diagnostics，新增 DiagnosticStyle 选项。所有判定入日志的要求不变。

## 8. 公司侧验证
COMPANY.md 更新为新完整清单；需用新 FIR 和新 solve，旧报告不会自动补新信息。检查同一行是否只产生一个完整 warning。

## 9. 待定 / 以后
尾数组默认策略、场景标签和统计分别后续实现。跨 TU report 失败的覆盖差额仍必须核对，不能把成功 TU 当成全板完整覆盖。
