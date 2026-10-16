//===-- Key.cpp - Keys of functions and globals -----------------*- C++ -*-===//
//
// 见 Key.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Key.h"

#include "OobSummary/core/Fingerprint.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <cctype>

namespace oobsum {

std::string NormalizePath(llvm::StringRef path)
{
    llvm::SmallString<256> out(path);
    // 失败（取不到当前目录）时保留原样：键仍确定，只是相对路径与绝对路径的 id 不一致
    (void)llvm::sys::fs::make_absolute(out);
    llvm::sys::path::remove_dots(out, true);
    llvm::sys::path::native(out, llvm::sys::path::Style::posix);
    return std::string(out);
}

std::string FileId(llvm::StringRef path)
{
    return HashHex(NormalizePath(path));
}

std::string FunctionKey(llvm::StringRef name, bool external, llvm::StringRef fileId)
{
    return external ? ("F:" + name).str() : ("F@" + fileId + ":" + name).str();
}

std::string GlobalKey(llvm::StringRef name, bool external, llvm::StringRef fileId)
{
    return external ? ("G:" + name).str() : ("G@" + fileId + ":" + name).str();
}

std::string StaticLocalKey(llvm::StringRef functionKey, llvm::StringRef name)
{
    return ("S@" + functionKey + ":" + name).str();
}

llvm::StringRef KeyName(llvm::StringRef key)
{
    size_t colon = key.rfind(':');
    llvm::StringRef name = colon == llvm::StringRef::npos ? key : key.substr(colon + 1);
    return name.split('#').first;
}

std::string GlobalSymbol(llvm::StringRef key, llvm::StringRef name)
{
    if (key.startswith("G:")) {
        return ("g:" + name).str();
    }
    return ("g:" + name + "__S" + HashHex(key).substr(0, 16)).str();
}

std::string StripGlobalSuffix(std::string text)
{
    auto isHex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    auto isIdent = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    for (size_t at = text.find("__S"); at != std::string::npos; at = text.find("__S", at + 1)) {
        size_t end = at + 3 + 16;
        bool hex = end <= text.size() && std::all_of(text.begin() + at + 3, text.begin() + end, isHex);
        if (hex && (end == text.size() || !isIdent(text[end]))) {
            text.erase(at, end - at);
        }
    }
    return text;
}

}  // namespace oobsum
