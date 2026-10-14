//===-- OobSummaryChecker.cpp - CSA Summary 2.0: cross-call OOB --*- C++ -*-===//
//
// rb.enhance.ipa.OobSummary：用函数摘要检测跨多层调用的内存越界（规格 oobsum/06-spec.md，
// 本次改动的 spec 见 oobsum/specs/）。只挂 check::ASTDecl<TranslationUnitDecl>，不挂任何路径敏感回调
// （06- D11：多挂 EndOfTranslationUnit 会让每个函数跑一遍 ExprEngine，沙箱实测）。
//
// 分层（06- §9.1，SPEC-0001 §7）：
//   本文件            入口：选项、TU 回调、调试内建函数、把判定交给 Reporter
//   OobSummary/core   不依赖 Clang：标签、判定、策略、指纹、判定日志（以后的 FIR、抽象域、摘要、求解都在这里）
//   OobSummary/lower  Clang AST/CFG → FIR（WP1 起）
//   OobSummary/report 判定 → CSA 告警
//   OobSummary/debug  调试输出（Dump=…）
//
// 现状（骨架，见 oobsum/STATUS.md）：选项、标签与策略、判定日志、调试输出、调用图顺序已就位；
// 引擎（FIR、摘要、代换判定）尚未接入，目前的判定只来自调试内建函数 oobsum_debug_emit()，
// 用来在 lit 用例里把"判定 → 标签 → 告警 / 日志"这条路走通。
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/DecisionLog.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Policy.h"
#include "OobSummary/debug/DumpSink.h"
#include "OobSummary/report/Reporter.h"

#include <string>
#include <vector>

using namespace clang;
using namespace ento;

namespace {

// 调试内建函数（SPEC-0001 §6.3）：测试源码里声明并调用，checker 认出来后按参数产出一条判定。
//   void oobsum_debug_emit(const char *label, const char *reason, int depth);
// 只认这个名字、参数都是字面量；业务代码里不会有它。引擎接入后，FIR 降级会把 oobsum_dump(x) 等
// 降成调试指令（STATUS 待办），这里的 AST 扫描届时删除。
class DebugIntrinsicVisitor : public RecursiveASTVisitor<DebugIntrinsicVisitor> {
public:
    DebugIntrinsicVisitor(const SourceManager &sm, std::vector<oobsum::Decision> &out) : sm(sm), out(out)
    {
    }

    bool VisitCallExpr(const CallExpr *call)
    {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getName() != "oobsum_debug_emit" || call->getNumArgs() != 3) {
            return true;
        }
        const auto *labelLit = dyn_cast<StringLiteral>(call->getArg(0)->IgnoreParenCasts());
        const auto *reasonLit = dyn_cast<StringLiteral>(call->getArg(1)->IgnoreParenCasts());
        Expr::EvalResult depthValue;
        if (labelLit == nullptr || reasonLit == nullptr || !call->getArg(2)->EvaluateAsInt(depthValue, *astContext)) {
            return true;
        }
        llvm::Optional<oobsum::Label> label = oobsum::ParseLabel(labelLit->getString());
        if (!label) {
            return true;
        }
        PresumedLoc loc = sm.getPresumedLoc(sm.getExpansionLoc(call->getBeginLoc()));
        oobsum::Decision decision;
        decision.label = *label;
        decision.reason = reasonLit->getString().str();
        decision.site = {loc.getFilename(), loc.getLine(), loc.getColumn()};
        decision.fn = currentFunction;
        decision.kind = "W";
        decision.depth = static_cast<unsigned>(depthValue.Val.getInt().getZExtValue());
        decision.msg = "debug decision from oobsum_debug_emit()";
        decision.origin = "debug-intrinsic";
        decision.fp = oobsum::DecisionFingerprint(oobsum::LabelName(decision.label), currentFunction, "debug",
                                                  decision.kind, std::to_string(loc.getLine()), decision.reason);
        out.push_back(std::move(decision));
        return true;
    }

    bool TraverseFunctionDecl(FunctionDecl *fd)
    {
        currentFunction = fd->getNameAsString();
        return RecursiveASTVisitor::TraverseFunctionDecl(fd);
    }

    const ASTContext *astContext = nullptr;

private:
    const SourceManager &sm;
    std::vector<oobsum::Decision> &out;
    std::string currentFunction;
};

class OobSummaryChecker : public Checker<check::ASTDecl<TranslationUnitDecl>> {
public:
    // 选项（06- §4.2，SPEC-0001 §8）
    std::string phase = "inline";
    std::string workDir;
    oobsum::PolicyOptions policy;
    std::string dumpKinds;
    std::string dumpFilter;
    std::string optionsText;  // Dump=options 打印用
    oobsum::DumpSink dump;

    void checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const;

private:
    // F-31：每个标签一个 BugType；category 只用于展示，不用于过滤
    BugType definiteType{this, "Out-of-bound access via call chain (definite)", "Memory error"};
    BugType possibleType{this, "Out-of-bound access via call chain (possible)", "Memory error"};
    BugType suspectType{this, "Out-of-bound access via call chain (suspect)", "Memory error"};
    BugType safeType{this, "Out-of-bound access via call chain (proven safe)", "Memory error"};

    void dumpCallGraph(const TranslationUnitDecl *tu, const std::string &tuId) const;
};

