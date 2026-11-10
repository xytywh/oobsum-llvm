//===-- FirStore.h - Immutable FIR publication and phase guards -*- C++ -*-===//
//
// SPEC-0133 / X-01、X-03：同Tu不同有效FIR不可静默覆盖；不代表支持多配置并存求解。
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRSTORE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRSTORE_H

#include "OobSummary/core/Fir.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <string>

namespace oobsum {
struct FirInputState {
    bool pending = false;
    bool conflict = false;
    size_t conflictingTus = 0;
    size_t incompleteFiles = 0;
    std::string error;
    bool ReadyForSolve() const
    {
        return !conflict && incompleteFiles == 0 && error.empty();
    }
    bool ReadyForReport() const
    {
        return ReadyForSolve() && !pending;
    }
};

// 写inputs-pending目录，完整候选以exclusive方式发布；冲突先建fir-conflicts/Tu目录，再存两版本。
// 正常TuId/文件名不变；失败保留完整候选/冲突证据，error说明原因。
bool PublishFir(llvm::StringRef workDir, const fir::Module &module, std::string &path, std::string &error);
// solve扫描*.tmp和冲突子目录；report只做O(1)的pending/冲突根目录检查。
FirInputState InspectFirInputs(llvm::StringRef workDir, bool scanTemporary);
// 仅在全部solved成功后移除inputs-pending；不清理任何冲突或输入文件。
bool MarkFirSolved(llvm::StringRef workDir, std::string &error);
}  // namespace oobsum
#endif
