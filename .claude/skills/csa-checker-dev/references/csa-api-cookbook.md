# CSA API Cookbook

高频配方与反复踩过的陷阱。默认 **LLVM 14**。

## 目录
0. include 原则 + LLVM 14 头文件速查（先看这条，避免缺头/路径错）
1. cast 剥离
2. 取 SVal / MemRegion
3. 结构体名：typedef 反查（高频坑）
4. 报告 + BugType category
5. 确定性排序去重
6. 常量值域（APSInt）
7. 附：函数名提取 / 成员路径打印（参考配方，非通用强制）

---

## 0. include 原则 + LLVM 14 头文件速查

**原则：为用到的每个 CSA/AST 类型，显式 `#include` 其定义头，不要依赖"间接包含"。** 仅靠 `Checker.h`/`CheckerManager.h`/`CheckerContext.h` 等带出的往往只是**前向声明**——能编过声明、却在调用成员函数时报 `invalid use of incomplete type`；或换个编译单元/版本就 `No such file`。include 路径与位置一律**以 LLVM 14 环境实际为准**（尤其本工程是定制 fork 时），下表是上游 LLVM 14 的规范定位，发现与环境不符以环境为准。

| 类型 / 符号 | 定义头（LLVM 14） |
|---|---|
| `Checker`, `check::*` | `clang/StaticAnalyzer/Core/Checker.h` |
| `CheckerManager` | `clang/StaticAnalyzer/Core/CheckerManager.h` |
| `AnalysisManager`（调其成员必须完整定义） | `clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h` |
| `CheckerContext` | `clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h` |
| `ProgramState(Ref)`, `REGISTER_*_WITH_PROGRAMSTATE` | `clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h` |
| `SVal` / `Loc` / `NonLoc` | `clang/StaticAnalyzer/Core/PathSensitive/SVals.h` |
| `SymbolRef`, `SymbolReaper` | `clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h` |
| `MemRegion`（调其成员时） | `clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h` |
| `ExplodedNode`, `ExplodedGraph` | `clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h` |
| `CallEvent`, `CallDescription` | `clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h` |
| `BugType` | `clang/StaticAnalyzer/Core/BugReporter/BugType.h` |
| `BugReporter`, `BasicBugReport`, `PathSensitiveBugReport` | `clang/StaticAnalyzer/Core/BugReporter/BugReporter.h` |
| `PathSensitiveBugReporter` | `clang/StaticAnalyzer/Core/BugReporter/PathSensitiveBugReporter.h` |
| `categories::*` | `clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h` |
| **`PathDiagnosticLocation`, `PathDiagnostic`** | **`clang/Analysis/PathDiagnostic.h`**（**非**旧版 `StaticAnalyzer/Core/BugReporter/`） |
| `AnalysisDeclContext(Manager)` | `clang/Analysis/AnalysisDeclContext.h` |
| 注册宏声明（`registerXXX`/`shouldRegisterXXX`） | `clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h` |
| `Expr`, `MemberExpr`, `CastExpr`, `ArraySubscriptExpr`, `CallExpr` | `clang/AST/Expr.h` |
| `Decl`, `FunctionDecl`, `RecordDecl`, `FieldDecl`, `TypedefNameDecl` | `clang/AST/Decl.h` |
| `RecursiveASTVisitor` | `clang/AST/RecursiveASTVisitor.h` |
| `ASTContext` | `clang/AST/ASTContext.h` |
| `SourceManager` | `clang/Basic/SourceManager.h` |

> `incomplete type` → 你只拿到了前向声明，缺该类型的定义头（如本表 `AnalysisManager`）。`No such file` → 路径错或版本搬迁（如 `PathDiagnostic.h` 已迁到 `clang/Analysis/`）。两类都靠"显式包含定义头 + 以环境实际路径为准"解决。

---

## 1. cast 剥离：用 `IgnoreParenCasts`，别用 `IgnoreParenImpCasts`

判断一个 `Expr` 本质是什么类型/节点时先剥括号+cast：

```cpp
const Expr *stripped = expr->IgnoreParenCasts();   // ✅ 剥括号 + 隐式&显式 cast
if (const auto *call = dyn_cast<CXXMemberCallExpr>(stripped)) { /* ... */ }
```

`IgnoreParenImpCasts` 不剥显式 cast——`(SomeType)x` 剥完仍是 `CStyleCastExpr`，`isa<...>` 判断会失配。

## 2. 取 SVal / MemRegion：用原始 Expr

判类型用 stripped；**取值/取区域用原始 expr**：

```cpp
const Expr *orig = call->getArg(0);                // 原始，不要 IgnoreXXX
SVal v = C.getSVal(orig);                          // ✅
const MemRegion *region = C.getSVal(orig).getAsRegion();
if (region == nullptr) {
    return;
}
```

CSA 路径分析会做死符号清理，stripped 子表达式可能不再对应活符号，`getSVal` 拿到 `UnknownVal`/null。

## 3. 结构体名：typedef 反查

工程约定 `typedef struct XxxTag { ... } Xxx;`，于是 `RecordDecl::getName()` 返回带后缀的 `XxxTag`。诊断要的是干净的 `Xxx`。**优先级：① typedef 反查 clean 名 → ② `RecordDecl::getName()` tag 名 → ③ `"<anonymous struct/union>"`。**

