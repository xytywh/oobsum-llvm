// core/AtomicFile.h 的单元测试（oobsum/specs/SPEC-0118）：写完改名、不留临时文件、覆盖旧文件、失败时不留东西。
// SPEC-0133：独立的不可覆盖安装接口，检查完整发布、保留目标/候选和失败证据。

#include "Test.h"

#include "OobSummary/core/AtomicFile.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <string>
#include <cstdint>
#include <limits>

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

TEST(AtomicFile, ExclusiveWritesAndInstalls)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    std::string candidate = "previous candidate", error = "previous error";
    auto result = oobsum::WriteFileExclusive(
        path, [](llvm::raw_ostream &os) { os << "complete\n"; }, candidate, error);
    EXPECT_TRUE(result == oobsum::ExclusiveInstallResult::Installed);
    EXPECT_TRUE(candidate.empty());
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(ReadAll(path), "complete\n");
    EXPECT_EQ(CountFiles(dir), 1);
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, ExclusiveExistingPreservesTarget)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    std::string error;
    ASSERT_TRUE(oobsum::WriteFileAtomic(
        path, [](llvm::raw_ostream &os) { os << "original\n"; }, error));
    std::string candidate;
    auto result = oobsum::WriteFileExclusive(
        path, [](llvm::raw_ostream &os) { os << "different\n"; }, candidate, error);
    EXPECT_TRUE(result == oobsum::ExclusiveInstallResult::Existing);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(ReadAll(path), "original\n");
    EXPECT_EQ(ReadAll(candidate), "different\n");
    EXPECT_TRUE(llvm::StringRef(candidate).startswith((path + ".export-").str()));
    EXPECT_TRUE(llvm::StringRef(candidate).endswith(".tmp"));
    EXPECT_EQ(CountFiles(dir), 2);
    llvm::sys::fs::remove(candidate);
    EXPECT_EQ(ReadAll(path), "original\n");
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, ExclusiveCandidatesAreUnique)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    std::string error, candidate;
    ASSERT_TRUE(oobsum::WriteFileExclusive(
                    path, [](llvm::raw_ostream &os) { os << "canonical"; }, candidate, error) ==
                oobsum::ExclusiveInstallResult::Installed);
    std::string first, second;
    EXPECT_TRUE(oobsum::WriteFileExclusive(
                    path, [](llvm::raw_ostream &os) { os << "one"; }, first, error) ==
                oobsum::ExclusiveInstallResult::Existing);
    EXPECT_TRUE(oobsum::WriteFileExclusive(
                    path, [](llvm::raw_ostream &os) { os << "two"; }, second, error) ==
                oobsum::ExclusiveInstallResult::Existing);
    EXPECT_TRUE(first != second);
    EXPECT_EQ(ReadAll(first), "one");
    EXPECT_EQ(ReadAll(second), "two");
    EXPECT_EQ(ReadAll(path), "canonical");
    EXPECT_EQ(CountFiles(dir), 3);
    llvm::sys::fs::remove(first);
    llvm::sys::fs::remove(second);
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, ExclusiveCreationFailure)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "missing", "tu.json");
    std::string candidate = "stale", error;
    bool invoked = false;
    auto result = oobsum::WriteFileExclusive(
        path, [&](llvm::raw_ostream &) { invoked = true; }, candidate, error);
    EXPECT_TRUE(result == oobsum::ExclusiveInstallResult::Error);
    EXPECT_FALSE(invoked);
    EXPECT_TRUE(candidate.empty());
    EXPECT_TRUE(llvm::StringRef(error).startswith("cannot create export candidate for "));
    EXPECT_EQ(CountFiles(dir), 0);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, ExclusiveWriteFailureLeavesCandidate)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    std::string candidate, error;
    auto result = oobsum::WriteFileExclusive(
        path,
        [](llvm::raw_ostream &os) {
            os << "partial\n";
            // 文件流的非法seek稳定触发I/O错误，无需依赖用户权限或填满磁盘。
            static_cast<llvm::raw_fd_ostream &>(os).seek(std::numeric_limits<uint64_t>::max());
        },
        candidate, error);
    EXPECT_TRUE(result == oobsum::ExclusiveInstallResult::Error);
    EXPECT_FALSE(candidate.empty());
    EXPECT_FALSE(llvm::sys::fs::exists(path));
    EXPECT_EQ(ReadAll(candidate), "partial\n");
    EXPECT_TRUE(llvm::StringRef(error).startswith("cannot write "));
    EXPECT_EQ(CountFiles(dir), 1);  // 错误证据留给调用方/阶段扫描，不自动删除。
    llvm::sys::fs::remove(candidate);
    llvm::sys::fs::remove(dir);
}

TEST(AtomicFile, ExclusiveDirectoryNotReplaced)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-exclusive-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    ASSERT_TRUE(!llvm::sys::fs::create_directory(path));
    std::string candidate, error;
    auto result = oobsum::WriteFileExclusive(
        path, [](llvm::raw_ostream &os) { os << "new\n"; }, candidate, error);
    EXPECT_TRUE(result != oobsum::ExclusiveInstallResult::Installed);
    EXPECT_TRUE(llvm::sys::fs::is_directory(path));
    EXPECT_EQ(CountFiles(path), 0);
    EXPECT_EQ(ReadAll(candidate), "new\n");
    EXPECT_EQ(CountFiles(dir), 2);
    llvm::sys::fs::remove(candidate);
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}
