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

#include <tuple>

namespace oobsum {

static llvm::json::Value SiteToJson(const Site &site)
{
    return (site.file + ":" + std::to_string(site.line) + ":" + std::to_string(site.col));
}

// 文件:行:列 → Site（文件名里可以有冒号，从右往左拆）
static bool SiteFromText(llvm::StringRef text, Site &out)
{
    llvm::StringRef rest = text;
    llvm::StringRef col;
    llvm::StringRef line;
    std::tie(rest, col) = rest.rsplit(':');
    std::tie(rest, line) = rest.rsplit(':');
    unsigned l = 0;
    unsigned c = 0;
    if (rest.empty() || line.getAsInteger(10, l) || col.getAsInteger(10, c)) {
        return false;
    }
    out.file = rest.str();
    out.line = l;
    out.col = c;
    return true;
}

bool DecisionFromJson(const llvm::json::Value &value, Decision &out, std::string &error)
{
    const llvm::json::Object *o = value.getAsObject();
    if (o == nullptr) {
        error = "decision: expected an object";
        return false;
    }
    auto str = [&](llvm::StringRef key, std::string &dst) {
        llvm::Optional<llvm::StringRef> v = o->getString(key);
        if (!v) {
            error = "decision: missing " + key.str();
            return false;
        }
        dst = v->str();
        return true;
    };
    std::string label;
    std::string site;
    if (!str("label", label) || !str("reason", out.reason) || !str("site", site) || !str("fn", out.fn) ||
        !str("kind", out.kind) || !str("obj", out.obj) || !str("have", out.have) || !str("bound", out.bound) ||
        !str("msg", out.msg) || !str("fp", out.fp) || !str("origin", out.origin)) {
        return false;
    }
    if (llvm::Optional<llvm::StringRef> dup = o->getString("dup_of")) {
        out.dupOf = dup->str();  // SPEC-0036
    }
    llvm::Optional<Label> parsed = ParseLabel(label);
    if (!parsed || !SiteFromText(site, out.site)) {
        error = "decision: bad label or site";
        return false;
    }
    out.label = *parsed;
    const llvm::json::Array *end = o->getArray("end");
    if (end == nullptr || end->size() != 2 || !(*end)[0].getAsString() || !(*end)[1].getAsString()) {
        error = "decision: bad end";
        return false;
    }
    out.endLo = (*end)[0].getAsString()->str();
    out.endHi = (*end)[1].getAsString()->str();
    out.depth = static_cast<unsigned>(o->getInteger("depth").getValueOr(0));
    out.effDepth = static_cast<unsigned>(o->getInteger("eff_depth").getValueOr(0));
    out.chain.clear();
    if (const llvm::json::Array *chain = o->getArray("chain")) {
        for (const llvm::json::Value &h : *chain) {
            llvm::Optional<llvm::StringRef> text = h.getAsString();
            if (!text) {
                error = "decision: bad chain";
                return false;
            }
            Hop hop;
            llvm::StringRef where;
            std::tie(hop.fn, where) = std::make_pair(text->split('@').first.str(), text->split('@').second);
            llvm::StringRef file;
            llvm::StringRef line;
            std::tie(file, line) = where.rsplit(':');
            hop.site.file = file.str();
            line.getAsInteger(10, hop.site.line);
            out.chain.push_back(hop);
        }
    }
    return true;
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
    if (!decision.dupOf.empty()) {
        out["dup_of"] = decision.dupOf;
    }
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
