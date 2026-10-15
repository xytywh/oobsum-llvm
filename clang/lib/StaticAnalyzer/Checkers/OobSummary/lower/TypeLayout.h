//===-- TypeLayout.h - Clang types to FIR types -----------------*- C++ -*-===//
//
// Clang QualType → fir::TypeId，记录布局进 Module::records（SPEC-0007 §2.1、§2.2；06- D-10、§5.4）。
// 大小与偏移一律取被分析 TU 的 ASTContext（D-39）：-triple armebv7-linux-gnueabi 下指针 / long 是 4 字节。
// 记录名：① typedef 反查的干净名 → ② tag 名 → ③ "<anonymous struct/union>"（csa-checker-dev 约定）。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_TYPELAYOUT_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_TYPELAYOUT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"

#include "llvm/ADT/DenseMap.h"

#include "OobSummary/core/Fir.h"

#include <string>

namespace oobsum {

class TypeLayout {
public:
    TypeLayout(const clang::ASTContext &ctx, fir::Module &module);

    fir::TypeId Map(clang::QualType type);
    std::string RecordName(const clang::RecordDecl *record) const;

private:
    uint32_t RecordId(const clang::RecordDecl *definition);

    const clang::ASTContext &ctx;
    fir::Module &module;
    llvm::DenseMap<const clang::RecordDecl *, uint32_t> records;          // 规范声明 → 记录号
    llvm::DenseMap<const clang::RecordDecl *, std::string> typedefNames;  // 规范声明 → 第一个 typedef 名
};

}  // namespace oobsum

#endif
