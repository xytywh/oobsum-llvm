//===-- LinExprTest.cpp - Tests for core/LinExpr ----------------*- C++ -*-===//
//
// 线性式（SPEC-0010）：与 replay.py 逐项同语义。金标准 data/lin-golden.txt 由 gen_lin_golden.py 从 104 份摘要生成。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/LinExpr.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"

#include <string>
#include <vector>

using namespace oobsum;

namespace {

std::string Show(llvm::StringRef text)
{
    LinExpr e;
    std::string error;
    if (!ParseLin(text, e, error)) {
        return "error: " + error;
    }
    return e.Show();
}

std::string Cond(llvm::StringRef text)
{
    std::vector<LinExpr> atoms;
    std::string error;
    if (!ParseCond(text, atoms, error)) {
        return "error: " + error;
    }
    std::string out;
    for (const LinExpr &a : atoms) {
        out += (out.empty() ? "" : " ; ") + ShowLe0(a);
    }
    return out;
}

LinExpr L(llvm::StringRef text)
{
    LinExpr e;
    std::string error;
    ParseLin(text, e, error);
    return e;
}

}  // namespace

TEST(LinExpr, Arithmetic)
{
    EXPECT_EQ(Show("16*p1 - 12"), "16*p1 - 12");
    EXPECT_EQ((L("p1 + 3") - L("p1 + 1")).Show(), "2");
    EXPECT_EQ(L("p0 - 2*i").Scale(-2).Show(), "-2*p0 + 4*i");
    EXPECT_EQ((L("4*p0") + L("-4*p0")).Show(), "0");
    EXPECT_TRUE(L("p0 + 1") == L("1 + p0"));
    EXPECT_FALSE(L("p0 + 1") == L("p0 + 2"));
    EXPECT_TRUE(L("7").IsConst());
    EXPECT_EQ(L("ext(p0) - 4*p1").Coeff(L("p1").Terms().begin()->first), -4);
}

TEST(LinExpr, CanonicalOrder)
{
    // 形参 → 形参字段 → ext → 全局 → 调用方局部 → 不透明项，常数最后；形参按下标的数值排
    EXPECT_EQ(Show("div(p0, 2) + i + g:g_x + ext(p0) + p1->ulLen + p0 + 5"),
              "p0 + p1->ulLen + ext(p0) + g:g_x + i + div(p0, 2) + 5");
    EXPECT_EQ(Show("p10 + p2"), "p2 + p10");
    EXPECT_EQ(Show("ext(p0) - p1"), "-p1 + ext(p0)");
    EXPECT_EQ(Show("128 - ulOff"), "-ulOff + 128");
}

TEST(LinExpr, PrintRules)
{
    EXPECT_EQ(Show("-p0"), "-p0");
    EXPECT_EQ(Show("0"), "0");
    EXPECT_EQ(Show("-5"), "-5");
    EXPECT_EQ(Show("3 - 2*p0"), "-2*p0 + 3");
    EXPECT_EQ(Show("p0 - 1"), "p0 - 1");
    EXPECT_EQ(Show("(p0 + 1)*4 - (2 + p1)"), "4*p0 - p1 + 2");
    EXPECT_EQ(Show("0x10 + p0"), "p0 + 16");
}

TEST(LinExpr, OpaqueFolding)
{
    // 内层是常数就当场算；div / mod 向下取整（Python 的 // 与 %）
    EXPECT_EQ(Show("div(7, 2)"), "3");
    EXPECT_EQ(Show("div(0 - 7, 2)"), "-4");
    EXPECT_EQ(Show("mod(0 - 7, 2)"), "1");
    EXPECT_EQ(Show("wrap32(0 - 1)"), "4294967295");
    EXPECT_EQ(Show("wrap16(65537)"), "1");
    EXPECT_EQ(Show("mul(3, 4)"), "12");
    // 显式的 mul(2, p0) 保持不透明；2*p0 是系数；两个非常数相乘成为 mul
    EXPECT_EQ(Show("mul(2, p0)"), "mul(2, p0)");
    EXPECT_EQ(Show("2*p0"), "2*p0");
    EXPECT_EQ(Show("p0*p1"), "mul(p0, p1)");
    EXPECT_EQ(Show("4*div(p1 + 31, 32) + div(p1 + 31, 32)"), "5*div(p1 + 31, 32)");
}

