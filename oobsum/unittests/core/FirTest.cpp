//===-- FirTest.cpp - Tests for core/Fir ------------------------*- C++ -*-===//
//
// FIR 结构（SPEC-0005 §2.1）：类型去重、按号查找、表达式构造。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Fir.h"

using namespace oobsum::fir;

TEST(Fir, InternDeduplicates)
{
    Module m;
    TypeId u32 = m.Intern(Type::Int(32, false));
    EXPECT_EQ(m.Intern(Type::Int(32, false)), u32);
    TypeId s32 = m.Intern(Type::Int(32, true));
    EXPECT_FALSE(s32 == u32);
    TypeId p1 = m.Intern(Type::Ptr(u32));
    EXPECT_EQ(m.Intern(Type::Ptr(u32)), p1);
    EXPECT_FALSE(m.Intern(Type::Ptr(s32)) == p1);
    TypeId flex = m.Intern(Type::Arr(u32, -1));
    EXPECT_FALSE(m.Intern(Type::Arr(u32, 4)) == flex);
    EXPECT_EQ(m.Intern(Type::Void()), m.Intern(Type::Void()));
    EXPECT_FALSE(m.Intern(Type::Other(8)) == m.Intern(Type::Other(4)));
    EXPECT_EQ(m.types.size(), size_t(9));
}

TEST(Fir, FindById)
{
    // 块号沿用 Clang CFG：入口块号最大、出口块是 0，不等于下标
    Function f;
    f.blocks.resize(2);
    f.blocks[0].id = 2;
    f.blocks[1].id = 0;
    ASSERT_TRUE(f.FindBlock(0) != nullptr);
    EXPECT_TRUE(f.FindBlock(0) == &f.blocks[1]);
    EXPECT_TRUE(f.FindBlock(1) == nullptr);
    f.locals.resize(1);
    f.locals[0].id = 7;
    EXPECT_TRUE(f.FindLocal(7) == &f.locals[0]);
    EXPECT_TRUE(f.FindLocal(0) == nullptr);
    Module m;
    m.records.resize(1);
    m.records[0].id = 3;
    EXPECT_TRUE(m.FindRecord(3) == &m.records[0]);
    EXPECT_TRUE(m.FindRecord(0) == nullptr);
}

TEST(Fir, ExprBuilders)
{
    Function f;
    ExprId c = f.Add(Expr::Const(16));
    ExprId l = f.Add(Expr::LocalValue(1));
    ExprId math = f.Add(Expr::Bin(BinOp::Mul, l, c));
    EXPECT_EQ(f.exprs[math].width, 0u);
    EXPECT_FALSE(f.exprs[math].isSigned);  // 数学整数没有符号（SPEC-0005 §2.3 #1）
    ExprId wrap = f.Add(Expr::Bin(BinOp::Sub, l, c, 32, false));
    EXPECT_EQ(f.exprs[wrap].width, 32u);
    ExprId fld = f.Add(Expr::Fld(l, 4, 12, "aucData"));
    EXPECT_EQ(f.exprs[fld].value, 4);
    EXPECT_EQ(f.exprs[fld].size, 12);
    EXPECT_EQ(f.exprs[fld].text, "aucData");
    EXPECT_EQ(f.exprs.size(), size_t(5));
    EXPECT_EQ(BinOpName(BinOp::Xor), "xor");
    EXPECT_EQ(CmpOpName(CmpOp::Ge), "ge");
}
