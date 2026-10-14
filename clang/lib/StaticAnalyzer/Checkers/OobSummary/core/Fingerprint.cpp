//===-- Fingerprint.cpp - Stable ids for TUs and decisions ------*- C++ -*-===//
//
// 见 Fingerprint.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Fingerprint.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"

namespace oobsum {

std::string HashHex(llvm::StringRef text)
{
    std::string out;
    llvm::raw_string_ostream os(out);
    os << llvm::format_hex_no_prefix(llvm::xxHash64(text), 16);
    return os.str();
}

std::string TuId(llvm::StringRef mainFilePath)
{
    llvm::SmallString<256> path(mainFilePath);
    llvm::sys::path::remove_dots(path, true);
    llvm::sys::path::native(path, llvm::sys::path::Style::posix);
    return HashHex(path);
}

std::string DecisionFingerprint(llvm::StringRef label, llvm::StringRef entryKey, llvm::StringRef accessKey,
                                llvm::StringRef kind, llvm::StringRef accessEnd, llvm::StringRef obj)
{
    std::string text =
        ("OobSummary|" + label + "|" + entryKey + "|" + accessKey + "|" + kind + "|" + accessEnd + "|" + obj).str();
    return HashHex(text);
}

}  // namespace oobsum
