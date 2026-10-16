//===-- OobSummaryChecker.cpp - CSA Summary 2.0: cross-call OOB --*- C++ -*-===//
//
// rb.enhance.ipa.OobSummary：用函数摘要检测跨多层调用的内存越界（规格 oobsum/06-spec.md，
// 本次改动的 spec 见 oobsum/specs/）。只挂 check::ASTDecl<TranslationUnitDecl>，不挂任何路径敏感回调
// （06- D11：多挂 EndOfTranslationUnit 会让每个函数跑一遍 ExprEngine，沙箱实测）。
//
// 分层（06- §9.1，SPEC-0001 §7）：
//   本文件            入口：选项、TU 回调、调试内建函数、把判定交给 Reporter
//   OobSummary/core   不依赖 Clang：标签、判定、策略、指纹、判定日志（以后的 FIR、抽象域、摘要、求解都在这里）
//   OobSummary/lower  Clang AST/CFG → FIR（WP1 起）；函数 / 全局的键（SPEC-0003）
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
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/DecisionLog.h"

#include <memory>
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/FirJson.h"
#include "OobSummary/core/Engine.h"
#include "OobSummary/core/FirText.h"
#include "OobSummary/core/Interp.h"
#include "OobSummary/core/Models.h"
#include "OobSummary/core/Policy.h"
#include "OobSummary/core/Summary.h"
#include "OobSummary/debug/DumpSink.h"
#include "OobSummary/lower/Keys.h"
#include "OobSummary/lower/Lowering.h"
#include "OobSummary/report/Reporter.h"

#include <set>
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
    DebugIntrinsicVisitor(const SourceManager &sm, oobsum::KeyMaker &keys, std::vector<oobsum::Decision> &out)
        : sm(sm), keys(keys), out(out)
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
        // 判定的 fn 与指纹的入口都用函数键（SPEC-0003 §2.2）
        currentFunction = keys.ForFunction(fd);
        return RecursiveASTVisitor::TraverseFunctionDecl(fd);
    }

    const ASTContext *astContext = nullptr;

private:
    const SourceManager &sm;
    oobsum::KeyMaker &keys;
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
    std::string modelFile;
    oobsum::models::ModelTable models = oobsum::models::ModelTable::Default();  // 内置表 + ModelFile（SPEC-0033）
    oobsum::DumpSink dump;

    void checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const;

private:
    // F-31：每个标签一个 BugType；category 只用于展示，不用于过滤
    BugType definiteType{this, "Out-of-bound access via call chain (definite)", "Memory error"};
    BugType possibleType{this, "Out-of-bound access via call chain (possible)", "Memory error"};
    BugType suspectType{this, "Out-of-bound access via call chain (suspect)", "Memory error"};
    BugType safeType{this, "Out-of-bound access via call chain (proven safe)", "Memory error"};

    void dumpCallGraph(const TranslationUnitDecl *tu, oobsum::KeyMaker &keys) const;
    void dumpKeys(const TranslationUnitDecl *tu, const std::string &tuId, oobsum::KeyMaker &keys) const;
    void dumpFir(const oobsum::fir::Module &module, const std::string &tuId) const;
    std::string writeFir(const oobsum::fir::Module &module, const std::string &tuId) const;
    void solveAll() const;
    void loadSolved(const std::string &tuId, std::vector<oobsum::Decision> &out) const;
    void dumpSummaries(const oobsum::fir::Module &module, const std::string &tuId,
                       const oobsum::engine::InlineResult &result) const;
};

void OobSummaryChecker::dumpCallGraph(const TranslationUnitDecl *tu, oobsum::KeyMaker &keys) const
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
            line += (line.empty() ? "" : " ") + keys.ForFunction(fd);
        }
        if (!line.empty()) {
            dump.Line("callgraph", "scc #" + std::to_string(index++) + (it.hasCycle() ? " cycle " : " ") + line);
        }
    }
}

