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
#include <vector>

namespace oobsum {
struct FirInputState {
    bool pending = false;
    bool conflict = false;
    bool variants = false;
    size_t conflictingTus = 0;
    size_t incompleteFiles = 0;
    size_t variantTus = 0;
    size_t variantFiles = 0;
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
// SPEC-0139：三阶段export可留存多个版本，并用固定最小内容哈希维护原入口；默认inline仍拒绝冲突。
bool PublishFir(llvm::StringRef workDir, const fir::Module &module, std::string &path, std::string &error,
                bool selectVariant = false);
std::string FirContentId(const fir::Module &module);
struct FirSelection {
    std::string selected;
    std::vector<std::string> ignored;
};
// 只读取代表模块，其它版本只盘点文件名；检查代表确为已存ID中的固定最小者。
bool DescribeFirSelection(llvm::StringRef workDir, const fir::Module &module, FirSelection &selection,
                          std::string &error);
bool IsStoredFirVariant(llvm::StringRef workDir, llvm::StringRef tu, llvm::StringRef id, std::string &error,
                        bool requireMinimum = false);
// SPEC-0137：仅所有export结束后的solve扫描*.tmp和冲突子目录；并发发布观察与report只检查根标记。
// scanTemporary=false时两个数量不代表实际0，调用方须显示为未扫描，不能由此宣称输入完整。
FirInputState InspectFirInputs(llvm::StringRef workDir, bool scanTemporary);
// 仅在全部solved成功后移除inputs-pending；不清理任何冲突或输入文件。
bool MarkFirSolved(llvm::StringRef workDir, std::string &error);
}  // namespace oobsum
#endif
