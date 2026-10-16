//===-- Policy.cpp - Which decisions become warnings ------------*- C++ -*-===//
//
// 规则见 Policy.h 与 oobsum/specs/SPEC-0001 §3。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Policy.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

#include <algorithm>

namespace oobsum {

std::set<std::string> SplitList(llvm::StringRef text)
{
    std::set<std::string> out;
    llvm::SmallVector<llvm::StringRef, 8> parts;
    text.split(parts, '+', -1, false);
    for (llvm::StringRef chunk : parts) {
        llvm::SmallVector<llvm::StringRef, 4> sub;
        chunk.split(sub, ',', -1, false);
        for (llvm::StringRef part : sub) {
            part = part.trim();
            if (!part.empty()) {
                out.insert(part.str());
            }
        }
    }
    return out;
}

Disposition Classify(const Decision &decision, const PolicyOptions &options)
{
    Disposition result;
    if (decision.label == Label::Safe) {
        result.emit = options.reportSafe;
        result.why = result.emit ? "" : "safe";
        return result;
    }
    if (decision.label == Label::Suspect) {
        if (!options.recall) {
            result.why = "policy_balanced";
            return result;
        }
        if (!options.suspectReasons.empty() && options.suspectReasons.count(decision.reason) == 0) {
            result.why = "reason_filtered";
            return result;
        }
    }
    // 层数过滤只用于已判定的 DEFINITE / POSSIBLE（与现有规则分工，F-20）；SUSPECT
    // 不过滤：它们本来就是现有规则也没判出来的
    unsigned depth = std::max(decision.depth, decision.effDepth);
    if (decision.label != Label::Suspect && options.inlineDepth > 0 && depth <= options.inlineDepth) {
        result.why = "inline_depth";
        return result;
    }
    result.emit = true;
    return result;
}

std::vector<Disposition> ClassifyBatch(const std::vector<Decision> &decisions, const PolicyOptions &options)
{
    std::vector<Disposition> out;
    std::set<std::string> emitted;
    for (const Decision &d : decisions) {
        out.push_back(Classify(d, options));
        if (out.back().emit) {
            emitted.insert(d.fp);
        }
    }
    // 同一入口同一次访问的重复账本：它指向的那条报出了才不报（那条被过滤掉时这条照常，SPEC-0036）
    for (size_t i = 0; i < decisions.size(); ++i) {
        if (out[i].emit && !decisions[i].dupOf.empty() && emitted.count(decisions[i].dupOf) != 0) {
            out[i].emit = false;
            out[i].why = "duplicate_access";
        }
    }
    return out;
}

}  // namespace oobsum
