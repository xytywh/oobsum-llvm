//===-- KeyTest.cpp - Tests for core/Key ------------------------*- C++ -*-===//
//
// 函数 / 全局的键（SPEC-0003 §2.1；06- D-01…D-05）。期望的哈希由 replay.py 的 xxh64 算出。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Key.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace oobsum;

TEST(Key, NormalizePathDots)
{
    EXPECT_EQ(NormalizePath("/a/./b/../c.c"), "/a/c.c");
    EXPECT_EQ(NormalizePath("/a//b/c.c"), "/a/b/c.c");
}

TEST(Key, NormalizePathRelativeBecomesAbsolute)
{
    // D-05：相对路径相对进程当前目录（compile_commands 的 directory）
    llvm::SmallString<256> cwd;
    ASSERT_TRUE(!llvm::sys::fs::current_path(cwd));
    llvm::SmallString<256> expected(cwd);
    llvm::sys::path::append(expected, "sub", "x.c");
    llvm::sys::path::native(expected, llvm::sys::path::Style::posix);
    EXPECT_EQ(NormalizePath("sub/./x.c"), std::string(expected));
    EXPECT_EQ(FileId("sub/x.c"), FileId(expected));
}

TEST(Key, FileIdMatchesPython)
{
    // python3 -c "print('%016x' % replay.xxh64(b'/a/c.c'))"
    EXPECT_EQ(FileId("/a/c.c"), "3d11a0796d4d8608");
    EXPECT_EQ(FileId("/a/b/../c.c"), "3d11a0796d4d8608");
}

TEST(Key, Spellings)
{
    EXPECT_EQ(FunctionKey("Ue_InitOne", true, ""), "F:Ue_InitOne");
    EXPECT_EQ(FunctionKey("Ue_Fill", false, "3fa1c2d4e5f60718"), "F@3fa1c2d4e5f60718:Ue_Fill");
    EXPECT_EQ(GlobalKey("g_astCellCtx", true, ""), "G:g_astCellCtx");
    EXPECT_EQ(GlobalKey("s_tbl", false, "9c02aa11bb22cc33"), "G@9c02aa11bb22cc33:s_tbl");
    EXPECT_EQ(StaticLocalKey("F:Ue_InitOne", "s_ulCnt"), "S@F:Ue_InitOne:s_ulCnt");
    EXPECT_EQ(StaticLocalKey("F@3fa1c2d4e5f60718:Ue_Fill", "s_x"), "S@F@3fa1c2d4e5f60718:Ue_Fill:s_x");
}

TEST(Key, KeyName)
{
    EXPECT_EQ(KeyName("F:Ue_InitOne"), "Ue_InitOne");
    EXPECT_EQ(KeyName("F@3fa1c2d4e5f60718:Ue_Fill"), "Ue_Fill");
    EXPECT_EQ(KeyName("F@9c02aa11bb22cc33:Pub_Min#a1b2c3d4"), "Pub_Min");
    EXPECT_EQ(KeyName("S@F:Ue_InitOne:s_ulCnt"), "s_ulCnt");
    EXPECT_EQ(KeyName("plain"), "plain");
}
