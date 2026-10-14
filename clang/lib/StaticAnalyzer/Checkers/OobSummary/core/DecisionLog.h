//===-- DecisionLog.h - JSONL log of every decision -------------*- C++ -*-===//
//
// 每个 TU 一份 <WorkDir>/decisions/<tu-id>.jsonl：每条判定一行，报了的、没报的（SAFE、
// 被策略或层数过滤的）都在，带标签、原因、去向。它是：
//   ① 调试时"为什么这里报 / 没报"的第一手材料（oobsum/tools/oobsum.py why 读它）；
//   ② 后续大模型复核的输入（SPEC-0001 §4）。
// 格式见 oobsum/specs/SPEC-0001 §5。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DECISIONLOG_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DECISIONLOG_H

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/Policy.h"

#include "llvm/Support/JSON.h"

#include <string>
#include <vector>

namespace oobsum {

llvm::json::Object DecisionToJson(const Decision &decision, const Disposition &disposition);

// 写整个 TU 的判定日志；workDir 为空则不写。返回写出的路径（失败返回空串，错误信息放 error）
std::string WriteDecisionLog(const std::string &workDir, const std::string &tuId, const std::string &mainFile,
                             const std::vector<std::pair<Decision, Disposition>> &entries, std::string &error);

}  // namespace oobsum

#endif
