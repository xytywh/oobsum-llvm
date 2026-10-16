//===-- HeaderKeys.h - Hash suffix of header-defined functions --*- C++ -*-===//
//
// 头文件里的内部链接函数的键加 #<FIR 哈希前 8 位>（06- D-02 后半、D-08，SPEC-0059）：同名、但因宏展开不同而函数体不同的
// static inline 在各 TU 里得到不同的键，solve 不再把它们当成同一个函数；函数体相同的仍是同一个键（只求解一次）。
// 纯函数，不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_HEADERKEYS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_HEADERKEYS_H

#include "OobSummary/core/Fir.h"

#include "llvm/ADT/StringRef.h"

#include <map>
#include <string>

namespace oobsum {
namespace fir {

// 键形如 F@<文件 id>:<名字>、文件 id 不是本 TU 的 id、还没有 # 后缀
bool IsHeaderFunctionKey(const Module &m, llvm::StringRef key);
// 一个函数的规范文本（哈希的内容，SPEC-0059 §2.2）：与 TU 无关的写法；引用到的函数键按模块里当前的键写
std::string CanonicalText(const Module &m, const Function &f);
// 头文件函数改成最终键，模块里对它们的引用（调用、函数地址、函数内 static）一起改；返回 基础键 → 最终键
std::map<std::string, std::string> FinalizeHeaderKeys(Module &m);

}  // namespace fir
}  // namespace oobsum

#endif
