//===-- Diagnostics.h - Human-readable access diagnostics ------*- C++ -*-===//
//
// SPEC-0126 / F-32：仅格式化已有判断，不重新证明安全。
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DIAGNOSTICS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DIAGNOSTICS_H

#include "OobSummary/core/Decision.h"

#include <string>

namespace oobsum {
std::string DiagnosticMessage(const Decision &decision);
std::string DiagnosticGroup(const Decision &decision);
}  // namespace oobsum
#endif
