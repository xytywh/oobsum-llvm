# SPEC-0067 告警文本：调用点上不知道的被调者符号按被调者的形参名写

状态：已实现
来源：探针（结构体赋值后传指针）：`Copy_TP` 没有形参，告警却写 "up to byte p1->ulLen may be accessed"——`p1` 是被调者 `Fill` 的第 2 个形参 `c`，
SPEC-0063 把它改名成 `_unmapped:p1->ulLen`，文本去掉前缀后看起来像调用方自己的 `p1`。复核的人会误会。

## 2. 设计

调用点结局（判 SAFE / 记账）的文本里，`_unmapped:` 后面的被调者符号按**被调者**的形参名渲染：`p<k>` → 被调者第 k 个形参的名字（`_unmapped:p1->ulLen` → `c->ulLen`，
`_unmapped:ext(p0->pucRes)` → `ext(pst->pucRes)`）；全局 `g:…` 照写。别的地方（入口账本、本函数账本）照旧只去掉前缀（SPEC-0064 的 `Readable`）。

只改文本，判定、原因、指纹都不变。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`core/Engine.cpp`（`Readable` 加被调者形参表）。

## 5. 测试

* lit `unmapped-text.test`：结构体赋值后传指针 → "up to byte c->ulLen may be accessed"；整份输出里没有 `p1->ulLen`。
* `run.py csa` 与基线相同。

## 6. 调试

无。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-32（SUSPECT 模板） | 占位取判定记录的机器字段 | 另：调用点上不知道的被调者符号按被调者的形参名写 |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
