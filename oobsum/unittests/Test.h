//===-- Test.h - Minimal unit test framework for OobSummary core -*- C++ -*-===//
//
// oobsum-core-tests 的最小测试框架（SPEC-0004；06- §9.3：不依赖 gtest）。写法与 gtest 相近：
//   TEST(Suite, Name) { EXPECT_EQ(a, b); EXPECT_TRUE(c); ASSERT_TRUE(c); }
// ASSERT_* 失败即 return（LLVM 不开异常）。两边的值用 llvm::raw_ostream 打印，枚举请先转成名字再比。
//
//===----------------------------------------------------------------------===//

#ifndef OOBSUM_UNITTESTS_TEST_H
#define OOBSUM_UNITTESTS_TEST_H

#include "llvm/Support/raw_ostream.h"

#include <string>

namespace oobsum {
namespace test {

using TestFn = void (*)();

struct Registrar {
    Registrar(const char *suite, const char *name, TestFn fn);
};

void Fail(const char *file, int line, const std::string &message);

template <typename A, typename B>
void CheckEq(const A &left, const B &right, const char *leftText, const char *rightText, const char *file, int line)
{
    if (left == right) {
        return;
    }
    std::string message;
    llvm::raw_string_ostream os(message);
    os << leftText << " == " << rightText << "\n    left:  " << left << "\n    right: " << right;
    Fail(file, line, os.str());
}

}  // namespace test
}  // namespace oobsum

#define TEST(suite, name)                                                                  \
    static void suite##_##name();                                                          \
    static ::oobsum::test::Registrar suite##_##name##_registrar(#suite, #name, suite##_##name); \
    static void suite##_##name()

#define EXPECT_EQ(a, b) ::oobsum::test::CheckEq((a), (b), #a, #b, __FILE__, __LINE__)

#define EXPECT_TRUE(c)                                                     \
    do {                                                                   \
        if (!(c)) {                                                        \
            ::oobsum::test::Fail(__FILE__, __LINE__, "expected true: " #c); \
        }                                                                  \
    } while (0)

#define EXPECT_FALSE(c)                                                     \
    do {                                                                    \
        if (c) {                                                            \
            ::oobsum::test::Fail(__FILE__, __LINE__, "expected false: " #c); \
        }                                                                   \
    } while (0)

#define ASSERT_TRUE(c)                                                     \
    do {                                                                   \
        if (!(c)) {                                                        \
            ::oobsum::test::Fail(__FILE__, __LINE__, "expected true: " #c); \
            return;                                                        \
        }                                                                  \
    } while (0)

#endif