void OobSummaryChecker::dumpKeys(const TranslationUnitDecl *tu, const std::string &tuId, oobsum::KeyMaker &keys) const
{
    // SPEC-0003 §2.3：键表；某个文件第一次被引用时先打一行 file，把 id 对回路径
    std::string content;
    std::set<std::string> shownFiles;
    for (const oobsum::KeyInfo &info : oobsum::CollectKeys(tu, keys)) {
        if (!dump.Wants("keys", info.scopeName)) {
            continue;
        }
        if (shownFiles.insert(info.fileId).second) {
            std::string fileLine = "file " + info.fileId + " " + info.file;
            dump.Line("keys", fileLine);
            content += fileLine + "\n";
        }
        std::string line = info.key + " " + info.kind + (info.def ? " def" : " decl") + (info.header ? " header" : "") +
                           " " + info.file + ":" + std::to_string(info.line) + ":" + std::to_string(info.col);
        dump.Line("keys", line);
        content += line + "\n";
    }
    dump.File(tuId + "/keys/keys.txt", content);
}

// Phase=solve（SPEC-0029 §2.2）：读 WorkDir/fir/*.json（读不了的跳过）→ RunSolve → 判定按汇合点函数所在的 TU 分组，
// 写 WorkDir/solved/<tu-id>.json（没有判定的 TU 也写空列表）
void OobSummaryChecker::solveAll() const
{
    if (workDir.empty()) {
        llvm::errs() << "[oobsum] warning: Phase=solve needs WorkDir; nothing solved\n";
        return;
    }
    llvm::SmallString<256> firDir(workDir);
    llvm::sys::path::append(firDir, "fir");
    std::vector<std::string> paths;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(firDir, ec), end; it != end && !ec; it.increment(ec)) {
        if (llvm::sys::path::extension(it->path()) == ".json") {
            paths.push_back(it->path());
        }
    }
    std::sort(paths.begin(), paths.end());
    std::vector<std::unique_ptr<oobsum::fir::Module>> modules;
    for (const std::string &path : paths) {
        auto m = std::make_unique<oobsum::fir::Module>();
        std::string error;
        if (!oobsum::fir::ReadModuleFile(path, *m, error)) {
            llvm::errs() << "[oobsum] warning: skip " << path << ": " << error << "\n";  // X-03：坏文件跳过并记日志
            continue;
        }
        modules.push_back(std::move(m));
    }
    std::vector<const oobsum::fir::Module *> mods;
    for (const auto &m : modules) {
        mods.push_back(m.get());
    }
    oobsum::engine::InlineResult result = oobsum::engine::RunSolve(mods, models);
    // Dump=summary：全局求解后的每个函数的摘要（SPEC-0032；engine_check.py 的跨 TU 用例用它）
    for (const auto &entry : result.summaries) {
        if (dump.Wants("summary", entry.second.fn)) {
            std::string text;
            llvm::raw_string_ostream os(text);
            os << oobsum::summary::SummaryToJson(entry.second);
            dump.Line("summary", os.str());
        }
    }
    for (const std::string &line : result.applyLines) {
        if (dump.Wants("apply", line.substr(0, line.find(' ')))) {
            dump.Line("apply", line);
        }
    }
    std::vector<llvm::json::Array> byModule(mods.size());
    for (const oobsum::Decision &d : result.decisions) {
        auto it = result.moduleOf.find(d.fn);
        if (it == result.moduleOf.end()) {
            continue;
        }
        llvm::json::Object o = oobsum::DecisionToJson(d, oobsum::Disposition());
        o.erase("emitted");  // "报没报"由 report 阶段的策略定
        o.erase("not_emitted_because");
        byModule[it->second].push_back(std::move(o));
    }
    llvm::SmallString<256> outDir(workDir);
    llvm::sys::path::append(outDir, "solved");
    llvm::sys::fs::create_directories(outDir);
    for (size_t i = 0; i < mods.size(); ++i) {
        llvm::SmallString<256> path(outDir);
        llvm::sys::path::append(path, mods[i]->tu + ".json");
        std::string tmp = path.str().str() + ".tmp";
        std::error_code wec;
        llvm::raw_fd_ostream os(tmp, wec, llvm::sys::fs::OF_Text);
        if (wec) {
            llvm::errs() << "[oobsum] warning: cannot write " << tmp << ": " << wec.message() << "\n";
            continue;
        }
        os << llvm::json::Value(llvm::json::Object{{"schema", "oobsum-solved/1"},
                                                   {"tu", mods[i]->tu},
                                                   {"file", mods[i]->file},
                                                   {"decisions", std::move(byModule[i])}})
           << "\n";
        os.close();
        if (std::error_code rec = llvm::sys::fs::rename(tmp, path)) {
            llvm::errs() << "[oobsum] warning: cannot rename " << tmp << ": " << rec.message() << "\n";
        }
    }
    if (dump.On("stats")) {
        dump.Line("stats", "solve tus=" + std::to_string(mods.size()) +
                               " functions=" + std::to_string(result.summaries.size()) +
                               " decisions=" + std::to_string(result.decisions.size()));
    }
}

