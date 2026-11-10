//===-- FirStoreTest.cpp - Immutable FIR inputs and phase guards -*- C++ -*-===//
//
// SPEC-0133 / X-01、X-03、X-04：正常单版本幂等，冲突/未完成输入不允许被当作成功结果。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/AtomicFile.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/FirJson.h"
#include "OobSummary/core/FirStore.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <cstddef>
#include <system_error>

namespace {

std::string At(llvm::StringRef dir, llvm::StringRef name)
{
    llvm::SmallString<256> path(dir);
    llvm::sys::path::append(path, name);
    return path.str().str();
}

std::string ReadAll(llvm::StringRef path)
{
    auto buffer = llvm::MemoryBuffer::getFile(path);
    return buffer ? (*buffer)->getBuffer().str() : "<missing>";
}

int CountFiles(llvm::StringRef path)
{
    int count = 0;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(path, ec), end; it != end && !ec; it.increment(ec)) {
        ++count;
    }
    return count;
}

oobsum::fir::Module Module(unsigned ptr = 4)
{
    oobsum::fir::Module module;
    module.tu = "test-tu";
    module.file = "/synthetic/source.c";
    module.files.push_back(module.file);
    module.target.triple = ptr == 4 ? "armebv7-linux-gnueabi" : "x86_64-pc-linux-gnu";
    module.target.ptr = ptr;
    module.target.longSize = ptr;
    module.target.bigEndian = ptr == 4;
    return module;
}

bool Write(llvm::StringRef path, llvm::StringRef text)
{
    std::string error;
    return oobsum::WriteFileAtomic(
        path, [&](llvm::raw_ostream &os) { os << text; }, error);
}

}  // namespace

