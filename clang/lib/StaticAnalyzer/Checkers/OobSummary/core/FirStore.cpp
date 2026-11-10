//===-- FirStore.cpp - Immutable FIR publication and phase guards -*- C++ -*-===//
//
// SPEC-0133 / X-01、X-03、X-04：输入冲突和不完整发布不能产生看似成功的结果。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/FirStore.h"

#include "OobSummary/core/AtomicFile.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/FirJson.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <system_error>

namespace oobsum {
namespace {

std::string In(llvm::StringRef dir, llvm::StringRef name)
{
    llvm::SmallString<256> path(dir);
    llvm::sys::path::append(path, name);
    return path.str().str();
}

bool MakeDirectory(llvm::StringRef path, std::string &error)
{
    if (std::error_code ec = llvm::sys::fs::create_directories(path)) {
        error = "cannot create input directory " + path.str() + ": " + ec.message();
        return false;
    }
    return true;
}

bool RemoveCandidate(llvm::StringRef path, std::string &error)
{
    if (std::error_code ec = llvm::sys::fs::remove(path, false)) {
        error = "cannot remove input candidate " + path.str() + ": " + ec.message();
        return false;
    }
    return true;
}

std::string Serialize(const llvm::json::Value &value)
{
    std::string text;
    llvm::raw_string_ostream stream(text);
    stream << llvm::formatv("{0:1}", value) << "\n";
    return stream.str();
}

// 仅冲突时保存；同一快照被多个导出者发布仍需幂等，哈希相同不代表可以覆盖不同内容。
bool SaveSnapshot(llvm::StringRef dir, llvm::StringRef text, std::string &error)
{
    std::string path = In(dir, HashHex(text) + ".json");
    std::string candidate;
    auto result = WriteFileExclusive(
        path, [&](llvm::raw_ostream &os) { os << text; }, candidate, error);
    if (result == ExclusiveInstallResult::Installed) {
        return true;
    }
    if (result == ExclusiveInstallResult::Error) {
        return false;
    }
    auto previous = llvm::MemoryBuffer::getFile(path);
    if (!previous) {
        error = "cannot read input snapshot " + path + ": " + previous.getError().message();
        return false;
    }
    if ((*previous)->getBuffer() != text) {
        error = "input snapshot hash collision; existing content and candidate retained";
        return false;
    }
    return RemoveCandidate(candidate, error);
}

// 缺少标记是正常状态；类型不对、权限问题或其它status错误均不能当作“不存在”。
bool DirectoryMarker(llvm::StringRef path, bool &present, std::string &error)
{
    llvm::sys::fs::file_status status;
    std::error_code ec = llvm::sys::fs::status(path, status, false);
    if (ec == std::errc::no_such_file_or_directory) {
        present = false;
        return true;
    }
    if (ec) {
        error = "cannot inspect input path " + path.str() + ": " + ec.message();
        return false;
    }
    present = llvm::sys::fs::exists(status);
    if (present && llvm::sys::fs::is_symlink_file(status)) {
        // 用户可用目录别名放置WorkDir/FIR；悬空链接已确认存在，不能按缺标记处理。
        if (std::error_code followError = llvm::sys::fs::status(path, status)) {
            error = "cannot follow input directory " + path.str() + ": " + followError.message();
            return false;
        }
    }
    if (present && !llvm::sys::fs::is_directory(status)) {
        error = "input path is not a directory: " + path.str();
        return false;
    }
    return true;
}

}  // namespace

bool PublishFir(llvm::StringRef workDir, const fir::Module &module, std::string &path, std::string &error)
{
    path.clear();
    error.clear();
    if (workDir.empty() || module.tu.empty() || module.tu == "." || module.tu == ".." ||
        llvm::StringRef(module.tu).contains('/') || llvm::StringRef(module.tu).contains('\\')) {
        error = "FIR publication requires WorkDir and a valid TU identifier";
        return false;
    }
    if (!MakeDirectory(In(workDir, "inputs-pending"), error)) {
        return false;
    }
    std::string firDir = In(workDir, "fir");
    if (!MakeDirectory(firDir, error)) {
        return false;
    }
    std::string canonical = In(firDir, module.tu + ".json");
    llvm::json::Value current = fir::ModuleToJson(module);
    std::string candidate;
    auto result = WriteFileExclusive(
        canonical, [&](llvm::raw_ostream &os) { os << llvm::formatv("{0:1}", current) << "\n"; }, candidate, error);
    if (result == ExclusiveInstallResult::Error) {
        return false;
    }
    if (result == ExclusiveInstallResult::Installed) {
        path = canonical;
        return true;
    }
    fir::Module previous;
    if (!fir::ReadModuleFile(canonical, previous, error)) {
        return false;
    }
    llvm::json::Value normalized = fir::ModuleToJson(previous);
    if (normalized == current) {
        if (!RemoveCandidate(candidate, error)) {
            return false;
        }
        path = canonical;
        return true;
    }
    // 先建立不可忽略的冲突标记。即使随后保存快照时出错或进程被杀，空目录也会阻止求解。
    std::string conflictDir = In(In(workDir, "fir-conflicts"), module.tu);
    if (!MakeDirectory(conflictDir, error) || !SaveSnapshot(conflictDir, Serialize(normalized), error) ||
        !SaveSnapshot(conflictDir, Serialize(current), error)) {
        return false;
    }
    if (!RemoveCandidate(candidate, error)) {
        return false;
    }
    error = "conflicting FIR variants; input snapshots retained in fir-conflicts";
    return false;
}

FirInputState InspectFirInputs(llvm::StringRef workDir, bool scanTemporary)
{
    FirInputState state;
    if (workDir.empty()) {
        state.error = "input inspection requires WorkDir";
        return state;
    }
    bool workDirPresent = false;
    if (!DirectoryMarker(workDir, workDirPresent, state.error)) {
        return state;
    }
    if (!workDirPresent) {
        state.error = "input WorkDir does not exist";
        return state;
    }
    if (!DirectoryMarker(In(workDir, "inputs-pending"), state.pending, state.error) ||
        !DirectoryMarker(In(workDir, "fir-conflicts"), state.conflict, state.error) || !scanTemporary) {
        return state;
    }
    if (state.conflict) {
        std::error_code ec;
        for (llvm::sys::fs::directory_iterator it(In(workDir, "fir-conflicts"), ec), end; it != end && !ec;
             it.increment(ec)) {
            bool present = false;
            if (!DirectoryMarker(it->path(), present, state.error)) {
                return state;
            }
            if (present) {
                ++state.conflictingTus;
            }
        }
        if (ec) {
            state.error = "cannot scan conflicting FIR inputs: " + ec.message();
            return state;
        }
    }
    std::string firDir = In(workDir, "fir");
    bool firPresent = false;
    if (!DirectoryMarker(firDir, firPresent, state.error) || !firPresent) {
        return state;
    }
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(firDir, ec), end; it != end && !ec; it.increment(ec)) {
        if (llvm::sys::path::extension(it->path()) != ".tmp") {
            continue;
        }
        ++state.incompleteFiles;
        llvm::sys::fs::file_status status;
        if (std::error_code statError = llvm::sys::fs::status(it->path(), status, false)) {
            state.error = "cannot inspect FIR candidate: " + statError.message();
            return state;
        }
        if (!llvm::sys::fs::is_regular_file(status)) {
            state.error = "FIR candidate is not a regular file";
            return state;
        }
    }
    if (ec) {
        state.error = "cannot scan FIR candidates: " + ec.message();
    }
    return state;
}

bool MarkFirSolved(llvm::StringRef workDir, std::string &error)
{
    error.clear();
    if (workDir.empty()) {
        error = "clearing input state requires WorkDir";
        return false;
    }
    std::string marker = In(workDir, "inputs-pending");
    bool present = false;
    if (!DirectoryMarker(marker, present, error)) {
        return false;
    }
    if (!present) {
        return true;  // 兼容没有标记的旧干净WorkDir。
    }
    // 标记本身也可能是目录别名，unlink不会检查目标是否为空，需先显式保住其中的证据。
    std::error_code scanError;
    llvm::sys::fs::directory_iterator it(marker, scanError), end;
    if (scanError) {
        error = "cannot inspect pending FIR directory: " + scanError.message();
        return false;
    }
    if (it != end) {
        error = "cannot clear nonempty pending FIR directory";
        return false;
    }
    if (std::error_code ec = llvm::sys::fs::remove(marker, false)) {
        error = "cannot clear pending FIR inputs: " + ec.message();
        return false;
    }
    return true;
}

}  // namespace oobsum