// Phase=report（SPEC-0030）：读 WorkDir/solved/<tu-id>.json 的判定；没有就提示一行（没被求解，或 TU id 对不上）
void OobSummaryChecker::loadSolved(const std::string &tuId, std::vector<oobsum::Decision> &out) const
{
    llvm::SmallString<256> path(workDir);
    llvm::sys::path::append(path, "solved", tuId + ".json");
    auto buffer = llvm::MemoryBuffer::getFile(path);
    if (workDir.empty() || !buffer) {
        llvm::errs() << "[oobsum] warning: no solved decisions for this TU (" << path << ")\n";
        return;
    }
    llvm::Expected<llvm::json::Value> value = llvm::json::parse((*buffer)->getBuffer());
    const llvm::json::Object *o = value ? value->getAsObject() : nullptr;
    const llvm::json::Array *list = o != nullptr ? o->getArray("decisions") : nullptr;
    if (list == nullptr) {
        if (!value) {
            llvm::consumeError(value.takeError());
        }
        llvm::errs() << "[oobsum] warning: cannot read " << path << "\n";
        return;
    }
    for (const llvm::json::Value &v : *list) {
        oobsum::Decision d;
        std::string error;
        if (oobsum::DecisionFromJson(v, d, error)) {
            out.push_back(std::move(d));
        } else {
            llvm::errs() << "[oobsum] warning: " << path << ": " << error << "\n";
        }
    }
}

// WorkDir/fir/<tu-id>.json（06- X-01，SPEC-0028）：紧凑 JSON，先写 .tmp 再改名；返回路径（没写成为空）
std::string OobSummaryChecker::writeFir(const oobsum::fir::Module &module, const std::string &tuId) const
{
    if (workDir.empty()) {
        llvm::errs() << "[oobsum] warning: Phase=export needs WorkDir; nothing written\n";
        return "";
    }
    llvm::SmallString<256> dir(workDir);
    llvm::sys::path::append(dir, "fir");
    std::error_code ec = llvm::sys::fs::create_directories(dir);
    llvm::SmallString<256> path(dir);
    llvm::sys::path::append(path, tuId + ".json");
    std::string error;
    if (ec || !oobsum::fir::WriteModuleFile(path, module, error)) {
        llvm::errs() << "[oobsum] warning: " << (ec ? "cannot create " + dir.str().str() + ": " + ec.message() : error)
                     << "\n";
        return "";
    }
    return path.str().str();
}

