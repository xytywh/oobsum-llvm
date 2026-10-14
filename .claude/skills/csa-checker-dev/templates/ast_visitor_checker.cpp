//===----------------------------------------------------------------------===//
//
// ASTVisitor 型 Clang Static Analyzer checker 模板（LLVM 14）。
//
// 复制后替换（注意 Checker 后缀的有无）：
//   <CheckerName>Checker        -> 类名 / 文件名 / Checkers.td def 名 / 注册函数名（带 Checker 后缀），如 ParamMemoryAccessChecker
//   <CheckerNameWithoutChecker> -> Checker<"..."> 运行短名 / -checker= 用名（不带 Checker 后缀），如 ParamMemoryAccess
//
// 约定：
//   - 告警文案与 Checkers.td 必须英文。
//   - include 手工分组、不排序（SortIncludes: false）。
//   - 函数/方法定义左大括号换行；控制流不换行；单语句也要 {}。
//   - 判类型用 IgnoreParenCasts；取 SVal/MemRegion 用原始 Expr。
//   - 结构体名诊断走 typedef 反查（clean -> tag -> 匿名）。
//   - BugType category：非空英文、按缺陷性质、仅用于展示，绝不用于过滤。
//
//===----------------------------------------------------------------------===//
#include "clang/AST/Decl.h"                  // Decl
#include "clang/AST/Expr.h"                  // Expr / MemberExpr
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"   // AnalysisDeclContext
#include "clang/Analysis/PathDiagnostic.h"        // PathDiagnosticLocation（LLVM 14：在 clang/Analysis/，非 StaticAnalyzer/Core/BugReporter/）
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"      // BugReporter / BasicBugReport
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h" // AnalysisManager（调用 mgr.getAnalysisDeclContext 必需完整定义）

using namespace clang;
using namespace ento;

namespace {

class <CheckerName>Checker : public Checker<check::ASTCodeBody>
{
public:
    void checkASTCodeBody(const Decl *decl, AnalysisManager &mgr, BugReporter &br) const;

private:
    mutable std::unique_ptr<BugType> BT;

    class Visitor : public RecursiveASTVisitor<Visitor>
    {
    public:
        Visitor(const <CheckerName>Checker *checker, BugReporter &br, AnalysisDeclContext *adc)
            : checker(checker), br(br), adc(adc)
        {
        }
        // VisitXX 回调及同级方法：UpperCamelCase。返回 true 继续遍历。
        bool VisitMemberExpr(MemberExpr *memberExpr);

    private:
        const <CheckerName>Checker *checker;
        BugReporter &br;
        AnalysisDeclContext *adc;
    };

    // 类内私有 helper：优先随既有文件风格，无则 lowerCamelCase。
    void reportBug(const Expr *expr, BugReporter &br, AnalysisDeclContext *adc, StringRef message) const;
};

} // namespace

void <CheckerName>Checker::checkASTCodeBody(const Decl *decl, AnalysisManager &mgr, BugReporter &br) const
{
    if (decl == nullptr) {
        return;
    }
    AnalysisDeclContext *adc = mgr.getAnalysisDeclContext(decl);
    if (adc == nullptr) {
        return;
    }
    Visitor visitor(this, br, adc);
    visitor.TraverseDecl(const_cast<Decl *>(decl));
}

bool <CheckerName>Checker::Visitor::VisitMemberExpr(MemberExpr *memberExpr)
{
    if (memberExpr == nullptr) {
        return true;
    }
    const Expr *baseExpr = memberExpr->getBase();
    if (baseExpr == nullptr) {
        return true;
    }
    // 判类型/形态：用 IgnoreParenCasts 剥括号与显式/隐式 cast。
    const Expr *strippedBase = baseExpr->IgnoreParenCasts();
    if (strippedBase == nullptr) {
        return true;
    }
    // TODO: 替换为实际规则逻辑；命中后调用 checker->reportBug(...)。告警字符串必须英文。
    return true;
}

void <CheckerName>Checker::reportBug(const Expr *expr, BugReporter &br, AnalysisDeclContext *adc, StringRef message) const
{
    if (expr == nullptr || adc == nullptr) {
        return;
    }
    if (!BT) {
        // category 按缺陷性质选：categories::SecurityError / MemoryError / LogicError / UnixAPI 或准确自定义英文串。
        BT.reset(new BugType(this, "<CheckerNameWithoutChecker>", categories::SecurityError));
    }
    PathDiagnosticLocation loc = PathDiagnosticLocation::createBegin(expr, br.getSourceManager(), adc);
    auto report = std::make_unique<BasicBugReport>(*BT, message, loc);
    report->addRange(expr->getSourceRange());
    br.emitReport(std::move(report));
}

void ento::register<CheckerName>Checker(CheckerManager &mgr)
{
    mgr.registerChecker<<CheckerName>Checker>();
}

bool ento::shouldRegister<CheckerName>Checker(const CheckerManager &mgr)
{
    return true;
}
