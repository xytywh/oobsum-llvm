//===-- DumpSink.cpp - Debug output of OobSummary ---------------*- C++ -*-===//
//
// 见 DumpSink.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/debug/DumpSink.h"

#include "OobSummary/core/Policy.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

namespace oobsum {

const std::set<std::string> &DumpSink::KnownKinds()
{
    static const std::set<std::string> known = {"options", "callgraph", "fir",       "state",
                                                "summary", "apply",     "decisions", "stats"};
    return known;
}

bool DumpSink::Configure(llvm::StringRef kindText, llvm::StringRef filterText, const std::string &dir,
                         std::string &unknown)
{
    workDir = dir;
    for (const std::string &kind : SplitList(kindText)) {
        if (kind == "all") {
            kinds.insert(KnownKinds().begin(), KnownKinds().end());
        } else if (KnownKinds().count(kind)) {
            kinds.insert(kind);
        } else {
            unknown = kind;
            return false;
        }
    }
    if (!filterText.empty()) {
        filter = std::make_unique<llvm::Regex>(filterText);
        std::string error;
        if (!filter->isValid(error)) {
            unknown = "DumpFilter: " + error;
            return false;
        }
    }
    return true;
}

bool DumpSink::On(llvm::StringRef kind) const
{
    return kinds.count(kind.str()) != 0;
}

bool DumpSink::Wants(llvm::StringRef kind, llvm::StringRef functionName) const
{
    return On(kind) && (!filter || filter->match(functionName));
}

void DumpSink::Line(llvm::StringRef kind, llvm::StringRef text) const
{
    llvm::errs() << "[oobsum:" << kind << "] " << text << "\n";
}

std::string DumpSink::File(llvm::StringRef relPath, llvm::StringRef content) const
{
    if (workDir.empty()) {
        return "";
    }
    llvm::SmallString<256> path(workDir);
    llvm::sys::path::append(path, "dump", relPath);
    if (llvm::sys::fs::create_directories(llvm::sys::path::parent_path(path))) {
        return "";
    }
    std::error_code ec;
    llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        return "";
    }
    os << content;
    return std::string(path);
}

}  // namespace oobsum
