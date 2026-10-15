//===-- FirText.h - Text notation of FIR ------------------------*- C++ -*-===//
//
// FIR 的文本记法（SPEC-0006 §2）：一行一条，给 Dump=fir 与 T-02 的快照用例用。纯函数，不依赖 Clang。
//
//   fn F:Ue_InitOne ext -> void @ue_init.c:40:6
//     local l0 pstTbl param0: rec#0*
//     b2 entry:
//       store4 [padd(l0, l1 * 16)] = 0  @42:5
//       -> b0
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRTEXT_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRTEXT_H

#include "OobSummary/core/Fir.h"

#include <string>
#include <vector>

namespace oobsum {
namespace fir {

// module / record / global 行
std::vector<std::string> ModuleHeaderText(const Module &m);
// fn 行、local 行、块、指令、边
std::vector<std::string> FunctionText(const Module &m, const Function &f);

std::string TypeText(const Module &m, TypeId type);
std::string ExprText(const Function &f, ExprId expr);
// 不含位置
std::string InstText(const Function &f, const Inst &inst);

}  // namespace fir
}  // namespace oobsum

#endif
