//===-- Reporter.h - Turn decisions into CSA warnings -----------*- C++ -*-===//
//
// 判定 → 告警（06- F-30…F-35，SPEC-0001 §3）：
//   * 每个标签一个 BugType（名字里带 definite / possible / suspect / proven safe），category 只用于展示；
//   * 告警文本以 [OOB-<标签>[:<原因>]] 开头，后面是 F-32 正文；
//   * 位置 = 汇合点调用；链上每一跳在本 TU 可见文件里的各给一条 note（F-33）；
//   * 同一 TU 内按（文件、行、列、指纹）排序后发出（F-35）。
// 不论报不报，每条判定都交给 DecisionLog。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_REPORT_REPORTER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_REPORT_REPORTER_H

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/Policy.h"

#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"

#include <string>
#include <utility>
#include <vector>

namespace oobsum {

struct BugTypes {
    const clang::ento::BugType *definite = nullptr;
    const clang::ento::BugType *possible = nullptr;
    const clang::ento::BugType *suspect = nullptr;
    const clang::ento::BugType *safe = nullptr;
};

class Reporter {
public:
    Reporter(const BugTypes &bugTypes, const PolicyOptions &policy) : bugTypes(bugTypes), policy(policy)
    {
    }
    void Add(Decision decision);
    // 排序、按策略发出；返回（判定, 去向），供 DecisionLog 与 Dump=decisions 使用
    std::vector<std::pair<Decision, Disposition>> Flush(clang::ento::BugReporter &br, const clang::SourceManager &sm);
    static std::string FullMessage(const Decision &decision);

private:
    const clang::ento::BugType *TypeOf(Label label) const;
    clang::SourceLocation Locate(const Site &site, const clang::SourceManager &sm) const;

    BugTypes bugTypes;
    PolicyOptions policy;
    std::vector<Decision> pending;
};

}  // namespace oobsum

#endif