```cpp
// 独立 helper → UpperCamelCase
std::string ResolveRecordName(const RecordDecl *record, const ASTContext &ctx)
{
    if (record == nullptr) {
        return "<anonymous struct/union>";
    }
    const RecordDecl *canonical = record->getCanonicalDecl();

    // ① 遍历 TU 的 typedef，反查 clean 名
    const TranslationUnitDecl *tu = ctx.getTranslationUnitDecl();
    for (const Decl *d : tu->decls()) {
        const auto *typedefDecl = dyn_cast<TypedefNameDecl>(d);
        if (typedefDecl == nullptr) {
            continue;
        }
        const Type *underlying = typedefDecl->getUnderlyingType().getTypePtrOrNull();
        if (underlying == nullptr) {
            continue;
        }
        const auto *rt = underlying->getAs<RecordType>();
        if (rt == nullptr) {
            continue;
        }
        if (rt->getDecl()->getCanonicalDecl() == canonical) {
            return typedefDecl->getNameAsString();
        }
    }

    // ② 退回 tag 名
    if (!record->getName().empty()) {
        return record->getName().str();
    }

    // ③ 匿名兜底
    return "<anonymous struct/union>";
}
```

热点路径可在首次扫描时建 `canonicalDecl -> typedef name` 缓存。union/enum 同理。

> 注意：不要采用"先 `getNameAsString()`"的顺序——在本工程约定下它会直接返回带 `Tag` 后缀的脏名。

## 4. 报告 + BugType category

### path-sensitive

```cpp
ExplodedNode *node = C.generateNonFatalErrorNode();
if (node == nullptr) {
    return;
}
if (!BT) {
    BT.reset(new BugType(this, "Short bug name", categories::MemoryError));
}
auto report = std::make_unique<PathSensitiveBugReport>(*BT, "English diagnostic message", node);
report->addRange(triggerExpr->getSourceRange());
C.emitReport(std::move(report));
```

### AST-level

```cpp
PathDiagnosticLocation loc =
    PathDiagnosticLocation::createBegin(node, br.getSourceManager(), adc);
auto report = std::make_unique<BasicBugReport>(*BT, "English diagnostic message", loc);
report->addRange(node->getSourceRange());
br.emitReport(std::move(report));
```

> **头文件位置（LLVM 14 易错点）**：`PathDiagnosticLocation`/`PathDiagnostic` 在 **`clang/Analysis/PathDiagnostic.h`**，**不是**旧版的 `clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h`（该头早已迁入 libAnalysis）。用到它就显式 `#include "clang/Analysis/PathDiagnostic.h"`，不要靠 `BugReporter.h` 间接带出，也不要凭旧记忆写路径。include 路径一律以 LLVM 14 实际布局为准。

### category 规则（“告警要全”的关键）

`BugType(this, "<bug name>", <category>)` 第三参是分类标签：
- **必须非空英文**；按缺陷真实性质选：贴合 LLVM 内置就用 `categories::MemoryError`/`SecurityError`/`LogicError`/`UnixAPI` 等（见 `CommonBugCategories.h`），不贴合就用准确的自定义英文串。
- **不要为省事统一塞一个值**（会抹平分类、给下游按类过滤埋雷）。
- **category 只用于归类展示，严禁作为过滤/抑制告警的判据**；告警齐全靠 `generateNonFatalErrorNode` + 边界设计保证。
- 同一规则在诊断、README、测试里引用的 category 保持一致。

诊断文案要可行动：尽量含相关符号/字段、触发 sink、关键 size/index/offset。推荐 `"Definite out-of-bounds access on unique-allocated global buffer"`；避免 `"error"`/`"bad access"`/中文。

## 5. 确定性排序去重

`MemRegion`/`SymbolRef`/map 遍历顺序不保证稳定，直接遍历报告会让 CI 抖动。收集后报告前排序：

```cpp
std::sort(findings.begin(), findings.end(), [&](const Finding &a, const Finding &b) {
    return sm.isBeforeInTranslationUnit(a.Loc, b.Loc);
});
// 同 location 去重
```

## 6. 常量值域（APSInt）

```cpp
Expr::EvalResult res;
if (expr->EvaluateAsInt(res, ctx)) {
    llvm::APSInt val = res.Val.getInt();
    unsigned destWidth = ctx.getIntWidth(destType);
    // 比较 val 有效位宽/符号性与 destWidth，判定是否截断；注意 signed/unsigned 混合
}
```

可暴露 `WarnOnConstantTruncation` 等 option 把 TP/FP 取舍交给使用方（option 注册见 registration-build-run.md）。

## 7. 附：参考配方（来自既有规则，非通用强制）

> 以下是某些规则的具体实现片段，按需借用即可，不作为所有 checker 的强制准则。

**函数名提取**（callee 匹配）：

```cpp
static std::string GetCalleeName(const CallExpr *callExpr)
{
    if (callExpr == nullptr) {
        return "";
    }
    const FunctionDecl *callee = callExpr->getDirectCallee();
    if (callee == nullptr) {
        return "";
    }
    return callee->getQualifiedNameAsString();
}

static std::string GetShortFunctionName(StringRef qualifiedName)   // 配置用短名：剥命名空间
{
    size_t pos = qualifiedName.rfind("::");
    if (pos == StringRef::npos) {
        return qualifiedName.str();
    }
    return qualifiedName.substr(pos + 2).str();
}
```

**嵌套成员路径稳定打印**（诊断/README/测试三处保持同一格式）：

```text
格式：object<RecordType>::field<FieldType>::leaf<LeafType>
示例：o<Outer>::inner<Inner>::ptr<int *>
```

变量/字段名与类型名保持绑定，不要混用 `Outer::inner::ptr` 与 `o<Outer>::inner<Inner>::ptr`。
