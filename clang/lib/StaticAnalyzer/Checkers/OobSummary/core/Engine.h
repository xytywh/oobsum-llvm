//===-- Engine.h - One-TU pipeline from FIR to decisions --------*- C++ -*-===//
//
// Phase=inline 的引擎（SPEC-0021；06- §7）：单个 TU 内 函数内解释 → 摘要 → 推到底（Propagate）→ 带标签的判定。
// 跨 TU 的被调者视为不在本 TU（not_in_tu）。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ENGINE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ENGINE_H

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/Fir.h"
#include "OobSummary/core/Interp.h"
#include "OobSummary/core/Models.h"
#include "OobSummary/core/Solver.h"
#include "OobSummary/core/Summary.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace oobsum {
namespace engine {

struct InlineResult {
    std::map<std::string, summary::Summary> summaries;  // 函数名 → 摘要（只有局部事实）
    std::map<std::string, interp::FunctionFacts> facts;
    solver::PropagateResult propagate;
    std::vector<Decision> decisions;
    std::vector<std::string> applyLines;  // Dump=apply：<函数> @ <调用点> <被调者>.<访问位置> → <结局>[ (<原因>)]
    std::map<std::string, std::vector<std::string>> traces;  // Dump=state：traceFilter 选中的函数
    std::vector<std::string> order;                          // 解释的顺序（被调者先，SPEC-0024 §2.3）
};

// traceFilter：哪些函数记 Dump=state 的轨迹（空 = 都不记）
InlineResult RunInline(const fir::Module &m, const models::ModelTable &models = models::ModelTable::Default(),
                       const std::function<bool(const std::string &)> &traceFilter = nullptr);

// 文本（SPEC-0021 §2.3）
std::string SuspectMessage(const std::string &kind, int64_t depth, const std::string &object, const std::string &have,
                           const std::string &endHi);
std::string SafeMessage(const std::string &object, const std::string &have, const std::string &endHi);
std::string LedgerMessage(const std::string &reason, const std::string &callee = "");

}  // namespace engine
}  // namespace oobsum

#endif
