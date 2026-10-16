# SPEC-0059 头文件里的内部链接函数：键加 `#<FIR 哈希前 8 位>`（D-02 后半）——跨 TU 不再用错函数体

状态：已实现
来源：06- D-02 / D-08；SPEC-0003 §2.1 把哈希留到"S4 有 FIR 之后"，STATUS 已知问题写着"S8 摘要接上之前必须补上"——S8 / S9 做完了，它还没补。

探针（两个 TU 各自 `#define BUF_LEN` 后包含同一个头文件）：

```c
/* buf.h */ static inline void Buf_Put(unsigned char *p, unsigned int i) { if (i < BUF_LEN) { p[i] = 0; } }
/* a.c */   #define BUF_LEN 32  #include "buf.h"  void A_TP(unsigned int i) { unsigned char a[16]; Buf_Put(a, i); }
/* b.c */   #define BUF_LEN 16  #include "buf.h"  void B_TN(unsigned int i) { unsigned char b[16]; Buf_Put(b, i); }
```

两份 `Buf_Put` 的键都是 `F@<buf.h 的 id>:Buf_Put`，solve 按键去重、只留一份（D-08 的"FIR 相同只求解一次"被当成了"键相同"）。留下 b.c 那份时，
**A_TP 的真越界被判成 SAFE（漏报）**；留下 a.c 那份时，B_TN 误报 POSSIBLE。哪份留下取决于 FIR 文件的顺序。

## 2. 设计

### 2.1 哪些函数

键形如 `F@<文件 id>:<名字>`、且文件 id 不是本 TU 的 id（定义在头文件里的内部链接函数，SPEC-0003 的 `header`）。外部链接函数（`F:`）不改（D-07 由 solve 的规则管）。

### 2.2 哈希的内容（规范文本）

一个函数一段文本，与 TU 无关的写法：

* `fn` 行：基础键、`ext`、返回类型；`local` 行：编号、名字、形参 / obj / tmp、类型；块、指令（`InstText`）与边（`ExprText`）；指令位置写**文件名 + 行列**；`flags`。
* 类型写结构：`rec#N`（记录在本 TU 里的下标）换成"名字 / 大小"，并附上用到的记录（局部、返回值、引用到的全局的类型里出现的，递归）每个字段的名字、偏移、类型。
* 引用到的全局（`&G…`）附一行"键：类型"（大小不同的同名头文件 `static` 全局让函数哈希不同）。
* 引用到的**头文件函数**写它的**最终键**（Merkle：被调者的函数体变了，调用者的键也变）。同一个强连通分量（互相递归）里的互相引用写基础键。

最终键 = 基础键 + `#` + xxHash64(文本) 的 16 位小写十六进制的前 8 位。函数体完全相同的版本（同一个头文件、同样的宏）在各 TU 里得到同一个键 → solve 仍只算一次（D-08）。

### 2.3 改名

强连通分量按被调者先的顺序（Tarjan）逐个算；算完一个就把模块里所有对它的引用换成最终键：函数的 `key`、调用的 `callee`、函数地址 `&F…`、
函数内 `static` 的键 `S@<函数键>:<名字>`（`globals` 与 `&S…` 都换，各版本的 `static` 局部从此分开）。

在 `lower/` 的 `LowerTranslationUnit` 末尾调用（纯逻辑在 `core/HeaderKeys`），所以 `Dump=fir`、export、inline 看到的都是最终键。
`Dump=keys`（Clang 侧的键表）照旧打印基础键与 `header` 标记。`KeyName` 本来就去掉 `#…`（调用链、告警文本里的函数名不变）。

## 3. 对召回与误报的影响

**召回**：修掉跨 TU 时头文件函数用错函数体导致的错误 SAFE；**误报**：另一个方向的误报也一起没了。inline（单 TU）不受影响（一个 TU 只有一份）。
指纹（F-34）里头文件函数的键变了（只影响这类函数的判定指纹，一次性）。

## 4. 实现位置

`core/HeaderKeys.{h,cpp}`（新）、`lower/Lowering.cpp`（调用）、`clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt`（源文件列表末尾追加）。

## 5. 测试

* 单测 `HeaderKeys.*`：头文件函数加哈希、主文件函数不变；常数不同 → 键不同；只有记录下标 / 文件表顺序不同 → 键相同；被调者变了调用者也变（Merkle）；
  调用、函数地址、`static` 局部一起改名；互相递归不死循环。
* lit `header-key.test`：探针那两个 TU 走 export → solve → report：A_TP 报 POSSIBLE、B_TN 判 SAFE；`Dump=fir` 里键带 `#`。
* `run.py csa`（`common.h` 的 `Pub_MemAlloc` 键变了）：回退 0；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=fir`（`fn` 行的最终键）；`Dump=keys`（基础键 + `header`）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| SPEC-0003 §2.1 / D-02 | S2 先不加哈希，S4 补上 | 已补：规范文本（类型按结构、位置按文件名、被调头文件函数按最终键）的 xxHash64 前 8 位 |

## 8. 公司侧验证

可选：业务全量 export 后，同名头文件函数有几个不同哈希（`fir/*.json` 里 `#` 后缀不同的键）。

## 9. 待定 / 以后

* 头文件里的 `static` **全局**大小因宏不同而不同：键仍相同，但**不是问题**（探针验证过，两种 FIR 顺序都判对）——类型完整的全局在各 TU 里按本 TU 的类型取大小，
  全局大小表（D-34，SPEC-0032）只给不完整的 `extern` 数组用。函数哈希里也带上了全局的类型。
