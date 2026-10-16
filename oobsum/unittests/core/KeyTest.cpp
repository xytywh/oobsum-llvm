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

TEST(Key, GlobalSymbol)
{
    // SPEC-0086：外部链接照旧；内部链接（文件内 / 函数内 static）带键的哈希后缀，显示时去掉
    EXPECT_EQ(GlobalSymbol("G:g_ulLen", "g_ulLen"), "g:g_ulLen");
    std::string a = GlobalSymbol("G@0123456789abcdef:g_len", "g_len");
    std::string b = GlobalSymbol("G@fedcba9876543210:g_len", "g_len");
    std::string l = GlobalSymbol("S@F:Top:n", "n");
    EXPECT_TRUE(a != b);
    EXPECT_EQ(a.size(), std::string("g:g_len__S").size() + 16);
    EXPECT_EQ(a.compare(0, 10, "g:g_len__S"), 0);
    EXPECT_EQ(StripGlobalSuffix(a + " + 1"), "g:g_len + 1");
    EXPECT_EQ(StripGlobalSuffix("4*" + l + " - " + b), "4*g:n - g:g_len");
    // 不是 16 位十六进制、或后面还连着标识符字符的不动
    EXPECT_EQ(StripGlobalSuffix("g:x__Sabc + 1"), "g:x__Sabc + 1");
    EXPECT_EQ(StripGlobalSuffix("g:x__S0123456789abcdefz"), "g:x__S0123456789abcdefz");
}
