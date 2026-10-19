# SPEC-0109 DEFINITE / POSSIBLE 的告警文字：调用方的对象代换不过来时用被调者的形参名

状态：已实现
来源：SPEC-0107 测试中发现（原来就有）：形参所指结构体里的成员数组的 Req（`pst->aucPad[i - 1]`）在调用点判 DEFINITE 时，调用方是局部结构体 `st`，
成员对象代换不过来（`obj` 为 `?`），告警文字写成 `before '?' (0 bytes available before the pointer)`。SUSPECT / SAFE 的文字早就用被调者的形参名兜底
（`core/Engine.cpp`："对象代换不过来……用被调者的形参名写成员"），DEFINITE / POSSIBLE 没有。

## 2. 设计

* `solver::FunctionResult::repCallee`（与 `reps` 对齐，只在内存里）：调用点判出的每条记 `(被调者的键, 被调者那一侧的对象)`；入口自己判的为空。
* `engine`：`rep.obj` 是 `?` 时，告警文字用被调者那一侧的对象、按被调者的形参名写（`alert::AlertMessage(…, 被调者的 params)`）。判定的 `obj`、指纹不变。
* 摘要格式、`replay.py` 不变（金标准里没有 `?` 的判定）。

## 3. 对召回与误报的影响

只改文字。

## 4. 实现位置

`core/Solver.{h,cpp}`、`core/Engine.cpp`。

## 5. 测试

lit `subobject-back.test` 加一条：`SetPrev(&st, 0)` 的 DEFINITE 文字写 `'pst->aucPad'`。

## 6. 调试

无新增。

## 7. 对主规格的修订

无（F-32 消息模板的对象名规则不变，只补兜底）。

## 8. 公司侧验证

无。

## 9. 待定 / 以后

无。
