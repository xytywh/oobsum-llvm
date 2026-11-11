//===-- Reporter.cpp - Turn decisions into CSA warnings ---------*- C++ -*-===//
//
// 见 Reporter.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/report/Reporter.h"
#include "OobSummary/core/Diagnostics.h"
#include "OobSummary/core/Key.h"
#include "OobSummary/core/SourceMap.h"

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"

#include "llvm/Support/Path.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

using namespace clang;
using namespace clang::ento;

namespace oobsum {

Reporter::Reporter(const BugTypes &bugTypes, const PolicyOptions &policy, const std::vector<std::string> *files)
    : bugTypes(bugTypes), policy(policy), fixedSourceFiles(files != nullptr)
{
    if (files != nullptr) {
        for (const std::string &file : *files) {
            sourceFiles.insert(NormalizePath(file));
        }
    }
}

void Reporter::Add(Decision decision)
{
    pending.push_back(std::move(decision));
}

std::string Reporter::FullMessage(const Decision &decision)
{
    std::string tag = "[" + LabelTag(decision.label).str();
    if ((decision.label == Label::Suspect || decision.label == Label::Safe) && !decision.reason.empty()) {
        tag += ":" + decision.reason;
    }
    tag += "]";
    std::string scene = " [scene=";
    if (decision.scenes.empty()) {
        scene += "UNCLASSIFIED";
    } else {
        for (size_t i = 0; i < decision.scenes.size(); ++i) {
            scene += (i == 0 ? "" : "+") + std::string("LV") + std::to_string(decision.scenes[i]);
        }
    }
    scene += "]";
    return (decision.warnMsg.empty() ? tag : tag + " " + decision.warnMsg) + scene;
}

const BugType *Reporter::TypeOf(Label label) const
{
    switch (label) {
        case Label::Definite:
            return bugTypes.definite;
        case Label::Possible:
            return bugTypes.possible;
        case Label::Suspect:
            return bugTypes.suspect;
        case Label::Safe:
            return bugTypes.safe;
    }
    return bugTypes.suspect;
}

// 文件:行:列 → SourceLocation（06- A-91）；文件不在本 TU 时返回无效位置，由调用方退回
SourceLocation Reporter::Locate(const Site &site, SourceManager &sm, bool load) const
{
    if (site.file.empty() || site.line == 0 || llvm::StringRef(site.file).startswith(kUnresolvedSourcePrefix)) {
        return SourceLocation();
    }
    auto file = sm.getFileManager().getFile(site.file);
    if (!file) {
        return SourceLocation();
    }
    if (load) {
        sm.getOrCreateFileID(*file, SrcMgr::C_User);  // SPEC-0126：跨 TU 的 sink 可能尚未载入
    }
    return sm.translateFileLineCol(*file, site.line, site.col == 0 ? 1 : site.col);
}

// SPEC-0078；由SPEC-0132 / F-33修订：不能从FileEntry指针哈希遍历中随意选一个同名文件。
SourceLocation Reporter::LocateBase(llvm::StringRef site, SourceManager &sm, size_t &candidates) const
{
    candidates = 0;
    std::pair<llvm::StringRef, llvm::StringRef> parts = site.rsplit(':');
    unsigned line = 0;
    if (parts.second.getAsInteger(10, line) || line == 0) {
        return SourceLocation();
    }
    if (llvm::sys::path::has_parent_path(parts.first)) {
        if (fixedSourceFiles && sourceFiles.count(NormalizePath(parts.first)) == 0) {
            return SourceLocation();
        }
        auto file = sm.getFileManager().getFile(parts.first);
        if (!file) {
            return SourceLocation();
        }
        candidates = 1;
        sm.getOrCreateFileID(*file, SrcMgr::C_User);
        return sm.translateFileLineCol(*file, line, 1);
    }
    // SPEC-0139 / F-33：相同FIR可能来自不同无语义include，不能取当前SM的可见文件集合。
    if (fixedSourceFiles) {
        std::string selected;
        for (const std::string &file : sourceFiles) {
            if (llvm::sys::path::filename(file) == parts.first) {
                selected = file;
                ++candidates;
            }
        }
        return candidates == 1 ? Locate({selected, line, 1}, sm, true) : SourceLocation();
    }
    std::map<std::string, SourceLocation> matches;
    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
        const FileEntry *entry = it->getFirst();
        if (entry != nullptr && llvm::sys::path::filename(entry->getName()) == parts.first) {
            SourceLocation loc = sm.translateFileLineCol(entry, line, 1);
            auto result = matches.emplace(NormalizePath(entry->getName()), loc);
            if (!result.second && loc.isValid() &&
                (result.first->second.isInvalid() || loc.getRawEncoding() < result.first->second.getRawEncoding())) {
                result.first->second = loc;  // 路径别名去重，代表位置亦不依赖指针顺序。
            }
        }
    }
    candidates = matches.size();
    return matches.size() == 1 ? matches.begin()->second : SourceLocation();
}