TEST(FirStore, PublishesAndNormalizesDuplicates)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    auto module = Module();
    std::string path, error;
    ASSERT_TRUE(oobsum::PublishFir(dir, module, path, error));
    EXPECT_EQ(path, At(At(dir, "fir"), "test-tu.json"));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(CountFiles(At(dir, "fir")), 1);
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_TRUE(state.pending);
    EXPECT_TRUE(state.ReadyForSolve());
    EXPECT_FALSE(state.ReadyForReport());
    // 兼容旧的紧凑JSON：比较读回后的规范对象，不按缩进/换行判为冲突。
    std::string compact;
    llvm::raw_string_ostream stream(compact);
    stream << oobsum::fir::ModuleToJson(module);
    ASSERT_TRUE(Write(path, stream.str()));
    ASSERT_TRUE(oobsum::PublishFir(dir, module, path, error));
    EXPECT_EQ(ReadAll(path), compact);
    EXPECT_EQ(CountFiles(At(dir, "fir")), 1);
    EXPECT_FALSE(llvm::sys::fs::exists(At(dir, "fir-conflicts")));
    ASSERT_TRUE(oobsum::MarkFirSolved(dir, error));
    state = oobsum::InspectFirInputs(dir, false);
    EXPECT_FALSE(state.pending);
    EXPECT_TRUE(state.ReadyForReport());
    EXPECT_TRUE(oobsum::MarkFirSolved(dir, error));  // 旧干净WorkDir/重复清除标记。
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, ConflictingVariantsPreserveCanonical)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    auto first = Module(4), second = Module(8);
    std::string path, error;
    ASSERT_TRUE(oobsum::PublishFir(dir, first, path, error));
    std::string canonical = path;
    std::string original = ReadAll(canonical);
    EXPECT_FALSE(oobsum::PublishFir(dir, second, path, error));
    EXPECT_TRUE(path.empty());
    EXPECT_TRUE(llvm::StringRef(error).contains("conflicting FIR variants"));
    EXPECT_EQ(ReadAll(canonical), original);
    EXPECT_EQ(CountFiles(At(dir, "fir")), 1);
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_TRUE(state.pending);
    EXPECT_TRUE(state.conflict);
    EXPECT_EQ(state.conflictingTus, size_t(1));
    EXPECT_EQ(state.incompleteFiles, size_t(0));
    EXPECT_FALSE(state.ReadyForSolve());
    EXPECT_FALSE(state.ReadyForReport());
    std::string conflictDir = At(At(dir, "fir-conflicts"), first.tu);
    EXPECT_EQ(CountFiles(conflictDir), 2);
    unsigned ptrMask = 0;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(conflictDir, ec), end; it != end && !ec; it.increment(ec)) {
        oobsum::fir::Module snapshot;
        EXPECT_TRUE(oobsum::fir::ReadModuleFile(it->path(), snapshot, error));
        ptrMask |= 1u << snapshot.target.ptr;
    }
    EXPECT_TRUE(!ec);
    EXPECT_EQ(ptrMask, (1u << 4) | (1u << 8));
    // 同一冲突变体重试不再增加正文，既有版本也不被覆盖。
    EXPECT_FALSE(oobsum::PublishFir(dir, second, path, error));
    EXPECT_EQ(ReadAll(canonical), original);
    EXPECT_EQ(CountFiles(conflictDir), 2);
    EXPECT_EQ(CountFiles(At(dir, "fir")), 1);
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, EmptyConflictRootBlocksBothPhases)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    ASSERT_TRUE(!llvm::sys::fs::create_directory(At(dir, "fir-conflicts")));
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_TRUE(state.conflict);
    EXPECT_EQ(state.conflictingTus, size_t(0));
    EXPECT_TRUE(state.error.empty());
    EXPECT_FALSE(state.ReadyForSolve());
    EXPECT_FALSE(oobsum::InspectFirInputs(dir, false).ReadyForReport());
    ASSERT_TRUE(!llvm::sys::fs::create_directory(At(At(dir, "fir-conflicts"), "one-tu")));
    ASSERT_TRUE(!llvm::sys::fs::create_directory(At(At(dir, "fir-conflicts"), "another-tu")));
    EXPECT_EQ(oobsum::InspectFirInputs(dir, true).conflictingTus, size_t(2));
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, TemporaryCandidatesBlockSolve)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    ASSERT_TRUE(!llvm::sys::fs::create_directory(At(dir, "fir")));
    ASSERT_TRUE(Write(At(At(dir, "fir"), "tu.123.tmp"), "old incomplete writer"));
    ASSERT_TRUE(Write(At(At(dir, "fir"), "tu.json.export-abc123.tmp"), "new incomplete writer"));
    ASSERT_TRUE(Write(At(At(dir, "fir"), "not-temporary.json"), "not inspected by this guard"));
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_EQ(state.incompleteFiles, size_t(2));
    EXPECT_TRUE(state.error.empty());
    EXPECT_FALSE(state.ReadyForSolve());
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, SolvedMarkerMustBeEmptyDirectory)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    std::string path, error;
    ASSERT_TRUE(oobsum::PublishFir(dir, Module(), path, error));
    std::string evidence = At(At(dir, "inputs-pending"), "evidence");
    ASSERT_TRUE(Write(evidence, "keep"));
    EXPECT_FALSE(oobsum::MarkFirSolved(dir, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(ReadAll(evidence), "keep");
    EXPECT_FALSE(oobsum::InspectFirInputs(dir, false).ReadyForReport());
    ASSERT_TRUE(!llvm::sys::fs::remove(evidence));
    ASSERT_TRUE(oobsum::MarkFirSolved(dir, error));
    EXPECT_TRUE(oobsum::InspectFirInputs(dir, false).ReadyForReport());
    EXPECT_EQ(CountFiles(At(dir, "fir")), 1);
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, FileSystemTypeErrorsAreNotAbsence)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    std::string path, error;
    std::string badRoot = At(dir, "regular-file");
    ASSERT_TRUE(Write(badRoot, "not a directory"));
    EXPECT_FALSE(oobsum::PublishFir(badRoot, Module(), path, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(oobsum::InspectFirInputs(badRoot, true).ReadyForSolve());
    EXPECT_FALSE(oobsum::MarkFirSolved(badRoot, error));
    std::string pending = At(dir, "inputs-pending");
    ASSERT_TRUE(Write(pending, "not a directory"));
    EXPECT_FALSE(oobsum::InspectFirInputs(dir, false).error.empty());
    EXPECT_FALSE(oobsum::MarkFirSolved(dir, error));
    EXPECT_EQ(ReadAll(pending), "not a directory");
    ASSERT_TRUE(!llvm::sys::fs::remove(pending));
    std::string fir = At(dir, "fir");
    ASSERT_TRUE(Write(fir, "not a directory"));
    EXPECT_FALSE(oobsum::InspectFirInputs(dir, true).ReadyForSolve());
    // report只检查两个阶段标记；不扫描或访问FIR目录本身。
    EXPECT_TRUE(oobsum::InspectFirInputs(dir, false).ReadyForReport());
    std::string conflict = At(dir, "fir-conflicts");
    ASSERT_TRUE(Write(conflict, "not a directory"));
    auto state = oobsum::InspectFirInputs(dir, false);
    EXPECT_TRUE(state.conflict);
    EXPECT_FALSE(state.error.empty());
    EXPECT_FALSE(state.ReadyForReport());
    EXPECT_FALSE(oobsum::PublishFir("", Module(), path, error));
    EXPECT_FALSE(oobsum::InspectFirInputs("", true).error.empty());
    EXPECT_FALSE(oobsum::MarkFirSolved("", error));
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, SnapshotCollisionRetainsAllEvidence)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    auto first = Module(4);
    std::string path, error;
    ASSERT_TRUE(oobsum::PublishFir(dir, first, path, error));
    std::string canonical = path, original = ReadAll(path);
    std::string conflicts = At(At(dir, "fir-conflicts"), first.tu);
    ASSERT_TRUE(!llvm::sys::fs::create_directories(conflicts));
    std::string occupied = At(conflicts, oobsum::HashHex(original) + ".json");
    ASSERT_TRUE(Write(occupied, "different existing bytes\n"));
    EXPECT_FALSE(oobsum::PublishFir(dir, Module(8), path, error));
    EXPECT_TRUE(llvm::StringRef(error).contains("hash collision"));
    EXPECT_EQ(ReadAll(occupied), "different existing bytes\n");
    EXPECT_EQ(ReadAll(canonical), original);
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_TRUE(state.conflict);
    EXPECT_EQ(state.incompleteFiles, size_t(1));
    EXPECT_FALSE(state.ReadyForSolve());
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, MalformedCanonicalLeavesCandidate)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    ASSERT_TRUE(!llvm::sys::fs::create_directory(At(dir, "fir")));
    std::string canonical = At(At(dir, "fir"), "test-tu.json");
    ASSERT_TRUE(Write(canonical, "not JSON\n"));
    std::string path, error;
    EXPECT_FALSE(oobsum::PublishFir(dir, Module(), path, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(path.empty());
    EXPECT_EQ(ReadAll(canonical), "not JSON\n");
    auto state = oobsum::InspectFirInputs(dir, true);
    EXPECT_TRUE(state.pending);
    EXPECT_EQ(state.incompleteFiles, size_t(1));
    EXPECT_FALSE(state.ReadyForSolve());
    llvm::sys::fs::remove_directories(dir);
}

TEST(FirStore, DirectoryAliasesAndDanglingMarkers)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-store-test", dir));
    std::string real = At(dir, "real"), alias = At(dir, "alias");
    ASSERT_TRUE(!llvm::sys::fs::create_directory(real));
    ASSERT_TRUE(!llvm::sys::fs::create_link(real, alias));
    std::string path, error;
    ASSERT_TRUE(oobsum::PublishFir(alias, Module(), path, error));
    EXPECT_TRUE(oobsum::InspectFirInputs(alias, true).ReadyForSolve());
    ASSERT_TRUE(oobsum::MarkFirSolved(alias, error));
    EXPECT_TRUE(oobsum::InspectFirInputs(alias, false).ReadyForReport());
    // FIR目录本身可为有效别名，相同内容仍保持幂等。
    std::string moved = At(dir, "fir-target");
    ASSERT_TRUE(!llvm::sys::fs::rename(At(real, "fir"), moved));
    ASSERT_TRUE(!llvm::sys::fs::create_link(moved, At(real, "fir")));
    ASSERT_TRUE(oobsum::PublishFir(alias, Module(), path, error));
    EXPECT_EQ(CountFiles(moved), 1);
    EXPECT_TRUE(oobsum::InspectFirInputs(alias, true).ReadyForSolve());
    ASSERT_TRUE(oobsum::MarkFirSolved(alias, error));
    std::string marker = At(real, "inputs-pending");
    ASSERT_TRUE(!llvm::sys::fs::create_link(At(dir, "missing"), marker));
    auto state = oobsum::InspectFirInputs(alias, false);
    EXPECT_TRUE(state.pending);
    EXPECT_FALSE(state.error.empty());
    EXPECT_FALSE(state.ReadyForReport());
    EXPECT_FALSE(oobsum::MarkFirSolved(alias, error));
    EXPECT_TRUE(llvm::sys::fs::is_symlink_file(marker));
    ASSERT_TRUE(!llvm::sys::fs::remove(marker));
    // 有效pending别名也不能通过unlink绕过“只移除空标记”的约束。
    std::string target = At(dir, "pending-target");
    ASSERT_TRUE(!llvm::sys::fs::create_directory(target));
    ASSERT_TRUE(Write(At(target, "evidence"), "keep"));
    ASSERT_TRUE(!llvm::sys::fs::create_link(target, marker));
    EXPECT_FALSE(oobsum::MarkFirSolved(alias, error));
    EXPECT_EQ(ReadAll(At(target, "evidence")), "keep");
    EXPECT_TRUE(llvm::sys::fs::is_symlink_file(marker));
    ASSERT_TRUE(!llvm::sys::fs::remove(At(target, "evidence")));
    ASSERT_TRUE(oobsum::MarkFirSolved(alias, error));
    EXPECT_FALSE(llvm::sys::fs::is_symlink_file(marker));
    EXPECT_TRUE(llvm::sys::fs::is_directory(target));
    llvm::sys::fs::remove_directories(dir);
}