TEST(LinExpr, OverflowIsUnknown)
{
    EXPECT_TRUE((LinExpr::Const(INT64_MAX - 1) + LinExpr::Const(2)).IsUnknown());
    EXPECT_TRUE(L("4611686018427387904*p0").Scale(4).IsUnknown());
    EXPECT_EQ(Show("99999999999999999999 + p0"), "?");
    EXPECT_TRUE((L("p0") + LinExpr::Unknown()).IsUnknown());
    EXPECT_TRUE(LinExpr::Div(L("p0"), 0).IsUnknown());
}

TEST(LinExpr, ParseErrors)
{
    EXPECT_EQ(Show("p0 $ 1"), "error: unexpected character '$' at 3");
    EXPECT_EQ(Show("p0 p1"), "error: trailing 'p1' at 3");
    EXPECT_EQ(Show("(p0 + 1"), "error: expected ')' at 7 (end)");
    EXPECT_EQ(Show("div(p0, p1)"), "error: div needs a constant divisor at 11 (end)");
    EXPECT_EQ(Show("mod(p0, 0)"), "error: mod by zero at 10 (end)");
    EXPECT_EQ(Show(""), "error: expected a name at 0 (end)");
    EXPECT_EQ(Show("p0->"), "error: expected a field name after '->' at 4 (end)");
    EXPECT_EQ(Show("p0 + )"), "error: expected a name at 5 (')')");
}

TEST(LinExpr, Conditions)
{
    EXPECT_EQ(Cond("p0 <= 15"), "p0 <= 15");
    EXPECT_EQ(Cond("p0 < 16"), "p0 <= 15");
    EXPECT_EQ(Cond("p0 >= 16"), "p0 >= 16");
    EXPECT_EQ(Cond("p0 > 15"), "p0 >= 16");
    EXPECT_EQ(Cond("p0 == 3"), "p0 <= 3 ; p0 >= 3");
    EXPECT_EQ(Cond("p0->ulLen > 3"), "p0->ulLen >= 4");  // -> 里的 > 不是比较符
    EXPECT_EQ(Cond("16*p1 - 12 <= ext(p0)"), "16*p1 <= ext(p0) + 12");
    EXPECT_EQ(Cond("p1 + p3 <= 124"), "p1 + p3 <= 124");
    EXPECT_EQ(Cond("p0 != 3"), "error: not a condition");
    EXPECT_EQ(ShowLe0(L("0 - p0")), "p0 >= 0");
}

TEST(LinExpr, Substitute)
{
    Substitution m;
    m.sym["p0"] = L("i + 1");
    m.ext["p0"] = L("16 - 4*j");
    m.alias["p1"] = "p2";
    EXPECT_EQ(Substitute(L("16*p0 - 12"), m).Show(), "16*i + 4");
    EXPECT_EQ(Substitute(L("ext(p0) - p0"), m).Show(), "-i - 4*j + 15");
    EXPECT_EQ(Substitute(L("p1->ulLen + ext(p1->pucBuf)"), m).Show(), "p2->ulLen + ext(p2->pucBuf)");
    EXPECT_EQ(Substitute(L("p1 + p3"), m).Show(), "p1 + p3");  // 别名只改字段路径
    Substitution konst;
    konst.sym["p1"] = L("33");
    EXPECT_EQ(Substitute(L("4*div(p1 + 31, 32)"), konst).Show(), "8");  // 代换后内层成了常数就算出来
    EXPECT_EQ(Substitute(L("wrap32(p1 - 34)"), konst).Show(), "4294967295");
}

TEST(LinExpr, GoldenFromReplay)
{
    // 104 份摘要（去掉 doc）里的全部表达式与条件，replay.py 的规范打印结果
    auto buffer = llvm::MemoryBuffer::getFile(OOBSUM_TEST_DATA_DIR "/lin-golden.txt");
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::SmallVector<llvm::StringRef, 128> lines;
    (*buffer)->getBuffer().split(lines, '\n', -1, false);
    unsigned checked = 0;
    for (llvm::StringRef line : lines) {
        if (line.startswith("#")) {
            continue;
        }
        llvm::SmallVector<llvm::StringRef, 3> cols;
        line.split(cols, '\t');
        ASSERT_TRUE(cols.size() == 3);
        std::string got = cols[0] == "C" ? Cond(cols[1]) : Show(cols[1]);
        EXPECT_EQ(got, cols[2].str());
        ++checked;
    }
    EXPECT_TRUE(checked >= 100);
}
