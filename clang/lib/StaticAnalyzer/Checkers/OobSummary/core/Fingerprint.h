//===-- Fingerprint.h - Stable ids for TUs and decisions --------*- C++ -*-===//
//
// 06- F-34：指纹不含行号；访问用"访问函数键 + 访问点自身的末端式"标识，对象用机器写法。
// 用 llvm::xxHash64（replay.py 里有逐位相同的实现，实测一致）。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FINGERPRINT_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FINGERPRINT_H

#include "llvm/ADT/StringRef.h"

#include <string>

namespace oobsum {

// 16 位十六进制
std::string HashHex(llvm::StringRef text);
// 规范化路径（去掉 ./、合并 ..、统一分隔符）后的哈希：TU 的 id（驱动脚本按同一规则从 compile_commands.json 算）
std::string TuId(llvm::StringRef mainFilePath);
// F-34
std::string DecisionFingerprint(llvm::StringRef label, llvm::StringRef entryKey, llvm::StringRef accessKey,
                                llvm::StringRef kind, llvm::StringRef accessEnd, llvm::StringRef obj);

}  // namespace oobsum

#endif
