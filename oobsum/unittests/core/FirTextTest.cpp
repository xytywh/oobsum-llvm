//===-- FirTextTest.cpp - Tests for core/FirText ----------------*- C++ -*-===//
//
// FIR 的文本记法（SPEC-0006 §2）：§2.1 的例子逐行相同；每种类型、表达式（括号、回绕、有无符号比较）、指令、边、flags、位置。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "FirExamples.h"
#include "OobSummary/core/FirJson.h"
#include "OobSummary/core/FirText.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"

#include <string>
#include <vector>

using namespace oobsum::fir;
using namespace oobsum::fir::examples;

namespace {

std::string Join(const std::vector<std::string> &lines)
{
    std::string out;
    for (const std::string &line : lines) {
        out += line + "\n";
    }
    return out;
}

}  // namespace

TEST(FirText, SpecExample)
{
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(kSpecExample);
    ASSERT_TRUE(static_cast<bool>(v));
    Module m;
    std::string error;
    ASSERT_TRUE(ModuleFromJson(*v, m, error));
    EXPECT_EQ(Join(ModuleHeaderText(m)),
              "module tu=abe9fde904012438 file=/src/ue/ue_init.c target=aarch64_be-unknown-linux-gnu ptr=4 long=4 "
              "big-endian\n"
              "record #0 UE_TBL_S size=16 { ulId@0: u32; aucData@4: u8[12] }\n"
              "global G:g_ulUeNum: u32 def @ue_init.c:12:8\n");
    EXPECT_EQ(Join(FunctionText(m, m.funcs[0])),
              "fn F:Ue_InitOne ext -> void @ue_init.c:40:6\n"
              "  local l0 pstTbl param0: rec#0*\n"
              "  local l1 ulIdx param1: u32\n"
              "  b2 entry:\n"
              "    store4 [padd(l0, l1 * 16)] = 0  @42:5\n"
              "    -> b0\n"
              "  b0 exit:\n");
}

TEST(FirText, Everything)
{
    Module m = BuildEverything();
    EXPECT_EQ(Join(ModuleHeaderText(m)),
              "module tu=0123456789abcdef file=/a.c target=armebv7-unknown-linux-gnueabi ptr=4 long=4 big-endian\n"
              "record #5 NODE_S size=8 { lVal@0: i32; pstNext@4: rec#5* }\n"
              "global G:g_aucBuf: u8[] decl @a.h:3:15\n"
              "global G@0123456789abcdef:s_x: other12 def @a.c:4:12\n");
    EXPECT_EQ(Join(FunctionText(m, m.funcs[0])),
              "fn F:Everything ext -> void @a.c:10:6 [cfg_too_large]\n"
              "  local l0 pstNode param0: rec#5*\n"
              "  local l1 alBuf obj: i32[4]\n"
              "  local l2 tmp0 tmp: i32\n"
              "  local l9 ulLen param1: u64\n"
              "  b3 entry:\n"
              "    l2 = psub(padd(&l1, l9 * 16), &l1, 4)  @11:5\n"
              "    l2 = load4 [fld(l0, lVal@0:4)] sub  @12:5\n"
              "    store4 [padd(&l1, l9 * 16)] = ?  @13:5\n"
              "    l2 = call F:Callee(&G:g_aucBuf, str5, (i16)((l9 - -5):u32), !(l9 <u 16), -l9, ~l9, unk(float), "
              "subobj(fld(l0, lVal@0:4), 4)) #0  @a.h:2:3\n"
              "    call *(&F:Callee)() #1  @14:5\n"
              "    l2 = havoc(inline_asm)  @15:5\n"
              "    -> b1 if l9 <u 16\n"
              "    -> b0\n"
              "  b1:\n"
              "    ret  @16:5\n"
              "    -> b0\n"
              "  b0 exit:\n");
}

TEST(FirText, Parentheses)
{
    Function f;
    ExprId l1 = f.Add(Expr::LocalValue(1));
    ExprId one = f.Add(Expr::Const(1));
    ExprId four = f.Add(Expr::Const(4));
    ExprId sum = f.Add(Expr::Bin(BinOp::Add, l1, one));
    EXPECT_EQ(ExprText(f, f.Add(Expr::Bin(BinOp::Mul, sum, four))), "(l1 + 1) * 4");
    ExprId wrapped = f.Add(Expr::Bin(BinOp::Sub, l1, one, 32, false));
    EXPECT_EQ(ExprText(f, wrapped), "(l1 - 1):u32");
    EXPECT_EQ(ExprText(f, f.Add(Expr::Bin(BinOp::Shl, wrapped, four, 32, true))), "(((l1 - 1):u32) << 4):i32");
    EXPECT_EQ(ExprText(f, f.Add(Expr::Cmp(CmpOp::Ge, sum, four, true))), "(l1 + 1) >=s 4");
    EXPECT_EQ(ExprText(f, f.Add(Expr::Unary(ExprOp::Neg, sum))), "-(l1 + 1)");
    EXPECT_EQ(ExprText(f, f.Add(Expr::Cast(8, false, l1))), "(u8)l1");
    EXPECT_EQ(ExprText(f, 999), "?");  // 坏下标不崩
}

TEST(FirText, SingleBlockFunction)
{
    // 入口即出口
    Module m;
    m.file = "/x.c";
    m.files = {"/x.c"};
    Function f;
    f.key = "F@1122334455667788:helper";
    f.ret = m.Intern(Type::Int(32, true));
    f.loc = {0, 3, 12};
    Block b;
    b.id = 0;
    f.blocks = {b};
    EXPECT_EQ(Join(FunctionText(m, f)), "fn F@1122334455667788:helper -> i32 @x.c:3:12\n  b0 entry exit:\n");
}