void OobSummaryChecker::dumpFir(const oobsum::fir::Module &module, const std::string &tuId) const
{
    // SPEC-0006 §3、SPEC-0007 §2.5：模块头总打，函数按 DumpFilter
    std::vector<std::string> lines = oobsum::fir::ModuleHeaderText(module);
    for (const oobsum::fir::Function &f : module.funcs) {
        if (dump.Wants("fir", f.name)) {
            std::vector<std::string> body = oobsum::fir::FunctionText(module, f);
            lines.insert(lines.end(), body.begin(), body.end());
        }
    }
    std::string text;
    for (const std::string &line : lines) {
        dump.Line("fir", line);
        text += line + "\n";
    }
    dump.File(tuId + "/fir/" + tuId + ".txt", text);

    // 自检：读回来、再写出逐字相同；否则是降级产出了不合法的 FIR（只打印，不影响判定）
    llvm::json::Value json = oobsum::fir::ModuleToJson(module);
    std::string compact;
    llvm::raw_string_ostream compactOs(compact);
    compactOs << json;
    compactOs.flush();
    oobsum::fir::Module back;
    std::string error;
    if (!oobsum::fir::ModuleFromJson(json, back, error)) {
        dump.Line("fir", "error: " + error);
    } else {
        std::string again;
        llvm::raw_string_ostream againOs(again);
        againOs << oobsum::fir::ModuleToJson(back);
        if (againOs.str() != compact) {
            dump.Line("fir", "error: FIR does not round-trip through JSON");
        }
    }
    std::string pretty;
    llvm::raw_string_ostream prettyOs(pretty);
    prettyOs << llvm::formatv("{0:1}", json) << "\n";
    dump.File(tuId + "/fir/" + tuId + ".json", prettyOs.str());
}

void OobSummaryChecker::dumpSummaries(const oobsum::fir::Module &module, const std::string &tuId,
                                      const oobsum::engine::InlineResult &result) const
{
    // SPEC-0015 §2.3：每个函数的局部事实（函数内解释的结果）写成摘要；Dump=state 打每个块入口的状态。
    // 摘要取引擎自底向上算出的那一份（与判定用的相同，SPEC-0024 §2.3），按源码里函数的顺序打
    for (const oobsum::fir::Function &f : module.funcs) {
        bool wantSummary = dump.Wants("summary", f.name);
        auto trace = result.traces.find(f.name);
        if (trace != result.traces.end()) {
            for (const std::string &line : trace->second) {
                dump.Line("state", "fn " + f.name + " " + line);
            }
        }
        auto summary = result.summaries.find(f.key);
        if (!wantSummary || summary == result.summaries.end()) {
            continue;
        }
        llvm::json::Value json = oobsum::summary::SummaryToJson(summary->second);
        std::string compact;
        llvm::raw_string_ostream compactOs(compact);
        compactOs << json;
        dump.Line("summary", compactOs.str());
        std::string pretty;
        llvm::raw_string_ostream prettyOs(pretty);
        prettyOs << llvm::formatv("{0:1}", json) << "\n";
        dump.File(tuId + "/summary/" + f.name + ".json", prettyOs.str());
    }
}

