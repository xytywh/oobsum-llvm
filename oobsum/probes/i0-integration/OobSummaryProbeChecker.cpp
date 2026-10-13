//===-- OobSummaryProbeChecker.cpp - CSA Summary 2.0 integration probe ----===//
//
// I0 集成探针（v3.12）：在写 OobSummary 正式代码之前，一次性确认它要依赖的
// 宿主能力，并顺带统计全量代码的形态数据。只读、不改任何状态、不影响其它 checker。
//
// 它回答（每一项在 bxen 输出里都有一行 [OSP] 前缀的结果）：
//   1. 构建：fork 的 LLVM/Clang 版本串、C++ 标准、RTTI / 异常开关       -> [OSP] build
//   2. 选项：-extra_args 里的 -analyzer-config <checker>:Opt=Val 能否透传 -> [OSP] opt
//   3. 目标 ABI：分析业务代码时指针 / long / size_t 的字节数与大小端     -> [OSP] tu
//   4. 回调：只挂 check::ASTDecl<TranslationUnitDecl>（TU 级，在逐函数遍历之前调用一次，整棵 AST 已就绪）。
//      沙箱实测【上游 14.0.6】：多挂一个空的 check::EndOfTranslationUnit，-analyzer-stats 的计时表里
//      就出现 "Path exploration time"——CSA 把它算作路径敏感回调，对每个函数跑 ExprEngine；
//      只挂 ASTDecl 时没有这一行。正式 checker 照此选择，不挂任何路径敏感回调。
//   5. 调用图：clang::CallGraph + scc_iterator 是否按"被调者先"给出顺序  -> [OSP] scc
//   6. 告警：BasicBugReport + addNote 在 bxen 里的呈现格式（只在测试用例上开）
//   7. 落盘：WorkDir 下每个 TU 写一份 JSON（函数、CFG 规模、调用边、耗时），
//      由 probe_stats.py 汇总成全量统计（函数指针占比、CFG 规模分布、跨 TU 调用、
//      多轮扫描需要几轮）。
//   8. 子目录：哈希函数放在 OobSummary/ProbeSubdirUnit.cpp，验证"入口平铺 + 引擎在子目录"的布局能编译链接
//
// 选项（Checkers.td 的 CheckerOptions，见 checkers_td_snippet.td）：
//   WorkDir          string  ""     非空时每个 TU 写 <WorkDir>/tu-<hash>.json
//   EmitProbeReport  bool    false  true 时每个 TU 报一条探针告警（只在测试用例上开）
//
// 两种构建方式共用本文件：
//   fork 内（默认）：Checkers.td 注册，本文件末尾提供 ento::register/shouldRegister…
//   沙箱插件（-DOOBSUM_PLUGIN_BUILD）：上游 LLVM 14.0.6 的 clang -cc1 -load 加载
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "OobSummary/ProbeSubdirUnit.h"

#ifdef OOBSUM_PLUGIN_BUILD
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#endif

using namespace clang;
using namespace ento;

namespace {

// 一个函数的形态统计（全部在 checkASTCodeBody 里采集，EndOfTranslationUnit 汇总落盘）
struct ProbeFuncStat {
    std::string key;
    std::string name;
    bool externalLinkage = false;
    bool inMainFile = false;
    unsigned params = 0;
    unsigned blocks = 0;
    unsigned elements = 0;
    unsigned loops = 0;
    unsigned directCalls = 0;
    unsigned indirectCalls = 0;
    std::vector<std::string> callees;  // 直接调用的被调者 key（去重前）
    double cfgMs = 0.0;
};

class OobSummaryProbeChecker : public Checker<check::ASTDecl<TranslationUnitDecl>> {
public:
    std::string workDir;
    bool emitProbeReport = false;

    void checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const;

private:
    BugType probeBugType{this, "OobSummary integration probe", "Probe"};
    mutable std::map<const FunctionDecl *, ProbeFuncStat> stats;
    mutable bool optionsPrinted = false;

