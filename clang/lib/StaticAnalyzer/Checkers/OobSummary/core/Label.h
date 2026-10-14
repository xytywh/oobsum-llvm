//===-- Label.h - Verdict labels of OobSummary decisions --------*- C++ -*-===//
//
// 判定标签（oobsum/specs/SPEC-0001）：每个判定恰好一个标签，告警文本、BugType、
// 判定日志里都带着它，后续的大模型复核按标签与原因分流。
//
//   DEFINITE  对所有取值都越界（06- F-11，L1）                    → 报
//   POSSIBLE  上界来自代码、超出对象；下界不越界（F-12，L2）        → 报
//   SUSPECT   证明不了安全：上界只来自类型范围 / widening、到入口仍
//             未证明、调用链断开（未知函数、函数指针…）（召回优先）  → 报（Policy=recall）
//   SAFE      证明了安全（常量、守卫为假、守卫蕴含、守卫界）（F-10） → 只进判定日志；ReportSafe=true 时也报
//
// 本文件不依赖 Clang（core/ 分层规则，06- §9.1）。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_LABEL_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_LABEL_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

namespace oobsum {

enum class Label { Definite, Possible, Suspect, Safe };

// 机器名：判定日志、选项值里用（definite / possible / suspect / safe）
llvm::StringRef LabelName(Label label);
// 告警文本前缀里用（OOB-DEFINITE / OOB-POSSIBLE / OOB-SUSPECT / OOB-SAFE）
llvm::StringRef LabelTag(Label label);
llvm::Optional<Label> ParseLabel(llvm::StringRef name);

}  // namespace oobsum

#endif
