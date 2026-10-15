//===-- DumpSink.h - Debug output of OobSummary -----------------*- C++ -*-===//
//
// 调试输出（SPEC-0001 §6；06- I-21、I-22）。相当于 CSA 的 debug.DumpCFG / ExprInspection：
// 结果不符合预期时，打开对应的种类看中间数据，定位是哪一步出的问题。只写 stderr 与
// <WorkDir>/dump/，不影响判定。
//
//   -analyzer-config rb.enhance.ipa.OobSummary:Dump=callgraph+decisions   （多个种类用 '+' 连，不能用逗号）
//   -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=^Ue_
//
// 种类（Kind）：options callgraph keys fir state summary apply decisions stats；all = 全部（keys 见 SPEC-0003）。
// 每行以 [oobsum:<种类>] 开头，lit 用例可以直接 FileCheck。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_DEBUG_DUMPSINK_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_DEBUG_DUMPSINK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"

#include <memory>
#include <set>
#include <string>

namespace oobsum {

class DumpSink {
public:
    // kinds：Dump 选项原文；filter：DumpFilter（函数名正则，空 = 全部）。不认识的种类放进 unknown
    bool Configure(llvm::StringRef kinds, llvm::StringRef filter, const std::string &workDir, std::string &unknown);
    bool On(llvm::StringRef kind) const;
    // 函数级输出先问这个（DumpFilter）
    bool Wants(llvm::StringRef kind, llvm::StringRef functionName) const;
    // stderr 一行：[oobsum:<kind>] <text>
    void Line(llvm::StringRef kind, llvm::StringRef text) const;
    // <WorkDir>/dump/<relPath>；WorkDir 为空时不写，返回空串
    std::string File(llvm::StringRef relPath, llvm::StringRef content) const;
    static const std::set<std::string> &KnownKinds();

private:
    std::set<std::string> kinds;
    std::unique_ptr<llvm::Regex> filter;
    std::string workDir;
};

}  // namespace oobsum

#endif