    std::string tuIdOf(const SourceManager &sm) const;
    std::string funcKeyOf(const FunctionDecl *fd, const std::string &tuId) const;
    void printBuildAndOptions() const;
    void collectCalls(const CFG &cfg, ProbeFuncStat &stat, const std::string &tuId) const;
    void collectFunction(const FunctionDecl *fd, AnalysisManager &mgr, const std::string &tuId) const;
    void writeTuJson(const std::string &mainFile, const std::string &tuId, const ASTContext &ctx,
                     const std::vector<std::vector<std::string>> &sccs, double tuMs) const;
};

// 主文件路径规范化后的哈希：驱动脚本用同一规则从 compile_commands.json 的 file 字段算出来
std::string OobSummaryProbeChecker::tuIdOf(const SourceManager &sm) const
{
    const FileEntry *fe = sm.getFileEntryForID(sm.getMainFileID());
    if (fe == nullptr) {
        return "unknown";
    }
    return oobsum::HashNormalizedPath(fe->getName());
}

// 函数键：外部链接用名字；内部链接（static）加 TU 前缀，跨 TU 永不查找
std::string OobSummaryProbeChecker::funcKeyOf(const FunctionDecl *fd, const std::string &tuId) const
{
    std::string name = fd->getNameAsString();
    if (fd->isExternallyVisible()) {
        return "F:" + name;
    }
    return "F@" + tuId + ":" + name;
}

void OobSummaryProbeChecker::printBuildAndOptions() const
{
    if (optionsPrinted) {
        return;
    }
    optionsPrinted = true;
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
    const char *rtti = "on";
#else
    const char *rtti = "off";
#endif
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    const char *exceptions = "on";
#else
    const char *exceptions = "off";
#endif
    llvm::errs() << "[OSP] build clang=" << CLANG_VERSION_STRING << " cxx=" << __cplusplus << " rtti=" << rtti
                 << " exceptions=" << exceptions << "\n";
    llvm::errs() << "[OSP] opt WorkDir=\"" << workDir << "\" EmitProbeReport=" << (emitProbeReport ? "true" : "false")
                 << "\n";
}

void OobSummaryProbeChecker::collectCalls(const CFG &cfg, ProbeFuncStat &stat, const std::string &tuId) const
{
    for (const CFGBlock *block : cfg) {
        if (block == nullptr) {
            continue;
        }
        if (block->getLoopTarget() != nullptr) {
            ++stat.loops;
        }
        for (const CFGElement &elem : *block) {
            ++stat.elements;
            llvm::Optional<CFGStmt> cs = elem.getAs<CFGStmt>();
            if (!cs) {
                continue;
            }
            const auto *call = dyn_cast<CallExpr>(cs->getStmt());
            if (call == nullptr) {
                continue;
            }
            const FunctionDecl *callee = call->getDirectCallee();
            if (callee == nullptr) {
                ++stat.indirectCalls;
                continue;
            }
            if (callee->getBuiltinID() != 0) {
                continue;
            }
            ++stat.directCalls;
            stat.callees.push_back(funcKeyOf(callee, tuId));
        }
    }
}

void OobSummaryProbeChecker::collectFunction(const FunctionDecl *fd, AnalysisManager &mgr,
                                             const std::string &tuId) const
{
    const SourceManager &sm = mgr.getSourceManager();
    auto start = std::chrono::steady_clock::now();
    const CFG *cfg = mgr.getCFG(fd);
    auto stop = std::chrono::steady_clock::now();

    ProbeFuncStat &stat = stats[fd];
    stat.key = funcKeyOf(fd, tuId);
    stat.name = fd->getNameAsString();
    stat.externalLinkage = fd->isExternallyVisible();
    stat.inMainFile = sm.isInMainFile(fd->getLocation());
    stat.params = fd->getNumParams();
    stat.cfgMs = std::chrono::duration<double, std::milli>(stop - start).count();
    if (cfg == nullptr) {
        return;
    }
    stat.blocks = cfg->getNumBlockIDs();
    collectCalls(*cfg, stat, tuId);
}

void OobSummaryProbeChecker::writeTuJson(const std::string &mainFile, const std::string &tuId, const ASTContext &ctx,
                                         const std::vector<std::vector<std::string>> &sccs, double tuMs) const
{
    llvm::json::Array funcs;
    for (const auto &entry : stats) {
        const ProbeFuncStat &s = entry.second;
        llvm::json::Array callees;
        for (const std::string &c : s.callees) {
            callees.push_back(c);
        }
        funcs.push_back(llvm::json::Object{{"key", s.key},
                                           {"name", s.name},
                                           {"ext", s.externalLinkage},
                                           {"main", s.inMainFile},
                                           {"params", static_cast<int64_t>(s.params)},
                                           {"blocks", static_cast<int64_t>(s.blocks)},
                                           {"elems", static_cast<int64_t>(s.elements)},
                                           {"loops", static_cast<int64_t>(s.loops)},
                                           {"direct", static_cast<int64_t>(s.directCalls)},
                                           {"indirect", static_cast<int64_t>(s.indirectCalls)},
                                           {"callees", std::move(callees)},
                                           {"cfg_ms", s.cfgMs}});
    }
    llvm::json::Array sccArr;
    for (const auto &scc : sccs) {
        llvm::json::Array one;
        for (const std::string &k : scc) {
            one.push_back(k);
        }
        sccArr.push_back(std::move(one));
    }
    const TargetInfo &ti = ctx.getTargetInfo();
    llvm::json::Object root{{"schema", "oobsum-probe/1"},
                            {"file", mainFile},
                            {"tu", tuId},
                            {"triple", ti.getTriple().str()},
                            {"ptr_bytes", static_cast<int64_t>(ti.getPointerWidth(0) / 8)},
                            {"long_bytes", static_cast<int64_t>(ti.getLongWidth() / 8)},
                            {"big_endian", ti.isBigEndian()},
                            {"tu_ms", tuMs},
                            {"funcs", std::move(funcs)},
                            {"sccs", std::move(sccArr)}};
    SmallString<256> out(workDir);
    llvm::sys::path::append(out, "tu-" + tuId + ".json");
    std::error_code ec;
    llvm::raw_fd_ostream os(out, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        llvm::errs() << "[OSP] write-failed " << out << " : " << ec.message() << "\n";
        return;
    }
    os << llvm::formatv("{0:2}", llvm::json::Value(std::move(root))) << "\n";
    llvm::errs() << "[OSP] wrote " << out << "\n";
}

void OobSummaryProbeChecker::checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const
{
    printBuildAndOptions();
    auto start = std::chrono::steady_clock::now();
    const ASTContext &ctx = mgr.getASTContext();
    const SourceManager &sm = mgr.getSourceManager();
    const TargetInfo &ti = ctx.getTargetInfo();
    std::string tuId = tuIdOf(sm);
    const FileEntry *fe = sm.getFileEntryForID(sm.getMainFileID());
    std::string mainFile = fe != nullptr ? fe->getName().str() : std::string("<unknown>");

    // 逐个顶层函数定义建 CFG（C 只有顶层函数；C++ 的命名空间 / 类成员不在 P0 范围）
    for (const Decl *decl : tu->decls()) {
        const auto *fd = dyn_cast<FunctionDecl>(decl);
        if (fd == nullptr || !fd->doesThisDeclarationHaveABody()) {
            continue;
        }
        collectFunction(fd, mgr, tuId);
    }

    llvm::errs() << "[OSP] tu file=" << mainFile << " id=" << tuId << " triple=" << ti.getTriple().str()
                 << " ptr=" << ti.getPointerWidth(0) / 8 << " long=" << ti.getLongWidth() / 8
                 << " sizet=" << ctx.getTypeSize(ctx.getSizeType()) / 8
                 << " endian=" << (ti.isBigEndian() ? "big" : "little") << " funcs=" << stats.size() << "\n";

    // 调用图：scc_iterator 按后序给出强连通分量，期望"被调者先、调用者后"
    CallGraph cg;
    cg.addToCallGraph(const_cast<TranslationUnitDecl *>(tu));
    std::vector<std::vector<std::string>> sccs;
    unsigned index = 0;
    for (auto it = llvm::scc_begin(&cg); !it.isAtEnd(); ++it) {
        std::vector<std::string> keys;
        for (const CallGraphNode *node : *it) {
            // 图里也有只声明、没定义的被调者（includeCalleeInGraph 允许声明）：只列有函数体的
            const auto *fd = dyn_cast_or_null<FunctionDecl>(node->getDecl());
            if (fd == nullptr || !fd->hasBody()) {
                continue;
            }
            keys.push_back(funcKeyOf(fd, tuId));
        }
        if (keys.empty()) {
            continue;
        }
        std::string line;
        for (size_t i = 0; i < keys.size(); ++i) {
            line += (i == 0 ? "" : ",") + keys[i];
        }
        llvm::errs() << "[OSP] scc #" << index++ << (it.hasCycle() ? " cycle " : " ") << line << "\n";
        sccs.push_back(std::move(keys));
    }
    for (const auto &entry : stats) {
        const ProbeFuncStat &s = entry.second;
        if (!s.inMainFile) {
            continue;
        }
        llvm::errs() << "[OSP] func " << s.key << " blocks=" << s.blocks << " loops=" << s.loops
                     << " direct=" << s.directCalls << " indirect=" << s.indirectCalls << "\n";
    }

    auto stop = std::chrono::steady_clock::now();
    double tuMs = std::chrono::duration<double, std::milli>(stop - start).count();
    for (const auto &entry : stats) {
        tuMs += entry.second.cfgMs;
    }
    if (!workDir.empty()) {
        writeTuJson(mainFile, tuId, ctx, sccs, tuMs);
    }

    // 告警格式探针：报在主文件第一个函数定义上，附一条注释指向第二个函数
    if (emitProbeReport) {
        std::vector<const FunctionDecl *> mainFuncs;
        for (const auto &entry : stats) {
            if (entry.second.inMainFile) {
                mainFuncs.push_back(entry.first);
            }
        }
        std::sort(mainFuncs.begin(), mainFuncs.end(), [&sm](const FunctionDecl *a, const FunctionDecl *b) {
            return sm.isBeforeInTranslationUnit(a->getLocation(), b->getLocation());
        });
        if (!mainFuncs.empty()) {
            std::string msg = "OobSummary probe: " + std::to_string(mainFuncs.size()) +
                              " function(s) analyzed in this translation unit";
            auto report = std::make_unique<BasicBugReport>(probeBugType, msg,
                                                           PathDiagnosticLocation::create(mainFuncs.front(), sm));
            if (mainFuncs.size() > 1) {
                report->addNote("Probe note: second function in this translation unit",
                                PathDiagnosticLocation::create(mainFuncs[1], sm));
            }
            br.emitReport(std::move(report));
        }
    }
    stats.clear();
}

void registerProbeWithOptions(CheckerManager &mgr)
{
    auto *chk = mgr.registerChecker<OobSummaryProbeChecker>();
    const AnalyzerOptions &opts = mgr.getAnalyzerOptions();
    chk->workDir = opts.getCheckerStringOption(chk, "WorkDir").str();
    chk->emitProbeReport = opts.getCheckerBooleanOption(chk, "EmitProbeReport");
}

}  // namespace

#ifdef OOBSUM_PLUGIN_BUILD

// 沙箱：上游 LLVM 14.0.6 插件入口（fork 内不编译这一段）
extern "C" const char clang_analyzerAPIVersionString[] = CLANG_ANALYZER_API_VERSION_STRING;

static bool shouldRegisterProbe(const CheckerManager &mgr)
{
    return true;
}

extern "C" void clang_registerCheckers(CheckerRegistry &registry)
{
    registry.addChecker(registerProbeWithOptions, shouldRegisterProbe, "rb.enhance.core.OobSummaryProbe",
                        "Integration probe for CSA Summary 2.0 (read-only)", "", false);
    registry.addCheckerOption("string", "rb.enhance.core.OobSummaryProbe", "WorkDir", "",
                              "Directory for per-TU probe JSON (empty: do not write)", "released");
    registry.addCheckerOption("bool", "rb.enhance.core.OobSummaryProbe", "EmitProbeReport", "false",
                              "Emit one probe report per translation unit", "released");
}

#else

void ento::registerOobSummaryProbeChecker(CheckerManager &mgr)
{
    registerProbeWithOptions(mgr);
}

bool ento::shouldRegisterOobSummaryProbeChecker(const CheckerManager &mgr)
{
    return true;
}

#endif
