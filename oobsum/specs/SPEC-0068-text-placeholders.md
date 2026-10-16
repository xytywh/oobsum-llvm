# SPEC-0068 告警文本：字符串常量、调用点上不知道的字段值不再露出内部名字

状态：已实现
来源：探针里的两处文本：

* 字符串常量当缓冲区：`Rd("abc", 4)` → "up to byte 5 of **'__str'** (4 bytes) is accessed"——`g:__str` 是引擎给字符串常量起的内部对象名。
* 别名实参的字段在调用前改成了不知道的值：调用点记录写占位符 `_unknown_<字段>`（SPEC-0026），文本是 "up to byte **_unknown_ulLen** may be accessed"。

## 2. 设计

* 对象名：`g:__str` 写成 `a string literal`（`Alert.cpp` 的 `ObjName` 与 `replay.py` 的 `obj_name` 同步）。
* 表达式：`_unknown_<字段>` 写成 `<字段>`（`Engine.cpp` 的 `Readable`；原因与"Cannot prove"已经说明它在调用点不知道）。

只改文本，判定、原因、指纹不变。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`core/Alert.cpp`（`ObjName`）、`core/Engine.cpp`（`Readable`）、`replay.py`（`obj_name`）。

## 5. 测试

* lit `text-placeholders.test`：字符串常量 → "of a string literal (4 bytes)"；字段不知道 → "up to byte ulLen may be accessed"；整份输出里没有 `__str`、`_unknown_`。
* `run.py csa` 与基线相同；`replay.py` 不回退（金标准告警里没有字符串常量）。

## 6. 调试

无。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-32 `{object}` | `g:` / `local:` 写变量名 | 字符串常量写 `a string literal` |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

无。
