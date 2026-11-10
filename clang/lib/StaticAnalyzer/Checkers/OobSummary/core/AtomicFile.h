//===-- AtomicFile.h - WorkDir 里的文件先写临时文件再改名 ------*- C++ -*-===//
//
// 公司 clang_check 按 cdb 每个文件起一个 bxen 进程、几十个并行，全部写同一个 WorkDir（SPEC-0118）：
//   * 临时文件名带进程号：cdb 里同一个文件出现两次（两个进程算出同一个 TU id）时，不会写进同一个临时文件；
//   * 写完才改名：进程被超时杀掉时只留下 *.tmp，读的一方（按 .json / .jsonl 扩展名找文件）看不到写了一半的文件。
// 同一个目标路径后改名的覆盖先改名的（POSIX rename 是原子的）。
// SPEC-0133 / X-01：FIR 改用下方独立的不可覆盖安装接口；原有原子覆盖接口保持不变。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ATOMICFILE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ATOMICFILE_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <system_error>

namespace oobsum {

// <path>.<进程号>.tmp
inline std::string TempPathFor(llvm::StringRef path)
{
    return (path + "." + std::to_string(static_cast<long long>(llvm::sys::Process::getProcessId())) + ".tmp").str();
}

// 用 write 写出内容，成功后改名成 path；失败时删掉临时文件、error 写原因
inline bool WriteFileAtomic(llvm::StringRef path, llvm::function_ref<void(llvm::raw_ostream &)> write,
                            std::string &error)
{
    std::string tmp = TempPathFor(path);
    {
        std::error_code ec;
        llvm::raw_fd_ostream os(tmp, ec, llvm::sys::fs::OF_Text);
        if (ec) {
            error = "cannot write " + tmp + ": " + ec.message();
            return false;
        }
        write(os);
        os.close();
        if (os.has_error()) {
            error = "cannot write " + tmp + ": " + os.error().message();
            os.clear_error();
            llvm::sys::fs::remove(tmp);
            return false;
        }
    }
    if (std::error_code ec = llvm::sys::fs::rename(tmp, path)) {
        error = "cannot rename " + tmp + " to " + path.str() + ": " + ec.message();
        llvm::sys::fs::remove(tmp);
        return false;
    }
    return true;
}

enum class ExclusiveInstallResult { Installed, Existing, Error };

// SPEC-0133 / X-01：先写唯一候选、关闭，再用硬链接不可覆盖地发布。
// Installed 会移除候选名并清空 candidate；Existing 保留完整候选供调用方比较。
// Error 不移除已创建的候选，供阶段扫描阻断后续分析；创建候选前失败则 candidate 为空。
// Existing 只表示目标路径已存在，文件类型/内容仍须调用方校验。清理候选失败也返回 Error，
// 此时目标可能已安装，不能据此删除目标或降级为 rename 覆盖。
inline ExclusiveInstallResult WriteFileExclusive(llvm::StringRef path,
                                                 llvm::function_ref<void(llvm::raw_ostream &)> write,
                                                 std::string &candidate, std::string &error)
{
    candidate.clear();
    error.clear();
    llvm::SmallString<256> uniquePath;
    int fd = -1;
    if (std::error_code ec =
            llvm::sys::fs::createUniqueFile(path + ".export-%%%%%%.tmp", fd, uniquePath, llvm::sys::fs::OF_Text)) {
        error = "cannot create export candidate for " + path.str() + ": " + ec.message();
        return ExclusiveInstallResult::Error;
    }
    candidate = uniquePath.str().str();
    {
        llvm::raw_fd_ostream os(fd, true);
        write(os);
        os.close();
        if (os.has_error()) {
            error = "cannot write " + candidate + ": " + os.error().message();
            os.clear_error();
            return ExclusiveInstallResult::Error;
        }
    }
    // create_hard_link 的第二个参数是待创建的新目录项；与候选处在同一目录。
    std::error_code ec = llvm::sys::fs::create_hard_link(candidate, path);
    if (ec == std::errc::file_exists) {
        return ExclusiveInstallResult::Existing;
    }
    if (ec) {
        error = "cannot install " + candidate + " as " + path.str() + ": " + ec.message();
        return ExclusiveInstallResult::Error;
    }
    // 安装后两者指向同一 inode，只删除候选目录项，不再修改候选内容。
    if (std::error_code cleanup = llvm::sys::fs::remove(candidate)) {
        error = "cannot remove installed export candidate " + candidate + ": " + cleanup.message();
        return ExclusiveInstallResult::Error;
    }
    candidate.clear();
    return ExclusiveInstallResult::Installed;
}

}  // namespace oobsum

#endif
