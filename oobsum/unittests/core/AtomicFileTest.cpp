// core/AtomicFile.h 的单元测试（oobsum/specs/SPEC-0118）：写完改名、不留临时文件、覆盖旧文件、失败时不留东西。

#include "Test.h"

#include "OobSummary/core/AtomicFile.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <string>

namespace {

int CountFiles(llvm::StringRef dir)
{
    int n = 0;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        ++n;
    }
    return n;
}

std::string ReadAll(llvm::StringRef path)
{
    auto buffer = llvm::MemoryBuffer::getFile(path);
    return buffer ? (*buffer)->getBuffer().str() : "<missing>";
}

}  // namespace

TEST(AtomicFile, WritesAndRenames)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-atomic-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.jsonl");
    std::string error;
    // 临时文件名带进程号
    EXPECT_TRUE(llvm::StringRef(oobsum::TempPathFor(path)).startswith((path + ".").str()));
    EXPECT_TRUE(llvm::StringRef(oobsum::TempPathFor(path)).endswith(".tmp"));
    ASSERT_TRUE(oobsum::WriteFileAtomic(
        path, [](llvm::raw_ostream &os) { os << "first\n"; }, error));
    EXPECT_EQ(ReadAll(path), "first\n");
    EXPECT_EQ(CountFiles(dir), 1);  // 没有留下 .tmp
    // 再写一次：覆盖
    ASSERT_TRUE(oobsum::WriteFileAtomic(
        path, [](llvm::raw_ostream &os) { os << "second\n"; }, error));
    EXPECT_EQ(ReadAll(path), "second\n");
    EXPECT_EQ(CountFiles(dir), 1);
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, FailureLeavesNothing)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-atomic-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "no-such-dir", "tu.json");
    std::string error;
    EXPECT_FALSE(oobsum::WriteFileAtomic(
        path, [](llvm::raw_ostream &os) { os << "x"; }, error));
    EXPECT_TRUE(llvm::StringRef(error).startswith("cannot write "));
    EXPECT_EQ(CountFiles(dir), 0);
    llvm::sys::fs::remove(dir);
}
