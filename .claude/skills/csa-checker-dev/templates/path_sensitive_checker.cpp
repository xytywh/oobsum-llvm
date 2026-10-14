//===----------------------------------------------------------------------===//
//
// 路径敏感 Clang Static Analyzer checker 模板（LLVM 14）。
//
// 复制后替换（注意 Checker 后缀的有无）：
//   <CheckerName>Checker        -> 类名 / 文件名 / Checkers.td def 名 / 注册函数名（带 Checker 后缀），如 ParamMemoryAccessChecker
//   <CheckerNameWithoutChecker> -> Checker<"..."> 运行短名 / -checker= 用名（不带 Checker 后缀），如 ParamMemoryAccess
//
// 约定：
//   - 告警文案与 Checkers.td 必须英文。
//   - include 手工分组、不排序。函数定义左大括号换行；控制流不换行；单语句也要 {}。
//   - IgnoreParenCasts 只用于 AST 形态判断；取 SVal/MemRegion 用原始 Expr。
//   - 报告优先 generateNonFatalErrorNode；多报告按 source location 排序保证确定性。
//   - BugType category：非空英文、按缺陷性质、仅用于展示，绝不用于过滤。
//   - 包路由：单函数规则 -> rb.enhance.core（ipa=none）；跨函数单文件 -> rb.enhance.ipa（开 IPA）。
//
//===----------------------------------------------------------------------===//
#include "clang/AST/Expr.h"                  // ArraySubscriptExpr / Expr
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"      // PathSensitiveBugReport
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathSensitiveBugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"  // ExplodedNode
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"          // SVal

using namespace clang;
using namespace ento;

namespace {

class <CheckerName>Checker : public Checker<check::PreStmt<ArraySubscriptExpr>>
{
public:
    // path-sensitive 回调：lowerCamelCase（贴合 CSA API）。
    void checkPreStmt(const ArraySubscriptExpr *expr, CheckerContext &C) const;

private:
    mutable std::unique_ptr<BugType> BT;
    void reportBug(const Expr *expr, CheckerContext &C, StringRef message) const;
};

} // namespace

void <CheckerName>Checker::checkPreStmt(const ArraySubscriptExpr *expr, CheckerContext &C) const
{
    if (expr == nullptr) {
        return;
    }
    const Expr *baseExpr = expr->getBase();
    const Expr *idxExpr = expr->getIdx();
    if (baseExpr == nullptr || idxExpr == nullptr) {
        return;
    }
    // IgnoreParenCasts 只用于 AST 形态判断。
    const Expr *strippedBase = baseExpr->IgnoreParenCasts();
    if (strippedBase == nullptr) {
        return;
    }
    // 取路径敏感信息用原始 Expr（不要用 stripped）。
    SVal baseVal = C.getSVal(baseExpr);
    SVal idxVal = C.getSVal(idxExpr);
    if (baseVal.isUnknownOrUndef() || idxVal.isUnknownOrUndef()) {
        return;
    }
    // TODO: 替换为实际规则逻辑；命中后调用 reportBug(...)。告警字符串必须英文。
    return;
}

void <CheckerName>Checker::reportBug(const Expr *expr, CheckerContext &C, StringRef message) const
{
    // 优先 non-fatal，避免同路径多 issue 被截断造成漏报。
    ExplodedNode *node = C.generateNonFatalErrorNode();
    if (node == nullptr) {
        return;
    }
    if (!BT) {
        // category 按缺陷性质选：categories::MemoryError / SecurityError / LogicError 或准确自定义英文串。
        BT.reset(new BugType(this, "<CheckerNameWithoutChecker>", categories::MemoryError));
    }
    auto report = std::make_unique<PathSensitiveBugReport>(*BT, message, node);
    if (expr != nullptr) {
        report->addRange(expr->getSourceRange());
    }
    C.emitReport(std::move(report));
}

void ento::register<CheckerName>Checker(CheckerManager &mgr)
{
    mgr.registerChecker<<CheckerName>Checker>();
}

bool ento::shouldRegister<CheckerName>Checker(const CheckerManager &mgr)
{
    return true;
}
