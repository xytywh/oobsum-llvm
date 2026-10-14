//===-- DecisionLog.cpp - JSONL log of every decision -----------*- C++ -*-===//
//
// 见 DecisionLog.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/DecisionLog.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

namespace oobsum {

static llvm::json::Value SiteToJson(const Site &site)
{
    return (site.file + ":" + std::to_string(site.line) + ":" + std::to_string(site.col));
}

llvm::json::Object DecisionToJson(const Decision &decision, const Disposition &disposition)
{
    llvm::json::Array chain;
    for (const Hop &hop : decision.chain) {
        chain.push_back(hop.fn + "@" + hop.site.file + ":" + std::to_string(hop.site.line));
    }
    llvm::json::Object out{
        {"schema", "oobsum-decision/1"},
        {"label", LabelName(decision.label)},
        {"reason", decision.reason},
        {"emitted", disposition.emit},
        {"site", SiteToJson(decision.site)},
        {"fn", decision.fn},
        {"kind", decision.kind},
        {"obj", decision.obj},
        {"have", decision.have},
        {"end", llvm::json::Array{decision.endLo, decision.endHi}},
        {"bound", decision.bound},
        {"depth", static_cast<int64_t>(decision.depth)},
        {"eff_depth", static_cast<int64_t>(decision.effDepth)},
        {"chain", std::move(chain)},
        {"msg", decision.msg},
        {"fp", decision.fp},
        {"origin", decision.origin},
    };
    if (!disposition.emit) {
        out["not_emitted_because"] = disposition.why;
    }
    return out;
}

std::string WriteDecisionLog(const std::string &workDir, const std::string &tuId, const std::string &mainFile,
                             const std::vector<std::pair<Decision, Disposition>> &entries, std::string &error)
{
    if (workDir.empty()) {
        return "";
    }
    llvm::SmallString<256> dir(workDir);
    llvm::sys::path::append(dir, "decisions");
    if (std::error_code ec = llvm::sys::fs::create_directories(dir)) {
        error = "cannot create " + std::string(dir) + ": " + ec.message();
        return "";
    }
    llvm::SmallString<256> path(dir);
    llvm::sys::path::append(path, tuId + ".jsonl");
    std::error_code ec;
    llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        error = "cannot write " + std::string(path) + ": " + ec.message();
        return "";
    }
    // 第一行是 TU 头，后面每条判定一行
    os << llvm::json::Value(llvm::json::Object{{"schema", "oobsum-decisions/1"},
                                               {"tu", tuId},
                                               {"file", mainFile},
                                               {"count", static_cast<int64_t>(entries.size())}})
       << "\n";
    for (const auto &entry : entries) {
        os << llvm::json::Value(DecisionToJson(entry.first, entry.second)) << "\n";
    }
    return std::string(path);
}

}  // namespace oobsum
