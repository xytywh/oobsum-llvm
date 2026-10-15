//===-- InterpTest.cpp - Tests for core/Interp ------------------*- C++ -*-===//
//
// 函数内解释第一步（SPEC-0014）：手工构造的 FIR 上，depth 0 的 Req 与金标准的写法相同。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Fir.h"
#include "OobSummary/core/Interp.h"
#include "OobSummary/core/Summary.h"

#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

using namespace oobsum;
using namespace oobsum::fir;

namespace {

// 小的 FIR 构造器：一个模块、一个函数，块号从 2 往下（入口 2、出口 0）
struct Builder {
    Module m;
    Function f;
    TypeId u8, u32, i32;

    Builder()
    {
        m.file = "/x/t.c";
        m.files = {"/x/t.c"};
        m.target = {"armebv7-unknown-linux-gnueabi", 4, 4, true};
        u8 = m.Intern(Type::Int(8, false));
        u32 = m.Intern(Type::Int(32, false));
        i32 = m.Intern(Type::Int(32, true));
        f.key = "F@abc:Fn";
        f.name = "Fn";
        f.ret = m.Intern(Type::Void());
    }
    uint32_t Param(const std::string &name, TypeId t)
    {
        Local l;
        l.id = static_cast<uint32_t>(f.locals.size());
        l.name = name;
        l.param = static_cast<int>(
            std::count_if(f.locals.begin(), f.locals.end(), [](const Local &x) { return x.param >= 0; }));
        l.type = t;
        f.locals.push_back(l);
        return l.id;
    }
    uint32_t LocalVar(const std::string &name, TypeId t, bool obj = false)
    {
        Local l;
        l.id = static_cast<uint32_t>(f.locals.size());
        l.name = name;
        l.type = t;
        l.obj = obj;
        f.locals.push_back(l);
        return l.id;
    }
    ExprId C(int64_t v)
    {
        return f.Add(Expr::Const(v));
    }
    ExprId L(uint32_t id)
    {
        return f.Add(Expr::LocalValue(id));
    }
    ExprId Math(BinOp op, ExprId a, ExprId b)
    {
        return f.Add(Expr::Bin(op, a, b));
    }
    ExprId Wrap(BinOp op, ExprId a, ExprId b, unsigned w = 32, bool s = false)
    {
        return f.Add(Expr::Bin(op, a, b, w, s));
    }
    ExprId PAdd(ExprId p, ExprId off)
    {
        return f.Add(Expr::PAdd(p, off));
    }
    Inst Load(uint32_t dst, ExprId addr, int64_t size, unsigned line)
    {
        Inst i;
        i.op = InstOp::Load;
        i.dst = dst;
        i.addr = addr;
        i.size = size;
        i.loc = {0, line, 5};
        return i;
    }
    Inst Store(ExprId addr, int64_t size, ExprId val, unsigned line)
    {
        Inst i;
        i.op = InstOp::Store;
        i.addr = addr;
        i.size = size;
        i.val = val;
        i.loc = {0, line, 5};
        return i;
    }
    Inst Set(uint32_t dst, ExprId val)
    {
        Inst i;
        i.op = InstOp::Set;
        i.dst = dst;
        i.val = val;
        return i;
    }
    // 一个块的函数：入口 2 → 出口 0
    void Straight(std::vector<Inst> insts)
    {
        Block b;
        b.id = 2;
        b.insts = std::move(insts);
        b.succ = {{0, kNone}};
        Block exit;
        exit.id = 0;
        f.blocks = {b, exit};
        f.entry = 2;
        f.exit = 0;
    }
    interp::FunctionFacts Run()
    {
        return interp::AnalyzeFunction(m, f);
    }
};

std::string Print(const summary::Req &r)
{
    std::string s = r.kind + " have=" + r.have + " obj=" + r.obj + " end=[" + r.endLo + ", " + r.endHi +
                    "] bound=" + r.bound + " at=" + r.access.at;
    if (r.absorbed) {
        for (const summary::Absorbed &a : *r.absorbed) {
            s += " +" + a.kind + "[" + a.endLo + ", " + a.endHi + "]";
        }
    }
    return s;
}

std::string Reqs(const interp::FunctionFacts &facts)
{
    std::string out;
    for (const summary::Req &r : facts.reqs) {
        out += (out.empty() ? "" : " | ") + Print(r);
    }
    for (const summary::Open &o : facts.opens) {
        out += (out.empty() ? "" : " | ") + ("open " + o.reason + " at=" + o.at);
    }
    return out;
}

}  // namespace

