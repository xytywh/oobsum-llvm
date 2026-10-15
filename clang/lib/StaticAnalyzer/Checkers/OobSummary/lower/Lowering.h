//===-- Lowering.h - Clang AST/CFG to FIR -----------------------*- C++ -*-===//
//
// 一个 TU 降成 fir::Module（06- §6.1，A-01…A-09；只有 lower/ 碰 Clang）。
//   S4b（SPEC-0007）：模块骨架——目标、文件表、类型与记录、全局、函数签名 / 局部（obj）/ CFG 块与边（先不带 assume）。
//   S4c：指令；S5：条件边与其余构造。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_LOWERING_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_LOWERING_H

#include "clang/AST/Decl.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"

#include "OobSummary/core/Fir.h"
#include "OobSummary/lower/Keys.h"

namespace oobsum {

// mainFile：主文件路径（按 FileEntry 名）；keys：本 TU 的 KeyMaker
fir::Module LowerTranslationUnit(const clang::TranslationUnitDecl *tu, clang::ento::AnalysisManager &mgr,
                                 KeyMaker &keys, llvm::StringRef mainFile);

}  // namespace oobsum

#endif
