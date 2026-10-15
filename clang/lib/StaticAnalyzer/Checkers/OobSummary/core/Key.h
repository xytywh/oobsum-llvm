//===-- Key.h - Keys of functions and globals -------------------*- C++ -*-===//
//
// 函数 / 全局的键（SPEC-0003 §2.1；06- D-01…D-05，决策 D13：自定义键，不用 USR）：
//   F:<名字>                外部链接函数（D-01）
//   F@<文件 id>:<名字>      内部链接函数（D-02；头文件里的以后再加 #<FIR 哈希前 8 位>，S4）
//   G:<名字> / G@<文件 id>:<名字>   外部 / 内部链接全局（D-03、D-04）
//   S@<函数键>:<名字>       函数内 static（D-04）
// 文件 id（D-05）：规范化绝对路径的 xxHash64，16 位小写十六进制；TU id 用同一规则。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_KEY_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_KEY_H

#include "llvm/ADT/StringRef.h"

#include <string>

namespace oobsum {

// make_absolute（相对进程当前目录）+ remove_dots + 统一成 '/'
std::string NormalizePath(llvm::StringRef path);
// D-05
std::string FileId(llvm::StringRef path);

// D-01 / D-02：external 为假时用 fileId（定义文件的 id）
std::string FunctionKey(llvm::StringRef name, bool external, llvm::StringRef fileId);
// D-03 / D-04
std::string GlobalKey(llvm::StringRef name, bool external, llvm::StringRef fileId);
// D-04：函数内 static
std::string StaticLocalKey(llvm::StringRef functionKey, llvm::StringRef name);

// 从键取回名字：最后一个 ':' 之后、'#' 之前（与 cases/run.py 的 _fn_name 同义）
llvm::StringRef KeyName(llvm::StringRef key);

}  // namespace oobsum

#endif