void OobSummaryChecker::dumpCallGraph(const TranslationUnitDecl *tu, const std::string &tuId) const
{
    // 06- A-80：被调者先（scc_iterator 的后序）；递归成环的标 cycle
    CallGraph cg;
    cg.addToCallGraph(const_cast<TranslationUnitDecl *>(tu));
    unsigned index = 0;
    for (auto it = llvm::scc_begin(&cg); !it.isAtEnd(); ++it) {
        std::string line;
        for (const CallGraphNode *node : *it) {
            const auto *fd = dyn_cast_or_null<FunctionDecl>(node->getDecl());
            if (fd == nullptr || !fd->hasBody() || !dump.Wants("callgraph", fd->getName())) {
                continue;
            }
            line += (line.empty() ? "" : " ") + std::string(fd->isExternallyVisible() ? "F:" : "F@" + tuId + ":") +
                    fd->getNameAsString();
        }
        if (!line.empty()) {
            dump.Line("callgraph", "scc #" + std::to_string(index++) + (it.hasCycle() ? " cycle " : " ") + line);
        }
    }
}

void OobSummaryChecker::checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const
{
    const SourceManager &sm = mgr.getSourceManager();
    const FileEntry *mainEntry = sm.getFileEntryForID(sm.getMainFileID());
    std::string mainFile = mainEntry != nullptr ? mainEntry->getName().str() : "<unknown>";
    std::string tuId = oobsum::TuId(mainFile);

    if (dump.On("options")) {
        dump.Line("options", optionsText);
    }
    if (dump.On("callgraph")) {
        dumpCallGraph(tu, tuId);
    }

    // 判定来源：目前只有调试内建函数；引擎接入后在这里加上 lower → solve → apply 产出的判定
    std::vector<oobsum::Decision> decisions;
    DebugIntrinsicVisitor visitor(sm, decisions);
    visitor.astContext = &mgr.getASTContext();
    visitor.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

    oobsum::Reporter reporter({&definiteType, &possibleType, &suspectType, &safeType}, policy);
    for (oobsum::Decision &decision : decisions) {
        reporter.Add(std::move(decision));
    }
    std::vector<std::pair<oobsum::Decision, oobsum::Disposition>> logged = reporter.Flush(br, sm);

    if (dump.On("decisions")) {
        for (const auto &entry : logged) {
            std::string text;
            llvm::raw_string_ostream os(text);
            os << llvm::json::Value(oobsum::DecisionToJson(entry.first, entry.second));
            dump.Line("decisions", os.str());
        }
    }
    std::string error;
    std::string logPath = oobsum::WriteDecisionLog(workDir, tuId, mainFile, logged, error);
    if (!error.empty()) {
        llvm::errs() << "[oobsum] warning: " << error << "\n";
    }
    if (dump.On("stats")) {
        unsigned emitted = 0;
        for (const auto &entry : logged) {
            emitted += entry.second.emit ? 1 : 0;
        }
        dump.Line("stats", "tu=" + tuId + " decisions=" + std::to_string(logged.size()) +
                               " emitted=" + std::to_string(emitted) + (logPath.empty() ? "" : " log=" + logPath));
    }
}

}  // namespace

void ento::registerOobSummaryChecker(CheckerManager &mgr)
{
    auto *chk = mgr.registerChecker<OobSummaryChecker>();
    const AnalyzerOptions &opts = mgr.getAnalyzerOptions();

    chk->phase = opts.getCheckerStringOption(chk, "Phase").str();
    if (chk->phase != "inline" && chk->phase != "export" && chk->phase != "solve" && chk->phase != "report") {
        mgr.reportInvalidCheckerOptionValue(chk, "Phase", "one of 'inline', 'export', 'solve', 'report'");
    }
    chk->workDir = opts.getCheckerStringOption(chk, "WorkDir").str();

    std::string policyName = opts.getCheckerStringOption(chk, "Policy").str();
    if (policyName != "recall" && policyName != "balanced") {
        mgr.reportInvalidCheckerOptionValue(chk, "Policy", "either 'recall' or 'balanced'");
    }
    chk->policy.recall = policyName != "balanced";
    int inlineDepth = opts.getCheckerIntegerOption(chk, "InlineDepth");
    if (inlineDepth < 0) {
        mgr.reportInvalidCheckerOptionValue(chk, "InlineDepth", "a non-negative integer");
        inlineDepth = 0;
    }
    chk->policy.inlineDepth = static_cast<unsigned>(inlineDepth);
    chk->policy.reportSafe = opts.getCheckerBooleanOption(chk, "ReportSafe");
    chk->policy.suspectReasons = oobsum::SplitList(opts.getCheckerStringOption(chk, "SuspectReasons"));

    chk->dumpKinds = opts.getCheckerStringOption(chk, "Dump").str();
    chk->dumpFilter = opts.getCheckerStringOption(chk, "DumpFilter").str();
    std::string unknownKind;
    if (!chk->dump.Configure(chk->dumpKinds, chk->dumpFilter, chk->workDir, unknownKind)) {
        mgr.reportInvalidCheckerOptionValue(chk, "Dump",
                                            "kinds separated by '+' from options, callgraph, fir, state, summary, "
                                            "apply, decisions, stats, all (and a valid DumpFilter regex); got '" +
                                                unknownKind + "'");
    }
    chk->optionsText = "Phase=" + chk->phase + " WorkDir=\"" + chk->workDir + "\" Policy=" + policyName +
                       " InlineDepth=" + std::to_string(chk->policy.inlineDepth) +
                       " ReportSafe=" + (chk->policy.reportSafe ? "true" : "false") + " SuspectReasons=\"" +
                       opts.getCheckerStringOption(chk, "SuspectReasons").str() + "\" Dump=\"" + chk->dumpKinds +
                       "\" DumpFilter=\"" + chk->dumpFilter + "\"";
}

bool ento::shouldRegisterOobSummaryChecker(const CheckerManager &mgr)
{
    return true;
}
