# AST-visitor Checker 范式

适用：缺陷能在单个 AST 子树/类型签名上判定（类型不匹配、危险 cast、函数指针调用形态、结构体成员访问形态、声明/命名规约）。便宜、确定性强、CI 友好。可编译骨架见 `templates/ast_visitor_checker.cpp`。

## 1. 基类

- `Checker<check::ASTCodeBody>` — 对每个有函数体的 `Decl` 触发 `checkASTCodeBody`，在里面跑内嵌 `RecursiveASTVisitor`。最常用。
- `Checker<check::ASTDecl<RecordDecl>>` 等 — 针对特定声明类型，适合声明级规约。
- 需 TU 级两遍遍历（先收集后判定）时用 `check::EndOfTranslationUnit` 收尾。

## 2. 命名（本范式）

- 内嵌 `RecursiveASTVisitor` 的 `VisitXX` 回调及同级方法 → UpperCamelCase。
- checker 类的回调 `checkASTCodeBody` 随 CSA API。
- 类内私有 helper（如 `reportBug`）→ 优先随既有文件风格，无则 lowerCamelCase。
- 局部变量 lowerCamelCase；单语句控制流带 `{}`。

## 3. 结构要点

```cpp
class MyChecker : public Checker<check::ASTCodeBody>
{
public:
    void checkASTCodeBody(const Decl *decl, AnalysisManager &mgr, BugReporter &br) const;

private:
    mutable std::unique_ptr<BugType> BT;

    class Visitor : public RecursiveASTVisitor<Visitor>
    {
    public:
        Visitor(const MyChecker *checker, BugReporter &br, AnalysisDeclContext *adc)
            : checker(checker), br(br), adc(adc)
        {
        }
        bool VisitMemberExpr(MemberExpr *memberExpr);   // 收集/判定，UpperCamelCase

    private:
        const MyChecker *checker;
        BugReporter &br;
        AnalysisDeclContext *adc;
    };

    void reportBug(const Expr *expr, BugReporter &br, AnalysisDeclContext *adc, StringRef message) const;
};
```

- 判类型先 `IgnoreParenCasts` 剥括号与显式/隐式 cast；判定逻辑要把模糊语义裁剪成可解释条件。
- 报告用 AST-level 形式：`PathDiagnosticLocation::createBegin(expr, br.getSourceManager(), adc)` + `BasicBugReport`，诊断英文。`PathDiagnosticLocation` 需显式 `#include "clang/Analysis/PathDiagnostic.h"`（LLVM 14 在 `clang/Analysis/`，非旧版 `StaticAnalyzer/Core/BugReporter/`）。
- `BugType` 第三参 category：非空英文、按缺陷性质（`categories::SecurityError`/`MemoryError`/`LogicError` 或准确自定义串），仅用于展示。

## 4. 两遍遍历 / 别名定点模式

当判定需"先收集全量、再统一判定"（结构体成员指针数组访问、跨语句别名传播）：

1. 第一遍 `RecursiveASTVisitor` 收集候选（声明、赋值、别名边）到成员容器；收集阶段不报告。
2. 在 `check::EndOfTranslationUnit`/收集后跑**别名定点迭代**（worklist/不动点）传播到收敛。
3. 第二遍在收敛后的集合上判定与报告。

报告顺序按 source location 排序、同 location 去重，保证 **CI 确定性**（见 csa-api-cookbook.md）。

## 5. 常见判定素材

- 类型：`QualType`/`getCanonicalType()`/`isIntegerType()`/`getIntWidth()`。
- cast：`CastExpr::getCastKind()`（`CK_IntegralCast` 等）/`getSubExpr()`。
- 常量：`Expr::EvaluateAsInt` + `APSInt` 做常量值域/截断判定。
- 声明/记录：`RecordDecl`/`FieldDecl`/`TypedefNameDecl`（结构体名反查见 cookbook）。

## 6. 自检
- 能否不看路径条件就判定？不能 → 改 path-sensitive。
- 误报主要来自"语法像但语义不构成缺陷"——边界是否显式排除？
- 漏报是否都在 README 写明并 justify？
