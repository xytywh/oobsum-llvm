//===-- Reporter.cpp - Turn decisions into CSA warnings ---------*- C++ -*-===//
//
// 见 Reporter.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/report/Reporter.h"

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"

#include "llvm/Support/Path.h"

#include <algorithm>
#include <tuple>

using namespace clang;
using namespace clang::ento;

namespace oobsum {

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
    return decision.warnMsg.empty() ? tag : tag + " " + decision.warnMsg;
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
SourceLocation Reporter::Locate(const Site &site, const SourceManager &sm) const
{
    if (site.file.empty() || site.line == 0) {
        return SourceLocation();
    }
    auto file = sm.getFileManager().getFile(site.file);
    if (!file) {
        return SourceLocation();
    }
    return sm.translateFileLineCol(*file, site.line, site.col == 0 ? 1 : site.col);
}

// 只有文件名的位置（heap@<文件名>:<行>）：在本 TU 已打开的文件里按文件名找（SPEC-0078）
SourceLocation Reporter::LocateBase(llvm::StringRef site, const SourceManager &sm) const
{
    std::pair<llvm::StringRef, llvm::StringRef> parts = site.rsplit(':');
    unsigned line = 0;
    if (parts.second.getAsInteger(10, line) || line == 0) {
        return SourceLocation();
    }
    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
        const FileEntry *entry = it->getFirst();
        if (entry != nullptr && llvm::sys::path::filename(entry->getName()) == parts.first) {
            return sm.translateFileLineCol(entry, line, 1);
        }
    }
    return SourceLocation();
}

std::vector<std::pair<Decision, Disposition>> Reporter::Flush(BugReporter &br, const SourceManager &sm)
{
    // F-35：按（文件、行、列、指纹）排序，输出稳定
    std::stable_sort(pending.begin(), pending.end(), [](const Decision &a, const Decision &b) {
        return std::tie(a.site.file, a.site.line, a.site.col, a.fp) <
               std::tie(b.site.file, b.site.line, b.site.col, b.fp);
    });
    std::vector<std::pair<Decision, Disposition>> out;
    std::vector<Disposition> dispositions = ClassifyBatch(pending, policy);
    for (size_t i = 0; i < pending.size(); ++i) {
        Decision &decision = pending[i];
        const Disposition &disposition = dispositions[i];
        if (disposition.emit) {
            SourceLocation loc = Locate(decision.site, sm);
            if (loc.isInvalid()) {
                // 位置换不出来：不丢告警，退回主文件开头，原位置写进文本（06- A-91）
                loc = sm.getLocForStartOfFile(sm.getMainFileID());
                decision.warnMsg += " (at " + decision.site.file + ":" + std::to_string(decision.site.line) + ")";
            }
            auto report = std::make_unique<BasicBugReport>(*TypeOf(decision.label), FullMessage(decision),
                                                           PathDiagnosticLocation(loc, sm));
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
                    SourceLocation hopLoc = Locate(chain[i].site, sm);
                    if (hopLoc.isInvalid()) {
                        continue;
                    }
                    std::string what = i + 1 < chain.size() ? chain[i].fn + " calls " + chain[i + 1].fn + " here"
                                                            : chain[i].fn + " accesses the object here";
                    report->addNote(what, PathDiagnosticLocation(hopLoc, sm));
                }
                llvm::StringRef obj = decision.obj;
                if (obj.consume_front("heap@")) {
                    SourceLocation allocLoc = LocateBase(obj, sm);
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
