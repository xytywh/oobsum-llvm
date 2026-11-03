//===-- AccessInfo.h - AST access descriptions ------------------*- C++ -*-===//
//
// SPEC-0125：只产出 FIR 展示信息，不参与抽象解释或安全证明。
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_ACCESSINFO_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_ACCESSINFO_H

#include "OobSummary/core/Fir.h"

#include <memory>
#include <vector>

namespace clang {
class ASTContext;
class FunctionDecl;
class Stmt;
}  // namespace clang

namespace oobsum {

class TypeLayout;

class AccessDescriber {
public:
    AccessDescriber(const clang::ASTContext &context, const clang::FunctionDecl *function,
                    const TypeLayout &layout);
    ~AccessDescriber();
    std::vector<AccessInfo> Describe(const clang::Stmt *at, const fir::Inst &inst, const fir::Function &function) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace oobsum
#endif
