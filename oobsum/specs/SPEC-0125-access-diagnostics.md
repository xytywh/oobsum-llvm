# SPEC-0125 访问点的诊断信息穿过 FIR 与摘要

状态：已实现
来源：用户公司反馈：只给字节末端、调用点位置和问号对象，看不出实际访问；结构体数组要自己换算，未知全局指针没有来源说明。

## 1. 要解决什么
先补齐实际 sink 的语义信息，供下一步报告格式和位置使用。不能在 report 阶段猜测调用方语句就是访问。

## 2. 设计
FIR 的访问指令附加仅供诊断的 accesses：规范化表达式、访问类型、元素类型 / 步长、访问宽度、字段偏移、下标表达式、可能的指针来源、调用实参编号。
AST 类型名只打印简单 typedef / record / builtin，不展开模板实参；表达式限制深度，不复制源码行或字符串字面量内容。
每个函数内的访问有独立 diag 编号，随 Req.access、Call 和 Open 传递；同一行多个操作不靠行号猜对应关系。
core 只看 FIR；lower 负责 Clang 类型与表达式。读取老 FIR / 摘要时诊断字段可缺省。HeaderKeys 的语义哈希不包括诊断信息。
Decision 附加 access_key / access_ids / target / sink / accesses。原 site 与指纹仍描述判定上下文，本步不改变标签、策略或 warning。
来源是语法证据：局部指针被赋过全局指针时列出可能来源，不声称它是精确的路径敏感指向分析。

## 3. 对召回与误报的影响
不改变推理。新字段不参与区间、守卫、Req 覆盖或指纹。金标准比较忽略 diag（诊断编号与源码坐标类似），仍逐项比较所有语义字段；不改 alerts / @expect。

## 4. 实现位置
core/AccessInfo、Fir / FirJson、Summary、Interp、Engine、DecisionLog；lower/AccessInfo 与 Lowering::Emit；engine_check 的展示字段归一化。

## 5. 测试
core：诊断 JSON 往返 / 坏类型；lit：结构体数组步长与字段偏移、内存函数实参、全局指针别名来源、同一行多个不同访问、C++ 模板不展开。
构建通过；lit 112/112、core 165、replay 一致、csa 回退 0 / TN 零强告警，engine_check 维持 14/16；check-tree 一致。
新增比较器回归：真实字段名 diag 不能被误删；只忽略已知诊断位置的编号。

## 6. 调试
Dump=fir 的 JSON 含每条指令的 accesses / diag，WorkDir/decisions 的 sink / access_ids 能对应到该指令；DumpFilter 选函数。

## 7. 对主规格的修订
D-12、D-23、D-27 增加可选诊断字段；N-10 允许有界的 AST 规范化访问表达式（名字、运算与常量），仍不含源码行与字符串内容。
T-06 只在 access / calls / opens 的已知注解位置忽略诊断编号 diag，不删除同名的真实字段；其余要求不变。F-30 的报告位置由后续 SPEC 修改。

## 8. 公司侧验证
下一版 COMPANY.md 合并 sink 诊断的完整验证；业务摘要与表达式只在公司查看，不带回名字和正文。

## 9. 待定 / 以后
下一步使用这些信息改报告位置、格式和同一行合并；尾数组的默认报告策略另写 SPEC。

C++ 压力复查：2373 个文件，1459 正常、912 编译不通过、2 处在不启用本规则的语法检查中同样断言（前端解析阶段）；本规则 0 新增崩溃、0 超时。

表达式回归：`p[i++]` 不能打印成 `p[++i]`；条件解引用打印 `*(c ? p : q)`，由 access-info.test 的真实 AST 产物核查。
