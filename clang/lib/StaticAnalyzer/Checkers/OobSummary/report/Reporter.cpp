//===-- Reporter.cpp - Turn decisions into CSA warnings ---------*- C++ -*-===//
//
// 见 Reporter.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/report/Reporter.h"

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/FileManager.h"

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
    return decision.msg.empty() ? tag : tag + " " + decision.msg;
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

std::vector<std::pair<Decision, Disposition>> Reporter::Flush(BugReporter &br, const SourceManager &sm)
{
    // F-35：按（文件、行、列、指纹）排序，输出稳定
    std::stable_sort(pending.begin(), pending.end(), [](const Decision &a, const Decision &b) {
        return std::tie(a.site.file, a.site.line, a.site.col, a.fp) <
               std::tie(b.site.file, b.site.line, b.site.col, b.fp);
    });
    std::vector<std::pair<Decision, Disposition>> out;
    for (Decision &decision : pending) {
        Disposition disposition = Classify(decision, policy);
        if (disposition.emit) {
            SourceLocation loc = Locate(decision.site, sm);
            if (loc.isInvalid()) {
                // 位置换不出来：不丢告警，退回主文件开头，原位置写进文本（06- A-91）
                loc = sm.getLocForStartOfFile(sm.getMainFileID());
                decision.msg += " (at " + decision.site.file + ":" + std::to_string(decision.site.line) + ")";
            }
            auto report = std::make_unique<BasicBugReport>(*TypeOf(decision.label), FullMessage(decision),
                                                           PathDiagnosticLocation(loc, sm));
            for (const Hop &hop : decision.chain) {
                SourceLocation hopLoc = Locate(hop.site, sm);
                if (hopLoc.isValid()) {
                    report->addNote("call chain: " + hop.fn, PathDiagnosticLocation(hopLoc, sm));
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
