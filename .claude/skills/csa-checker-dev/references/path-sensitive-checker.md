# Path-sensitive Checker 范式

适用：缺陷依赖变量在某条执行路径上的值/状态/约束（空指针、越界、未初始化、内存泄漏、taint、值域）。能感知路径条件，代价更高，漏报多来自路径覆盖/loop 展开不足。可编译骨架见 `templates/path_sensitive_checker.cpp`。

> **包路由**：**单函数**路径敏感 → `rb.enhance.core`（运行 `ipa=none`）；**跨函数、单文件** → `rb.enhance.ipa`（运行开启 IPA，**不要** `ipa=none`）；**跨文件/其余** → 先与用户确认。所有 CSA 规则（含 ipa）都在 `xir/llvm_ast` 内用 CSA 自身 IPA 实现，与 BinExplorer 无关。

## 1. 基类与回调（回调名 lowerCamelCase）

| 回调 | 时机 | 用途 |
|---|---|---|
| `checkPreCall`/`checkPostCall` | 调用前/后 | 参数校验、库函数建模、资源获取/释放 |
| `checkBind` | 值绑定到 location | 赋值跟踪、污点传播、状态置位 |
| `checkLocation` | 读/写 location | 解引用、越界 |
| `checkPreStmt<T>`/`checkPostStmt<T>` | 特定语句前/后 | 针对 `ArraySubscriptExpr`/`BinaryOperator` 等 |
| `checkDeadSymbols` | 符号死亡 | 资源泄漏判定、清理 state |
| `checkEndFunction` | 函数返回 | 收尾判定 |

## 2. ProgramState trait

```cpp
REGISTER_MAP_WITH_PROGRAMSTATE(ResourceMap, const MemRegion *, ResourceState)
REGISTER_SET_WITH_PROGRAMSTATE(TaintedSymbols, SymbolRef)
REGISTER_TRAIT_WITH_PROGRAMSTATE(SomeCount, unsigned)
```

state 不可变：`state = state->set<ResourceMap>(region, st);` 后必须 `C.addTransition(state);` 才生效。

## 3. 取值要点（极易错）

```cpp
const Expr *baseExpr = expr->getBase();                 // 原始
const Expr *stripped = baseExpr->IgnoreParenCasts();    // 仅用于 AST 形态判断
SVal baseVal = C.getSVal(baseExpr);                     // 取值用原始 expr，不要用 stripped
const MemRegion *region = C.getSVal(baseExpr).getAsRegion();
if (region == nullptr) {
    return;
}
```

processed expr 可能因死符号清理返回 `UnknownVal`/null——判类型用 stripped、取值用原始，分清两件事。

## 4. 报告与确定性

```cpp
ExplodedNode *node = C.generateNonFatalErrorNode();     // 优先 non-fatal，避免漏后续 issue
if (node == nullptr) {
    return;
}
if (!BT) {
    // category：非空英文、按缺陷性质、仅用于展示，绝不用于过滤
    BT.reset(new BugType(this, "Resource leak", categories::MemoryError));
}
auto report = std::make_unique<PathSensitiveBugReport>(*BT, "English message", node);
report->addRange(expr->getSourceRange());
C.emitReport(std::move(report));
```

- `generateNonFatalErrorNode`：同路径多 issue 时不截断后续；仅在确需 sink 中止路径时才 fatal。
- **确定性**：`checkDeadSymbols`/`checkEndFunction` 里对多个区域报告前，按 source location 排序再报；`MemRegion`/map 遍历顺序不保证稳定。
- 改了 state 别忘 `addTransition`，报告别忘 `emitReport`。

## 5. 路径覆盖与漏报

- loop 默认展开有限：运行用 `-analyzer-max-loop 3`（core/ipa 规则统一 3），README 写清展开假设。
- 跨函数覆盖由 analyzer IPA/inlining 决定：`rb.enhance.core` 单函数规则运行 `ipa=none`；`rb.enhance.ipa` 跨函数规则需开启 IPA。
- 其他循环配置（谨慎调大，会显著增加耗时）：`-analyzer-config widen-loops=true`、`-analyzer-config unroll-loops=true`。
- 在 README 写明：哪些路径不探索 → 哪些缺陷会漏 → 为何工业上可接受。

## 6. 自检
- 真需要路径条件/值流吗？纯语法可判 → 改 AST-visitor。
- 取 SVal/MemRegion 用的是原始 Expr 吗？
- 用了 `generateNonFatalErrorNode`？多报告排序确定？
- 漏报与 loop 展开/IPA 覆盖的关系写进 README 了吗？
