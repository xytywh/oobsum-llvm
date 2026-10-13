# cases/summaries：16 个用例的函数摘要实样

每个子目录是一个用例，里面**一个函数一份摘要 JSON**（`<函数名>.json`），格式见 `summary.schema.json` 与 `06-spec.md` §5.3。

## 摘要里的自然语言

**只在键名为 `doc` 的字段里，只给人看；引擎读入时整棵删掉，任何计算都不读它。去掉 `doc` 剩下的就是完整摘要**（`06-` D-30）。

* 机器字段：整数、枚举、规范表达式（`16*p1 - 12`、`ext(p0)`）、对象（`g:g_x`、`local:aucPkt`、`heap@a.c:68`、`*p0`）、位置、函数键。
* `doc`：用形参名渲染的一句话、源码原文、宏的值、为什么这样判、告警文本的副本。生产运行默认不写（`Emit=machine`）。
* 保证：schema 里除 `doc` 外每个对象都不允许多余的键；`replay.py` 检查 `doc` 之外没有中文或成句的英文。

## 文件

| 文件 | 是什么 |
|---|---|
| `<用例>/<函数>.json` | 函数摘要（带 `doc` 的调试版） |
| `<用例>/case.json` | 走查文档用的元数据（标题、要点、代码），**不是摘要** |
| `summary.schema.json` | JSON Schema |
| `alerts.json` | 从机器字段推出来的 13 条最终告警（位置、消息、调用链 note、指纹）——C++ `Report.cpp` 的金标准 |
| `replay.py` | 参考实现：逐调用点核对、从局部事实推到底、生成告警、去掉 `doc` |

## 跑

```bash
python3 replay.py                       # ① 逐调用点核对 ② 从局部事实推到底，对照 @expect 与 alerts.json
python3 replay.py LV4-01 -v             # 一个用例的每一步
python3 replay.py --strip /tmp/mach     # 写出去掉 doc 的机器版（引擎实际读写的样子）
python3 replay.py --dir /tmp/mach       # 从机器版重跑，结论完全相同
python3 replay.py --alerts /tmp/a.json  # 输出最终告警
```

只依赖 Python 3 标准库（装了 `jsonschema` 会顺带做 schema 校验）。