TEST(Interp, GlobalCounterIncrement)
{
    // Stat_Inc：g_aulCellStat[ulType]++ → 一条 W（读进 absorbed），have 16、end 4*p0 + 4
    Builder b;
    TypeId tbl = b.m.Intern(Type::Arr(b.u32, 4));
    b.m.globals.push_back({"G@abc:g_aulCellStat", "g_aulCellStat", true, tbl, {0, 30, 15}});
    uint32_t type = b.Param("ulType", b.u32);
    uint32_t t = b.LocalVar("t0", b.u32);
    ExprId addr = b.PAdd(b.f.Add(Expr::GAddr("G@abc:g_aulCellStat")), b.Math(BinOp::Mul, b.L(type), b.C(4)));
    b.Straight({b.Load(t, addr, 4, 37), b.Store(addr, 4, b.Wrap(BinOp::Add, b.L(t), b.C(1)), 37)});
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(facts.status, "analyzed");
    EXPECT_EQ(Reqs(facts),
              "W have=16 obj=g:g_aulCellStat end=[4*p0 + 4, 4*p0 + 4] bound=exact at=t.c:37 +R[4*p0 + 4, 4*p0 + 4]");
    ASSERT_TRUE(facts.reqs.size() == 1);
    EXPECT_EQ(facts.reqs[0].path[0], "Fn@t.c:37");
    EXPECT_EQ(facts.reqs[0].access.end, "4*p0 + 4");
    EXPECT_EQ(facts.reqs[0].start.getValueOr("-"), "4*p0");
}

TEST(Interp, ParamPointerConstIndex)
{
    // Name_Terminate：pszName[16] = 0 → have ext(p0)、对象 *p0、end 17
    Builder b;
    uint32_t name = b.Param("pszName", b.m.Intern(Type::Ptr(b.u8)));
    b.Straight({b.Store(b.PAdd(b.L(name), b.C(16)), 1, b.C(0), 29)});
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[17, 17] bound=exact at=t.c:29");
}

