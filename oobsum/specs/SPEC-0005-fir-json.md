# SPEC-0005 FIR 的 C++ 结构与 JSON 读写（`oobsum-fir/1`，06- D-10…D-14）

状态：已实现（S3b）
来源：`STATUS.md` S3b；06- §5.2（D-10…D-14）、D9（引擎只看 FIR）、D10（`llvm::json`，不引入第三方库）、X-01（导出写 `.tmp` 再改名）、
A-96（异常输入返回错误、不靠 `assert`）。

## 1. 要解决什么

降级（S4、S5）产出 FIR，引擎（S7 起）只读 FIR，方案 B 的 export / solve 之间靠 FIR 文件传递。先把**数据结构**与**读写**定下来，
并用单元测试证明"写出 → 读回 → 再写出"逐字相同，坏输入报错不崩。06- §5.2 有几处没写死，本 SPEC 定下来（§2.3）。

## 2. 设计

### 2.1 C++ 结构（`core/Fir.h`，命名空间 `oobsum::fir`）

| 结构 | 内容 | 说明 |
|---|---|---|
| `Module` | `tu`、`file`、`target`、`files`、`types`、`records`、`globals`、`funcs` | 一个 TU 一份；`types` 是**类型表**（C++ 侧才有，JSON 里类型照 D-10 嵌套写） |
| `Type` / `TypeId` | `kind`（int / ptr / arr / rec / void / other）、`width`、`isSigned`、`to`、`count`（-1 = 不完整）、`record`、`size` | `Module::Intern(Type)` 去重，同样的类型同一个 `TypeId`；比较类型就是比下标 |
| `Record` / `Field` | `id`、`name`、`size`；字段 `name`、`off`、`type` | 布局按被分析 TU 的目标 ABI（D-10、§5.4） |
| `Global` | `key`、`name`、`def`、`type`、`loc` | 键见 SPEC-0003 |
| `Function` | `key`、`name`、`ext`、`loc`、`ret`、`locals`、`entry`、`exit`、`blocks`、`flags`、`exprs` | `exprs` 是本函数的**表达式池**，`ExprId` 是下标；`flags` 如 `cfg_too_large`（A-09） |
| `Local` | `id`、`name`、`param`（-1 = 不是形参）、`type`、`obj`、`tmp` | D-11 |
| `Block` / `Edge` | `id`、`insts`、`succ`；边 `to`、`assume`（`kNone` = 无条件） | D-14 |
| `Inst` | `op`（set / load / store / call / havoc / ret）、`dst`、`addr`、`val`、`size`、`sub`、`callee` / `calleeExpr`、`args`、`ord`、`why`、`loc` | D-12；`kNone` 表示 null / 没有 |
| `Expr` | `op`、`sub`（`BinOp` / `CmpOp`）、`width`、`isSigned`、`value`、`size`、`a`、`b`、`text` | D-13；各字段在哪种 `op` 下有效见 `Fir.h` 注释 |
| `Loc` | `file`（`files` 下标）、`line`、`col` | D-06 |

* 块号、局部号、记录号是**任意的唯一整数**（§5.2 的例子里入口块是 2、出口块是 0，沿用 Clang CFG 的块号），不要求等于下标；
  `Function::FindBlock` / `FindLocal`、`Module::FindRecord` 按号查。
* 构造表达式：`Function::Add(Expr::Const(16))`、`Expr::Bin(BinOp::Mul, a, b)`、`Expr::PAdd(p, e)`…（降级与单测共用）。

### 2.2 JSON（`core/FirJson.h`）

```cpp
llvm::json::Value ModuleToJson(const fir::Module &m);
bool ModuleFromJson(const llvm::json::Value &v, fir::Module &out, std::string &error);   // error 带位置：funcs[0].blocks[1].insts[0].addr: …
bool WriteModuleFile(llvm::StringRef path, const fir::Module &m, std::string &error);   // 先写 <path>.tmp 再改名（X-01）
bool ReadModuleFile(llvm::StringRef path, fir::Module &out, std::string &error);
```

形状与 06- §5.2 的例子逐字段一致；`llvm::json` 输出时对象键按字母排序，所以同一个 `Module` 的输出是确定的。

### 2.3 §5.2 没写死、本 SPEC 定下的

