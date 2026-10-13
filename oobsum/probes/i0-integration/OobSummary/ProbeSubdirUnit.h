//===-- ProbeSubdirUnit.h - subdirectory compile check for the I0 probe ---===//
//
// 探针的第二个编译单元，故意放在 Checkers/OobSummary/ 子目录里：
// 如果 CMakeLists.txt 里追加 "OobSummary/ProbeSubdirUnit.cpp" 能编译、链接通过，
// 正式代码就按"入口文件平铺 + 引擎在子目录"的布局放（06-spec.md §9.1）。
//
//===----------------------------------------------------------------------===//

#ifndef OOBSUMMARY_PROBE_SUBDIR_UNIT_H
#define OOBSUMMARY_PROBE_SUBDIR_UNIT_H

#include "llvm/ADT/StringRef.h"

#include <string>

namespace oobsum {

// 路径规范化（make_absolute + remove_dots）后取 xxHash64，16 位小写十六进制
std::string HashNormalizedPath(llvm::StringRef path);

}  // namespace oobsum

#endif
