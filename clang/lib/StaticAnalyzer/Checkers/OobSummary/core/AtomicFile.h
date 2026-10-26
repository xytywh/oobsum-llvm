//===-- AtomicFile.h - WorkDir 里的文件先写临时文件再改名 ------*- C++ -*-===//
//
// 公司 clang_check 按 cdb 每个文件起一个 bxen 进程、几十个并行，全部写同一个 WorkDir（SPEC-0118）：
//   * 临时文件名带进程号：cdb 里同一个文件出现两次（两个进程算出同一个 TU id）时，不会写进同一个临时文件；
//   * 写完才改名：进程被超时杀掉时只留下 *.tmp，读的一方（按 .json / .jsonl 扩展名找文件）看不到写了一半的文件。
// 同一个目标路径后改名的覆盖先改名的（POSIX rename 是原子的）。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ATOMICFILE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ATOMICFILE_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
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

}  // namespace oobsum

#endif