| # | 问题 | 定为 |
|---|---|---|
| 1 | §5.2 例子 `["bin", "mul", ["l", 1], ["c", 16]]` 没带 `w`、`s` | `["bin", op, a, b]` = **数学整数运算、不回绕**（降级生成的下标 × 元素大小、指针偏移都用它）；`["bin", op, a, b, w, s]` = 按 `w` 位、`s` 有无符号回绕（C 源码里的整数运算） |
| 2 | `cmp` 的 `s` | 必须写（`true` / `false`），`op` 取 `eq ne lt le gt ge` |
| 3 | 布尔 `s` 的写法 | JSON `true` / `false`（与 D-10 的 `{k:int,w:32,s:false}` 一致） |
| 4 | `bnot` 没有宽度 | 数学整数上 `~a = −a − 1`；要按宽度截断由降级再套 `cast` |
| 5 | 常量范围 | `["c", v]` 的 `v` 必须**严格**在 int64 之内（`INT64_MIN < v < INT64_MAX`）；放不下的（`0xFFFFFFFFFFFFFFFFULL`、`LLONG_MAX` 等）由降级写成 `["unk", "const_overflow"]`（A-08，未知值只会导向 SUSPECT，不会导向 SAFE）。**实测：LLVM 14.0.0 的 `json::parse` 把超出 int64 的整数悄悄饱和成 INT64_MAX / INT64_MIN**（不报错、不走 uint64），读回来分不清，所以这两个值本身也拒绝。所有整数字段同此规则 |
| 6 | 可选键 | 取默认值时**省略**：`param`（不是形参）、`obj` / `tmp`（false）、`sub`（false）、`assume`（无条件）、`call.dst`（没有返回值）、`flags`（空）、`arr.n` 例外——见下 |
| 7 | 可为 null 的键 | **显式写 null**：`store.val`、`ret.val`（D-12）、`arr.n`（柔性 / 不完整，D-10） |
| 8 | 不认识的键、`op`、`k` | **报错**（FIR 只由我们自己的降级产生，写读两边不一致要当场暴露）；任何错误都不 `assert`，返回 false + 带位置的原因（A-96），调用方把这个 TU 当成 export 失败（`not_exported`） |
| 9 | 引用检查 | `["l", id]` / `laddr` / `dst` 的局部、`succ.to` / `entry` / `exit` 的块、`{k:rec,id}` 的记录、`loc` 的文件下标都必须存在；`param` 下标不重复 |
| 10 | `call` 的被调者 | 字符串 = 函数键（直接调用）；`["ind", E]` = 间接调用（D-12）；`ord` 必须有 |

### 2.4 例：§5.2 的函数

```json
{ "key": "F:Ue_InitOne", "name": "Ue_InitOne", "ext": true, "loc": [0, 40, 6], "ret": { "k": "void" },
  "locals": [ { "id": 0, "name": "pstTbl", "param": 0, "t": { "k": "ptr", "to": { "k": "rec", "id": 0 } } },
              { "id": 1, "name": "ulIdx",  "param": 1, "t": { "k": "int", "w": 32, "s": false } } ],
  "entry": 2, "exit": 0,
  "blocks": [ { "id": 2, "insts": [ { "op": "store", "addr": ["padd", ["l", 0], ["bin", "mul", ["l", 1], ["c", 16]]],
                                       "size": 4, "val": ["c", 0], "loc": [0, 42, 5] } ], "succ": [ { "to": 0 } ] },
              { "id": 0, "insts": [], "succ": [] } ] }
```

单元测试把 §5.2 的整段例子读进来再写出，与原文（解析后）完全相同。

## 3. 对召回与误报的影响

没有判定逻辑。读入严格报错的后果是"整个 TU 当成 export 失败"，其中的函数在调用方看来是 `not_exported`——这在 S8 以后会产出
`SUSPECT:not_exported`（召回优先，D16），不会悄悄丢掉判定。

## 4. 实现位置

| 文件 | 内容 |
|---|---|
| `OobSummary/core/Fir.{h,cpp}` | 结构、类型表、表达式构造、按号查找 |
| `OobSummary/core/FirJson.{h,cpp}` | 写出、读入（校验）、文件读写 |
| `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt` | 末尾追加两个 `.cpp` |

## 5. 测试

| 单元测试（`oobsum/unittests/core/`） | 覆盖 |
|---|---|
| `FirTest.cpp` | 类型去重（同构的类型同一个 `TypeId`、不同的不同）；`FindBlock` / `FindLocal` / `FindRecord`；表达式构造 |
| `FirJsonTest.cpp` | §5.2 整段例子往返相同；全部 16 种表达式、6 种指令、6 种类型、可选键 / null 键的往返；坏输入逐条报错且信息带位置：schema 不对、不认识的 `op` / `k` / 键、表达式元素个数不对、引用不存在的局部 / 块 / 记录 / 文件、常量超 int64、重复的块号；文件写读往返且不留 `.tmp` |

lit：`core-unittests.test` 跑上述单测（S3a 已接入）。

## 6. 调试

FIR 的 JSON 本身就是可读的落盘格式；`Dump=fir`（文本记法 + `WorkDir/dump/<tu>/fir/<函数>.json`）随 S4 的降级一起做。
读入失败时的 `error` 形如 `funcs[0].blocks[1].insts[0].addr[2]: unknown expression op 'foo'`，可以直接定位到文件里的那一处。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-13 `bin` | `["bin", op, a, b, w, s]` | 另允许 `["bin", op, a, b]` = 数学整数、不回绕（§2.3 #1） |
| D-13 `cmp` / `bnot` / `c` | —— | §2.3 #2、#4、#5 |
| D-12 / D-14 | `val` 可为 null；`assume?` | §2.3 #6、#7：可选键省略、可为 null 的键显式 null；读入严格（#8、#9） |

## 8. 公司侧验证

不需要单独验证（同步进 BXPlugin 后随 checker 一起编译；`land.py --check-tree` 保证登记了）。可选：公司编译通过即说明 fork 的
`llvm::json` 与上游 14.0.0 在这些用法上一致（`json::Value` 的 int64 / uint64 区分、`json::parse`）。

## 9. 待定 / 以后

* 位域、全局的常量初值（A-28 的"`const` 全局且有常量初值 → 常数"）、调试指令（`oobsum_dump(x)`）：用到时另写 SPEC 扩 `oobsum-fir/1`。
* 大 TU 的读写性能（R3 给出规模后再看是否需要流式读写）。
