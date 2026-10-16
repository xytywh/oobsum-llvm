# SPEC-0033 `ModelFile`：YAML 模型表合并进内置默认表；`Dump=models`

状态：已实现（S7d-2a）
来源：`STATUS.md` S7d-2；06- §4.4 I-40（模型表 `oob_models.yaml`）、I-41（`size` / `bytes` 的写法）；SPEC-0018（内置默认表、`destMax`、`when`、`free`）；
07- 输入 I5（`VOS_MemAlloc` 的字节数形参）、I8（内部封装的访问 / 分配函数）、I9（纯函数清单）——这些清单以后由用户在文件里补，不改代码。

## 1. 要解决什么

模型表现在只有编进代码的默认表（SPEC-0018）。业务代码有自己的分配器、拷贝封装、日志函数，清单要由用户维护，不能每次改代码重编。

## 2. 设计

### 2.1 选项

`ModelFile`（字符串，默认空）：YAML 文件路径。空 = 只用内置默认表（与 06- I-40 的默认内容相同）。
文件读不了、YAML 语法 / 结构错（未知的键、类型不对）→ **选项错误**（`reportInvalidCheckerOptionValue`，与 `Phase` 等写错时一样：运行失败、立即发现），
不悄悄退回默认表跑出另一套结果。

### 2.2 文件格式（I-40 的写法 + SPEC-0018 的扩展）

```yaml
defaults: true                  # 可省略；false = 先清空内置表（调试内建函数的 ^oobsum_debug_ 模式总是保留）
allocators:
  - { name: VOS_MemAlloc, result: ret, size: "p2", maybe_null: true }
access:
  - { name: memcpy_s, dest_max: p1,
      conds: [ { base: p0, bytes: "p1", kind: W }, { base: p2, bytes: "p3", kind: R, when: "p3 <= p1" } ] }
pure:
  names:    [ LOG_Print ]
  patterns: [ "^LOG_" ]
frees: [ VOS_MemFree ]
```

* **合并**：文件里的条目按名字**替换**内置表里的同名条目（无论原来在哪一类：一个名字只属于一类）；新名字追加。`pure.patterns` 追加。
  同一个文件里一个名字写了两次：后面的为准，并写一条提示。
* **逐条校验**，坏条目跳过（内置表里的同名条目保留）并写一条 warning（`[oobsum] warning: ModelFile <路径>: <类别> '<名字>': <原因>`）：
  `name` 为空；`base` / `dest_max` 不是 `pN`；`bytes` / `size` 解析不了（`ParseLin`）或用到 `pN` 以外的名字；`when` 解析不了（`ParseCond`）；
  `kind` 不是 `R` / `W`；`result` 不是 `ret`（出参式分配器 I-42 以后）；`patterns` 不是合法正则。`maybe_null` 读入、P0 不用。

### 2.3 `Dump=models`

每条生效的模型一行（`[oobsum:models] …`，并写 `WorkDir/dump/`）：

```
source default+/path/to/oob_models.yaml
access memcpy_s dest_max=p1 W p0[p1] R p2[p3] when p3 <= p1
alloc malloc size=p0
free free
pure name LOG_Print
pure pattern ^LOG_
```

### 2.4 用在哪

inline、solve 两个跑引擎的阶段用合并后的表；export（只产 FIR）、report（读判定）不用，但同样校验文件（选项错误在哪个阶段都早发现）。

### 2.5 示例文件

`oobsum/config/oob_models.example.yaml`：内置默认表写成文件的样子，附 I5 / I8 / I9 待补的位置。公司侧复制一份按业务改，`ModelFile=` 指向它。

## 3. 对召回与误报的影响

不配 `ModelFile`：与现在相同。配了：新增的访问模型、分配器让原来记 `not_in_tu` / `unknown_callee`（SUSPECT）的调用点能判定；新增的纯函数让调用点不再记账
（**用户声明的事实**：纯函数写错会漏掉它的副作用——文件里写的是用户对业务函数的断言，按 06- I-40 的定义使用）。坏条目不会删掉内置条目。

## 4. 实现位置

`core/Models.{h,cpp}`（`MergeModelYaml`、`DescribeModels`）、`OobSummaryChecker.cpp`（选项、读文件、`Dump=models`、传给 `RunInline` / `RunSolve`）、
`debug/DumpSink.cpp`（种类 `models`）、`Checkers.td`（`ModelFile` 选项、`Dump` 说明）、`oobsum/config/oob_models.example.yaml`。

## 5. 测试

* 单元测试 `Models.MergeYaml*`：新增访问 / 分配 / 纯函数 / 释放；同名替换（跨类别）；`defaults: false`；坏条目跳过并给 warning、内置条目保留；语法错返回 false。
* lit `model-file.test`：`Inputs/models/extra.yaml` 声明 `Drv_Copy`（访问）、`Pool_Get`（分配器）、`^Stat_`（纯函数）和两个坏条目 →
  ① 调用 `Drv_Copy` 越界报 DEFINITE、`Pool_Get` 的块越界报 DEFINITE、`Stat_Inc` 不记账；不配文件时这三处是 `not_in_tu`；② warning 两行；
  ③ `Dump=models` 列出新增条目；④ 文件不存在 → 选项错误；⑤ 示例文件能读、`Dump=models` 与内置表相同。

## 6. 调试

`Dump=models`（看实际生效的表）、`Dump=options`（`ModelFile=`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| I-40 | 默认随代码放 `OobSummary/config/oob_models.yaml` | 默认表编进代码（`ModelTable::Default`）；`ModelFile` 指定的文件**合并**进默认表（同名替换，`defaults: false` 清空）；示例在 `oobsum/config/oob_models.example.yaml`。访问模型加 `dest_max`、条件加 `when`，加 `frees` 类（SPEC-0018） |

## 8. 公司侧验证

**等用户提供**：I5（`VOS_MemAlloc` 的字节数形参）、I8（内部封装的访问 / 分配函数）、I9（纯函数清单）——写进公司侧的模型文件即可，不用改代码；
有了以后用 `Dump=models` 确认生效。

## 9. 待定 / 以后

* 出参式分配器（I-42 的 `MEM_ALLOC=<名字>,<下标>` 非 −1）；`AllocIdentity`（I-43）。
* `strcpy_s` 等其余 securec 函数加进内置表（S7d-2b）。