void OobSummaryChecker::checkASTDecl(const TranslationUnitDecl *tu, AnalysisManager &mgr, BugReporter &br) const
{
    const SourceManager &sm = mgr.getSourceManager();
    const FileEntry *mainEntry = sm.getFileEntryForID(sm.getMainFileID());
    std::string mainFile = mainEntry != nullptr ? mainEntry->getName().str() : "<unknown>";
    std::string tuId = oobsum::TuId(mainFile);
    oobsum::KeyMaker keys(sm);

    if (dump.On("options")) {
        dump.Line("options", optionsText);
    }
    if (dump.On("models")) {
        dump.Line("models", "source default" + (modelFile.empty() ? std::string() : "+" + modelFile));
        for (const std::string &line : oobsum::models::DescribeModels(models)) {
            dump.Line("models", line);
        }
    }
    if (dump.On("keys")) {
        dumpKeys(tu, tuId, keys);
    }
    if (dump.On("callgraph")) {
        dumpCallGraph(tu, keys);
    }
    // Phase=inline：单个 TU 内 降级 → 函数内解释 → 摘要 → 推到底 → 判定（SPEC-0021）；降级只做一次，Dump=fir/summary/state 共用
    if (phase == "solve") {
        solveAll();  // 桩 TU：读全部 FIR、跨 TU 求解（X-03 / X-04，SPEC-0029）；本 TU 不报
        return;
    }
    bool engine = phase == "inline";
    bool exporting = phase == "export";
    bool keepFir = exporting || (engine && !workDir.empty());  // X-01 / X-06（SPEC-0028）
    std::vector<oobsum::Decision> decisions;
    if (engine || exporting || dump.On("fir") || dump.On("summary") || dump.On("state")) {
        oobsum::fir::Module module = oobsum::LowerTranslationUnit(tu, mgr, keys, mainFile);
        if (dump.On("fir")) {
            dumpFir(module, tuId);
        }
        if (keepFir) {
            std::string path = writeFir(module, tuId);
            if (exporting) {
                // export：不求解、不报、不写判定日志（X-02）
                if (dump.On("stats")) {
                    dump.Line("stats", "export tu=" + tuId + " functions=" + std::to_string(module.funcs.size()) +
                                           " fir=" + path);
                }
                return;
            }
        }
        bool wantState = dump.On("state");
        oobsum::engine::InlineResult result = oobsum::engine::RunInline(
            module, models, [&](const std::string &fn) { return wantState && dump.Wants("state", fn); });
        if (dump.On("summary") || dump.On("state")) {
            dumpSummaries(module, tuId, result);
        }
        if (engine) {
            for (const std::string &line : result.applyLines) {
                if (dump.Wants("apply", line.substr(0, line.find(' ')))) {
                    dump.Line("apply", line);
                }
            }
            decisions = std::move(result.decisions);
        }
    }

    // Phase=report：判定来自 solve 的结果（SPEC-0030）
    if (phase == "report") {
        loadSolved(tuId, decisions);
    }

    // 另一个判定来源：调试内建函数（SPEC-0001 §6.2）
    DebugIntrinsicVisitor visitor(sm, keys, decisions);
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
                                            "kinds separated by '+' from options, models, callgraph, keys, fir, "
                                            "state, summary, apply, decisions, stats, all (and a valid DumpFilter "
                                            "regex); "
                                            "got '" +
                                                unknownKind + "'");
    }
    // ModelFile（SPEC-0033 §2.1）：读不了、YAML 错 → 选项错误；坏条目跳过并提示
    chk->modelFile = opts.getCheckerStringOption(chk, "ModelFile").str();
    if (!chk->modelFile.empty()) {
        auto buffer = llvm::MemoryBuffer::getFile(chk->modelFile);
        std::vector<std::string> warnings;
        std::string error;
        if (!buffer) {
            mgr.reportInvalidCheckerOptionValue(
                chk, "ModelFile",
                "a readable YAML file ('" + chk->modelFile + "': " + buffer.getError().message() + ")");
        } else if (!oobsum::models::MergeModelYaml((*buffer)->getBuffer(), chk->models, warnings, error)) {
            mgr.reportInvalidCheckerOptionValue(chk, "ModelFile",
                                                "a valid model table ('" + chk->modelFile + "':" + error + ")");
        }
        for (const std::string &w : warnings) {
            llvm::errs() << "[oobsum] warning: ModelFile " << chk->modelFile << ": " << w << "\n";
        }
    }
    chk->optionsText = "Phase=" + chk->phase + " WorkDir=\"" + chk->workDir + "\" Policy=" + policyName +
                       " InlineDepth=" + std::to_string(chk->policy.inlineDepth) +
                       " ReportSafe=" + (chk->policy.reportSafe ? "true" : "false") + " SuspectReasons=\"" +
                       opts.getCheckerStringOption(chk, "SuspectReasons").str() + "\" Dump=\"" + chk->dumpKinds +
                       "\" DumpFilter=\"" + chk->dumpFilter + "\" ModelFile=\"" + chk->modelFile + "\"";
}

bool ento::shouldRegisterOobSummaryChecker(const CheckerManager &mgr)
{
    return true;
}