std::vector<std::pair<Decision, Disposition>> Reporter::Flush(BugReporter &br, SourceManager &sm,
                                                              const AllocationNoteTrace &allocationTrace)
{
    auto reportSite = [&](const Decision &decision) -> const Site & {
        return policy.sinkDiagnostics && !decision.sink.file.empty() ? decision.sink : decision.site;
    };
    auto rank = [](Label label) {
        return label == Label::Definite ? 0 : label == Label::Possible ? 1 : label == Label::Suspect ? 2 : 3;
    };
    for (auto &decision : pending) {
        if (policy.sinkDiagnostics && decision.origin == "engine") {
            if (decision.sink.file.empty()) {
                decision.sink = decision.chain.empty() ? decision.site : decision.chain.back().site;
            }
            decision.warnMsg = DiagnosticMessage(decision);
        }
    }
    // SPEC-0126 / F-35：物理位置与主标签排序，原判定上下文留在 site。
    std::stable_sort(pending.begin(), pending.end(), [&](const Decision &a, const Decision &b) {
        const Site &x = reportSite(a);
        const Site &y = reportSite(b);
        return std::make_tuple(x.file, x.line, rank(a.label), x.col, a.fp) <
               std::make_tuple(y.file, y.line, rank(b.label), y.col, b.fp);
    });
    std::vector<std::pair<Decision, Disposition>> out;
    std::vector<Disposition> dispositions = ClassifyBatch(pending, policy);
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (dispositions[i].emit) {
            groups[policy.sinkDiagnostics ? DiagnosticGroup(pending[i]) : std::to_string(i)].push_back(i);
        }
    }
    std::vector<std::string> messages(pending.size());
    for (const auto &group : groups) {
        size_t first = group.second.front();
        std::set<std::string> seen;
        for (size_t index : group.second) {
            auto &decision = pending[index];
            if (policy.sinkDiagnostics) {
                decision.reportGroup = group.first;
                decision.diagnosticPrimary = index == first;
            }
            std::string text = FullMessage(decision);
            bool unresolved = llvm::StringRef(decision.site.file).startswith(kUnresolvedSourcePrefix);
            for (const auto &hop : decision.chain) {
                unresolved |= llvm::StringRef(hop.site.file).startswith(kUnresolvedSourcePrefix);
            }
            if (unresolved) {
                text += "; some context or call-chain source locations are unresolved";
            }
            if (seen.insert(text).second) {
                messages[first] += (messages[first].empty() ? "" : " | Also: ") + text;
            }
        }
        if (policy.sinkDiagnostics) {
            messages[first] += "; group=" + group.first + "; judgments=" + std::to_string(group.second.size());
        }
    }
    for (size_t i = 0; i < pending.size(); ++i) {
        Decision &decision = pending[i];
        const Disposition &disposition = dispositions[i];
        if (disposition.emit && !messages[i].empty()) {
            const Site &where = reportSite(decision);
            SourceLocation loc = Locate(where, sm, policy.sinkDiagnostics);
            if (loc.isInvalid()) {
                // 位置换不出来：不丢告警，退回主文件开头，原位置写进文本（06- A-91）
                loc = sm.getLocForStartOfFile(sm.getMainFileID());
                messages[i] += " (source unavailable at " + where.file + ":" + std::to_string(where.line) + ")";
            }
            auto report =
                std::make_unique<BasicBugReport>(*TypeOf(decision.label), messages[i], PathDiagnosticLocation(loc, sm));
            // F-33（SPEC-0078）：① 总链 note；② 本 TU 可见的每一跳、访问点、分配点；一跳的不加（与告警位置重复）
            const std::vector<Hop> &chain = decision.chain;
            if (chain.size() >= 2) {
                std::string text = "Call chain: ";
                for (size_t i = 0; i < chain.size(); ++i) {
                    text += (i == 0 ? "" : " -> ") + chain[i].fn + " (" +
                            llvm::sys::path::filename(chain[i].site.file).str() + ":" +
                            std::to_string(chain[i].site.line) + ")";
                }
                report->addNote(text, PathDiagnosticLocation(loc, sm));
                for (size_t i = 0; i < chain.size(); ++i) {
                    Site hopSite = chain[i].site;
                    // SPEC-0139 / F-33：note可见性由所选FIR固定，不能受当前AST额外include影响。
                    if (fixedSourceFiles && sourceFiles.count(NormalizePath(hopSite.file)) == 0) {
                        continue;
                    }
                    if (!policy.sinkDiagnostics) {
                        hopSite.col = 0;
                    }
                    SourceLocation hopLoc = Locate(hopSite, sm, fixedSourceFiles);
                    if (hopLoc.isInvalid()) {
                        continue;
                    }
                    std::string what = i + 1 < chain.size() ? chain[i].fn + " calls " + chain[i + 1].fn + " here"
                                                            : chain[i].fn + " accesses the object here";
                    report->addNote(what, PathDiagnosticLocation(hopLoc, sm));
                }
                llvm::StringRef obj = decision.obj;
                if (obj.consume_front("heap@")) {
                    size_t candidates = 0;
                    SourceLocation allocLoc = LocateBase(obj, sm, candidates);
                    if (allocationTrace) {
                        allocationTrace(decision,
                                        allocLoc.isValid() ? "unique"
                                        : candidates > 1   ? "ambiguous"
                                                           : "missing",
                                        candidates, allocLoc);
                    }
                    if (allocLoc.isValid()) {
                        report->addNote("the object is allocated here", PathDiagnosticLocation(allocLoc, sm));
                    }
                }
            }
            br.emitReport(std::move(report));
        }
        out.emplace_back(decision, disposition);
    }
    pending.clear();
    return out;
}

}  // namespace oobsum
