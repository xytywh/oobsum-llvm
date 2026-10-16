//===-- Policy.h - Which decisions become warnings --------------*- C++ -*-===//
//
// 召回优先（SPEC-0001）：拿不准就报 SUSPECT，交给后续大模型复核过滤；只有被证明安全的
// 不报。每条判定的去向（报 / 只记日志）与理由都写进判定日志，"为什么这里没报"可查。
// 不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_POLICY_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_POLICY_H

#include "OobSummary/core/Decision.h"

#include "llvm/ADT/StringRef.h"

#include <set>
#include <string>
#include <vector>

namespace oobsum {

struct PolicyOptions {
    bool recall = true;        // Policy=recall（默认）/ balanced
    unsigned inlineDepth = 2;  // F-20：max(depth, effDepth) <= inlineDepth 的交给现有规则；0 = 全报
    bool reportSafe = false;   // SAFE 也报（用例、审计）
    std::set<std::string> suspectReasons;  // 空 = 所有原因都报 SUSPECT
};

struct Disposition {
    bool emit = false;
    std::string why;  // emit=false 时：safe / policy_balanced / reason_filtered / inline_depth
};

Disposition Classify(const Decision &decision, const PolicyOptions &options);
// 一批判定：逐条 Classify，再把 dupOf 指向的判定已报出的那些改成不报（duplicate_access，SPEC-0036）
std::vector<Disposition> ClassifyBatch(const std::vector<Decision> &decisions, const PolicyOptions &options);

// "a+b + c" → {"a","b","c"}。分隔符用 '+'：-analyzer-config 会把逗号当成多个键值对的分隔（实测：
// Dump=options,stats 报 "analyzer-config option 'stats' has a key but no value"）。逗号也认，供将来从配置文件读入。
std::set<std::string> SplitList(llvm::StringRef text);

}  // namespace oobsum

#endif
