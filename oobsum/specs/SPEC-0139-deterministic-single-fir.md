# SPEC-0139 多FIR留存、每TU确定选择一份

状态：已实现
来源：2026-11-11用户明确选择小范围方案：export可保存不同FIR，后续每个源文件确定性只读取一个；接受未选变体真实问题漏报，优先告警稳定及改动可控。覆盖SPEC-0133对三阶段不同内容一律拒绝的规定；其它输入失败保护保持。

## 1. 决策与边界
这是用户授权的召回例外：未选变体不求解、不标SAFE，不为其伪造逐访问判断；以输入选择账本记录selected/ignored及政策版本。已求解判断仍按原标签/日志规则处理。
不改core引擎、函数身份、类型、代换、模型、报告过滤或告警指纹；不实现多目标求解、配置组合、通用缓存。各TU独立选代表，不能保证代表集合对应真实完整链接配置，仍可能误报/漏报。
仅Phase=export启用新政策。inline无后续选择阶段，仍保留原单变体保护；旧fir-conflicts目录仍拒绝，不自动迁移。使用新WorkDir、统一版本、阶段串行。

## 2. 保存与固定选择
沿用fir/<tu>.json及原TuId，保持现有工具/solve的单TU入口不变。规范ModuleToJson的固定缩进文本（含target及源身份，不含PID/WorkDir/耗时）计算16位HashHex作为变体ID；固定选择哈希字典序最小者，哈希无质量或严重程度含义。
首次仍不可覆盖发布。碰到不同内容时，将原/新内容都幂等保存到fir-variants/<tu>/<hash>.json；正常同内容仅保留原入口，出现多变体后所有不同内容留存。
不同发布者在稳定的fir-locks/<tu>.lock上使用LLVM文件锁串行比较/归档/更新代表，先保存版本证据再原子写代表；锁文件不删除，内核释放进程持有的锁。固定取min具交换/幂等性，不能用完成顺序、mtime、PID或随机哈希表首项选择。
保留完整候选直到保存/更新成功；任何I/O、哈希碰撞、锁失败都非0报错，禁止静默继续。后续重导出仍设置inputs-pending并要求重新solve。

## 3. solve与观测
solve仍只解析fir/*.json，一TU一份；校验每个多变体入口确实是已留存哈希中的最小者，不对其它正文重复求解。
完整输入检查包含fir-variants内未完成候选、目录/文件类型及名称异常、少于两份版本；老冲突目录和原候选检查保持。发布时只做浅观察，不扫描其它活动写者。
总是写dump/solve/fir-selection.json：政策、TU/变体/忽略计数及每个多变体TU的selected/ignored哈希。Dump=inputs另打印选择事件；普通统计和告警不受Dump开关影响。
solved中每TU保存source_files；有多变体时另存selected_fir哈希，固定report输入。选择依据/未选数量可只计数带回，原FIR只留公司。

## 4. report对齐
仅多变体TU重新Lower当前编译实例并算相同哈希；先校验solved.selected_fir仍为最小完整已存ID，再允许等于selected_fir的实例报告/写decisions。已导出未选哈希留report选择记录后返回，不能用空日志覆盖选中结果；不在已导出集合的哈希属于输入漂移，非0报错。
gate在TU级调试输出、当前AST调试判定及判断日志前；复用已Lower的模块，避免同一TU重复降级。export/report须保持MaxBlocks等降级选项一致。
新solved的report note可见集合固定为source_files：链note只在集合内定位并载入，allocation basename也按此集合唯一消歧，完整路径也须在集合内。主warning与完整链文本保留；legacy手工solved无字段时维持旧行为；inline维持当前TU可见集合。
新solved report只用预计算判断，不从当前AST追加oobsum_debug_emit调试标签；此钩子仅inline或无真实solved的legacy调试模式保留。避免同FIR却字符串宏不同引入不同调试标签，正常业务不使用此钩子。

## 5. 测试
core：多内容正反次序选同一代表/留同一集合、同内容幂等、原默认拒绝模式兼容、真正I/O/碰撞/候选失败仍可见、空/错误变体目录拒绝。
lit：自有CAP8/32重复与实际并发，最终代表/solved/判断/报告稳定；只换-o幂等；忽略变体可含真实DEFINITE，明确属于获准范围例外，不能篡改基线掩盖。
report：选中才写日志，未选不清空；未知哈希拒绝；输入顺序/完成顺序/Dump开关保持一致；same-FIR不同无语义头文件的allocation/chain note一致；legacy歧义仍不猜。
全构建/lit/core/replay/csa/check-tree；源用例/金标准不改。更新既有冲突lit的预期，保留inline拒绝、残留、老冲突及失败回归。

## 6. 主规格与交付
D-05/X-01/X-03/N-06/F-33/A-91/I-21注明本SPEC覆盖；HANDOFF记录用户授权的召回取舍。COMPANY继续空目录单轮主流程，解释代表/留存版本、选择清单、未选报告、统计口径和异常反馈。
公司带回原cdb10707条/10277文件/262重复/108不同命令，另一份10128文件无重复、export无冲突；少149文件只能记录范围差异，不能当作单纯去重覆盖证明。仅记录计数，不记录业务名。

## 7. 本地验收
构建成功、lit123/123、core194/194、replay一致、csa回退0/TN强告警0、check-tree29源/30头一致；自有TestCase7判断/5warning保持。32个公司Bash块语法、内嵌只计数命令自有夹具验证通过；模拟落地幂等，conventions31PASS/1既有WARN/0FAIL。未改Engine/Interp/Solver或金标准，已知engine_check历史关系域缺口不在本步修复。公司清单2026-11-11-3待验证。