TEST(Interp, UnsignedSubtractionWraps)
{
    // Hist_GetPrev：aucHist[ulPos - 1] → wrap32(p1 - 1) + 1（A-26）
    Builder b;
    uint32_t hist = b.Param("aucHist", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t pos = b.Param("ulPos", b.u32);
    uint32_t t = b.LocalVar("t0", b.u8);
    b.Straight({b.Load(t, b.PAdd(b.L(hist), b.Wrap(BinOp::Sub, b.L(pos), b.C(1))), 1, 29)});
    EXPECT_EQ(Reqs(b.Run()),
              "R have=ext(p0) obj=*p0 end=[wrap32(p1 - 1) + 1, wrap32(p1 - 1) + 1] bound=exact at=t.c:29");
    // 常数的无符号减法直接取模
    Builder c;
    uint32_t p = c.Param("p", c.m.Intern(Type::Ptr(c.u8)));
    c.Straight({c.Store(c.PAdd(c.L(p), c.Wrap(BinOp::Sub, c.C(0), c.C(1))), 1, c.C(0), 3)});
    EXPECT_EQ(Reqs(c.Run()), "W have=ext(p0) obj=*p0 end=[4294967296, 4294967296] bound=exact at=t.c:3");
}

TEST(Interp, LocalArrayAndCells)
{
    // 局部数组：对象 local:名、have 为数组大小；格：存进去的下标再读出来仍是确切值
    Builder b;
    uint32_t n = b.Param("n", b.u32);
    uint32_t buf = b.LocalVar("aulBuf", b.m.Intern(Type::Arr(b.u32, 4)), true);
    uint32_t idx = b.LocalVar("aulIdx", b.m.Intern(Type::Arr(b.u32, 2)), true);
    uint32_t t = b.LocalVar("t0", b.u32);
    ExprId bufAddr = b.f.Add(Expr::LAddr(buf));
    ExprId idxAddr = b.f.Add(Expr::LAddr(idx));
    b.Straight({b.Store(idxAddr, 4, b.L(n), 10), b.Load(t, idxAddr, 4, 11),
                b.Store(b.PAdd(bufAddr, b.Math(BinOp::Mul, b.L(t), b.C(4))), 4, b.C(0), 12),
                b.Store(b.PAdd(bufAddr, b.C(20)), 4, b.C(0), 13)});
    EXPECT_EQ(Reqs(b.Run()),
              // 第 11 行读 aulIdx[0] 与第 10 行的写末端相同：读并进写（写优先，A-44）
              "W have=8 obj=local:aulIdx end=[4, 4] bound=exact at=t.c:10 +R[4, 4] | W have=16 obj=local:aulBuf "
              "end=[4*p0 + 4, 4*p0 + 4] "
              "bound=exact at=t.c:12 | W have=16 obj=local:aulBuf end=[24, 24] bound=exact at=t.c:13");
}

TEST(Interp, ParamFieldsAndMemberArray)
{
    // 形参结构体：字段读成 p0->ulLen；数组成员 fld 收窄成 p0->aucBuf（D-20、D-31）
    Builder b;
    Record r;
    r.id = 0;
    r.name = "REC_S";
    r.size = 12;
    r.fields = {{"ulLen", 0, b.u32}, {"aucBuf", 4, b.m.Intern(Type::Arr(b.u8, 8))}};
    b.m.records.push_back(r);
    uint32_t p = b.Param("pstRec", b.m.Intern(Type::Ptr(b.m.Intern(Type::Rec(0)))));
    uint32_t len = b.LocalVar("t0", b.u32);
    ExprId member = b.f.Add(Expr::Fld(b.L(p), 4, 8, "aucBuf"));
    b.Straight({b.Load(len, b.L(p), 4, 20), b.Store(b.PAdd(member, b.L(len)), 1, b.C(0), 21)});
    EXPECT_EQ(Reqs(b.Run()),
              "R have=ext(p0) obj=*p0 end=[4, 4] bound=exact at=t.c:20 | W have=8 obj=p0->aucBuf end=[p0->ulLen + 1, "
              "p0->ulLen + 1] "
              "bound=exact at=t.c:21");
}

TEST(Interp, MaskDivAndNonlinear)
{
    // 每个访问用不同的指针形参（避免互相覆盖合并），逐条看转移规则
    Builder b;
    TypeId pu8 = b.m.Intern(Type::Ptr(b.u8));
    std::vector<uint32_t> ptrs;
    for (int k = 0; k < 7; ++k) {
        ptrs.push_back(b.Param("p" + std::to_string(k), pu8));
    }
    uint32_t i = b.Param("i", b.u32);  // p7
    uint32_t j = b.Param("j", b.u32);  // p8
    auto at = [&](int k, ExprId off) {
        return b.Store(b.PAdd(b.L(ptrs[k]), off), 1, b.C(0), static_cast<unsigned>(k + 1));
    };
    b.Straight({at(0, b.Wrap(BinOp::Rem, b.L(i), b.C(8))), at(1, b.Wrap(BinOp::And, b.L(i), b.C(7))),
                at(2, b.Wrap(BinOp::And, b.L(i), b.C(6))), at(3, b.Wrap(BinOp::Div, b.L(i), b.C(4))),
                at(4, b.Wrap(BinOp::Shr, b.L(i), b.C(2))), at(5, b.Wrap(BinOp::Mul, b.L(i), b.L(j))),
                at(6, b.f.Add(Expr::Cast(8, false, b.L(i))))});
    interp::FunctionFacts facts = b.Run();
    std::vector<std::string> got;
    for (const summary::Req &r : facts.reqs) {
        got.push_back(r.obj + " [" + r.endLo + ", " + r.endHi + "] " + r.bound);
    }
    // % 8 与 & 7 都是 mod(p7, 8)；& 6 是 mask 区间；/ 4 与 >> 2 同为 div；两个变量相乘是 mul；截断到 8 位是类型范围
    std::vector<std::string> want = {"*p0 [mod(p7, 8) + 1, mod(p7, 8) + 1] exact",
                                     "*p1 [mod(p7, 8) + 1, mod(p7, 8) + 1] exact",
                                     "*p2 [1, 7] mask",
                                     "*p3 [div(p7, 4) + 1, div(p7, 4) + 1] exact",
                                     "*p4 [div(p7, 4) + 1, div(p7, 4) + 1] exact",
                                     "*p5 [mul(p7, p8) + 1, mul(p7, p8) + 1] exact",
                                     "*p6 [1, 256] type"};
    EXPECT_EQ(got.size(), want.size());
    for (size_t k = 0; k < std::min(got.size(), want.size()); ++k) {
        EXPECT_EQ(got[k], want[k]);
    }
}

TEST(Interp, JoinOfBranches)
{
    // 两支分别把下标设成 1 与 3 → 汇合后 [1, 3]（branch）；一支 p1、一支 p2 → 不可比，未知 → 账本
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t a = b.Param("a", b.u32);
    uint32_t c = b.Param("c", b.u32);
    uint32_t k = b.LocalVar("k", b.u32);
    uint32_t q = b.LocalVar("q", b.u32);
    Block entry;
    entry.id = 4;
    entry.succ = {{3, kNone}, {2, kNone}};
    Block left;
    left.id = 3;
    left.insts = {b.Set(k, b.C(1)), b.Set(q, b.L(a))};
    left.succ = {{1, kNone}};
    Block right;
    right.id = 2;
    right.insts = {b.Set(k, b.C(3)), b.Set(q, b.L(c))};
    right.succ = {{1, kNone}};
    Block join;
    join.id = 1;
    join.insts = {b.Store(b.PAdd(b.L(p), b.L(k)), 1, b.C(0), 9), b.Store(b.PAdd(b.L(p), b.L(q)), 1, b.C(0), 10)};
    join.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    b.f.blocks = {entry, left, right, join, exit};
    b.f.entry = 4;
    b.f.exit = 0;
    EXPECT_EQ(Reqs(b.Run()),
              "W have=ext(p0) obj=*p0 end=[2, 4] bound=branch at=t.c:9 | open type_range_only at=t.c:10");
}

TEST(Interp, UnknownBaseAndLoops)
{
    Builder b;
    uint32_t t = b.LocalVar("t0", b.u32);
    b.Straight({b.Load(t, b.f.Add(Expr::Unknown("float")), 4, 5)});
    EXPECT_EQ(Reqs(b.Run()), "open untracked_pointer at=t.c:5");
    // 有回边：本步不分析（S7c）
    Builder c;
    Block head;
    head.id = 2;
    head.succ = {{1, kNone}};
    Block body;
    body.id = 1;
    body.succ = {{2, kNone}, {0, kNone}};
    Block exit;
    exit.id = 0;
    c.f.blocks = {head, body, exit};
    c.f.entry = 2;
    c.f.exit = 0;
    interp::FunctionFacts facts = c.Run();
    EXPECT_EQ(facts.status, "partial:loop_pending");
    EXPECT_TRUE(facts.reqs.empty());
}
