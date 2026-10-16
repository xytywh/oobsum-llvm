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

#include <functional>
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
    for (const std::string &g : r.guard) {
        s += " guard=" + g;
    }
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
    interp::FunctionFacts facts = b.Run();
    // aulIdx[0] 的写与读（末端 4 ≤ 8）当场证明安全，不生成 Req（A-43 / A-44，SPEC-0016 §2.2）；常数越界的照常生成
    EXPECT_EQ(Reqs(facts),
              "W have=16 obj=local:aulBuf end=[4*p0 + 4, 4*p0 + 4] bound=exact at=t.c:12 | "
              "W have=16 obj=local:aulBuf end=[24, 24] bound=exact at=t.c:13");
    ASSERT_TRUE(facts.safe.size() == 2);
    EXPECT_EQ(Print(facts.safe[0]), "W have=8 obj=local:aulIdx end=[4, 4] bound=exact at=t.c:10");
    EXPECT_EQ(Print(facts.safe[1]), "R have=8 obj=local:aulIdx end=[4, 4] bound=exact at=t.c:11");
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
    // 成员数组的写同时查外层对象：4 + p0->ulLen + 1 <= ext(p0)（SPEC-0022；LV7-03 的"按结构体访问小缓冲区"靠它）
    EXPECT_EQ(Reqs(b.Run()),
              "R have=ext(p0) obj=*p0 end=[4, 4] bound=exact at=t.c:20 | "
              "W have=ext(p0) obj=*p0 end=[p0->ulLen + 5, p0->ulLen + 5] bound=exact at=t.c:21 | "
              "W have=8 obj=p0->aucBuf end=[p0->ulLen + 1, p0->ulLen + 1] bound=exact at=t.c:21");
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
    // 有回边：S7c 起照常分析（SPEC-0017；这里没有访问）
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
    EXPECT_EQ(facts.status, "analyzed");
    EXPECT_TRUE(facts.reqs.empty());
}

TEST(Interp, CallRecords)
{
    // 调用点记录（SPEC-0015 §2.1）：确切整数、纯函数模型的指针 → null、指针的 have / alias、偏移、局部数组、未知
    Builder b;
    uint32_t p = b.Param("pucBuf", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulLen", b.u32);
    uint32_t buf = b.LocalVar("aucTmp", b.m.Intern(Type::Arr(b.u8, 16)), true);
    uint32_t t = b.LocalVar("t0", b.u32);
    auto call = [&](const std::string &callee, std::vector<ExprId> args, unsigned line) {
        Inst i;
        i.op = InstOp::Call;
        i.callee = callee;
        i.args = std::move(args);
        i.loc = {0, line, 5};
        return i;
    };
    auto havoc = [&](uint32_t dst, unsigned line) {
        Inst i;
        i.op = InstOp::Havoc;
        i.dst = dst;
        i.why = "inline_asm";
        i.loc = {0, line, 5};
        return i;
    };
    b.Straight({call("F:LOG_Print", {b.C(3), b.f.Add(Expr::Str(4)), b.L(n)}, 10),
                call("F@abc:Leaf", {b.L(p), b.PAdd(b.L(p), b.C(4)), b.f.Add(Expr::LAddr(buf)), b.L(n)}, 11),
                b.Load(t, b.f.Add(Expr::Unknown("float")), 4, 12), call("F@abc:Leaf", {b.L(t)}, 13), havoc(t, 14),
                call("F@abc:Leaf", {b.L(t)}, 15)});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 4);
    auto show = [](const summary::ArgValue &a) {
        switch (a.kind) {
            case summary::ArgValue::Kind::Null:
                return std::string("null");
            case summary::ArgValue::Kind::Expr:
                return a.expr;
            case summary::ArgValue::Kind::Ptr:
                return "{" + a.ptr->obj + ", " + a.ptr->have + (a.ptr->alias ? ", alias " + *a.ptr->alias : "") + "}";
        }
        return std::string("?");
    };
    auto args = [&](const summary::Call &c) {
        std::string out;
        for (const summary::ArgValue &a : c.args) {
            out += (out.empty() ? "" : " ") + show(a);
        }
        return out;
    };
    EXPECT_EQ(facts.calls[0].callee, "LOG_Print");
    EXPECT_EQ(facts.calls[0].model.getValueOr("-"), "pure");
    EXPECT_EQ(args(facts.calls[0]), "3 null p1");
    EXPECT_EQ(facts.calls[1].site, "t.c:11");
    EXPECT_EQ(facts.calls[1].callee, "Leaf");
    EXPECT_FALSE(facts.calls[1].model.hasValue());
    EXPECT_EQ(args(facts.calls[1]), "{*p0, ext(p0), alias p0} {*p0, ext(p0) - 4} {local:aucTmp, 16} p1");
    // 从未知地址读到的 u32：类型范围（data）→ 局部名 + locals（data 不是代码给出的界，调用点上只会记账）
    EXPECT_EQ(args(facts.calls[2]), "t0");
    ASSERT_TRUE(facts.calls[2].locals.hasValue());
    EXPECT_EQ(facts.calls[2].locals->at("t0").hi, "4294967295");
    EXPECT_EQ(facts.calls[2].locals->at("t0").bound, "data");
    // 完全未知（havoc）的实参写成非接口符号（只会走向 entry_unproven，不写 null）
    EXPECT_EQ(args(facts.calls[3]), "_unknown0");
}

// ---- 条件边与守卫（SPEC-0016）----

// 菱形：入口（带条件的两条边）→ 真 / 假 → 汇合 → 出口。块号：入口 5、真 4、假 3、汇合 1、出口 0
struct Diamond {
    Block entry, yes, no, join, exit;
    Diamond()
    {
        entry.id = 5;
        yes.id = 4;
        no.id = 3;
        join.id = 1;
        exit.id = 0;
        yes.succ = {{1, kNone}};
        no.succ = {{1, kNone}};
        join.succ = {{0, kNone}};
    }
    void Install(Builder &b, ExprId cond, ExprId negated)
    {
        entry.succ = {{4, cond}, {3, negated}};
        b.f.blocks = {entry, yes, no, join, exit};
        b.f.entry = 5;
        b.f.exit = 0;
    }
};

TEST(Interp, GuardFromEarlyReturn)
{
    // LV2-09 Hist_GetPrevSafe：if (n == 0) return; p[n − 1] → 守卫 p1 >= 1、n − 1 不回绕、末端 p1
    Builder b;
    uint32_t p = b.Param("pucHist", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulPos", b.u32);
    uint32_t t = b.LocalVar("t", b.u8);
    Diamond d;
    d.yes.succ = {{0, kNone}};  // return 0
    d.no.insts = {b.Load(t, b.PAdd(b.L(p), b.Wrap(BinOp::Sub, b.L(n), b.C(1))), 1, 37)};
    ExprId eq = b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(n), b.C(0), false));
    ExprId ne = b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(n), b.C(0), false));
    d.Install(b, eq, ne);
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(Reqs(facts), "R have=ext(p0) obj=*p0 end=[p1, p1] bound=exact at=t.c:37 guard=p1 >= 1");
    ASSERT_TRUE(facts.reqs.size() == 1);
    EXPECT_EQ(facts.reqs[0].start.getValueOr("-"), "p1 - 1");
}

TEST(Interp, GuardFromRangeCheck)
{
    // if (i >= 32) return; p[24*i + 4] = …（LV3-03 Port_SetMode）→ 守卫 p1 <= 31；!(i < 16) 写成 LNot
    Builder b;
    uint32_t p = b.Param("astPort", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t i = b.Param("ulPortId", b.u32);
    Diamond d;
    d.yes.succ = {{0, kNone}};
    d.no.insts = {
        b.Store(b.PAdd(b.L(p), b.Math(BinOp::Add, b.Math(BinOp::Mul, b.L(i), b.C(24)), b.C(4))), 4, b.C(0), 20)};
    ExprId ge = b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.C(32), false));
    d.Install(b, ge, b.f.Add(Expr::Unary(ExprOp::LNot, ge)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[24*p1 + 8, 24*p1 + 8] bound=exact at=t.c:20 guard=p1 <= 31");
}

TEST(Interp, GuardsUnreachableAndUnfaithful)
{
    // 两边常数且为假 → 边不可达（访问不产出）；n != 5 推不出线性原子；off + len 可能回绕 → 不当守卫
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t off = b.Param("off", b.u32);
    uint32_t len = b.Param("len", b.u32);
    uint32_t k = b.LocalVar("k", b.u32);
    Block entry;
    entry.id = 6;
    entry.insts = {b.Set(k, b.C(3))};
    ExprId never = b.f.Add(Expr::Cmp(CmpOp::Gt, b.L(k), b.C(10), false));
    ExprId always = b.f.Add(Expr::Cmp(CmpOp::Le, b.L(k), b.C(10), false));
    entry.succ = {{5, never}, {4, always}};
    Block dead;
    dead.id = 5;
    dead.insts = {b.Store(b.PAdd(b.L(p), b.C(100)), 1, b.C(0), 10)};
    dead.succ = {{0, kNone}};
    Block live;
    live.id = 4;
    ExprId ne = b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(off), b.C(5), false));
    live.succ = {{3, ne}, {0, b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(off), b.C(5), false))}};
    Block checked;
    checked.id = 3;
    ExprId sum = b.Wrap(BinOp::Add, b.L(off), b.L(len));
    ExprId fits = b.f.Add(Expr::Cmp(CmpOp::Le, sum, b.C(64), false));
    checked.succ = {{2, fits}, {0, b.f.Add(Expr::Cmp(CmpOp::Gt, sum, b.C(64), false))}};
    Block body;
    body.id = 2;
    body.insts = {b.Store(b.PAdd(b.L(p), b.L(off)), 1, b.C(0), 12)};
    body.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    b.f.blocks = {entry, dead, live, checked, body, exit};
    b.f.entry = 6;
    b.f.exit = 0;
    std::vector<std::string> trace;
    interp::FunctionFacts facts = interp::AnalyzeFunction(b.m, b.f, &trace);
    EXPECT_EQ(Reqs(facts), "W have=ext(p0) obj=*p0 end=[p1 + 1, p1 + 1] bound=exact at=t.c:12");
    bool deadSeen = false;
    for (const std::string &line : trace) {
        deadSeen = deadSeen || line.rfind("b5:", 0) == 0;
    }
    EXPECT_FALSE(deadSeen);
}

TEST(Interp, GuardsJoinMergeAndCalls)
{
    // 一支有守卫 p1 <= 15、一支没有 → 汇合后没有守卫；守卫不同的两次访问不合并；调用点带守卫
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t i = b.Param("i", b.u32);
    Diamond d;
    ExprId lt = b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(i), b.C(16), false));
    ExprId ge = b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.C(16), false));
    Inst call;
    call.op = InstOp::Call;
    call.callee = "F@abc:Leaf";
    call.args = {b.L(i)};
    call.loc = {0, 8, 5};
    d.yes.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 7), call};
    d.join.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 9)};
    d.Install(b, lt, ge);
    std::vector<std::string> trace;
    interp::FunctionFacts facts = interp::AnalyzeFunction(b.m, b.f, &trace);
    EXPECT_EQ(Reqs(facts),
              "W have=ext(p0) obj=*p0 end=[p1 + 1, p1 + 1] bound=exact at=t.c:7 guard=p1 <= 15 | "
              "W have=ext(p0) obj=*p0 end=[p1 + 1, p1 + 1] bound=exact at=t.c:9");
    ASSERT_TRUE(facts.calls.size() == 1);
    ASSERT_TRUE(facts.calls[0].guards.hasValue());
    ASSERT_TRUE(facts.calls[0].guards->size() == 1);
    EXPECT_EQ((*facts.calls[0].guards)[0].cond, "p1 <= 15");
    EXPECT_EQ((*facts.calls[0].guards)[0].bound, "check");
    std::string joined;
    for (const std::string &line : trace) {
        if (line.rfind("b1:", 0) == 0) {
            joined = line;
        }
        if (line.rfind("b4:", 0) == 0) {
            EXPECT_EQ(line, "b4: p=*p0+0 i=p1 guards: p1 <= 15");
        }
    }
    EXPECT_EQ(joined, "b1: p=*p0+0 i=p1");
}

TEST(Interp, NarrowRangeLocal)
{
    // 从未知内存读到的 u32（data）：if (k < 16) → [0, 15]（check），p[k] 末端 16；k == 3 → 确切值 3；
    // k 是 [0, 15] 时 k > 20 的边不可达
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t q = b.Param("q", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t k = b.LocalVar("k", b.u32);
    Block entry;
    entry.id = 6;
    entry.insts = {b.Load(k, b.f.Add(Expr::Unknown("float")), 4, 3)};
    entry.succ = {{5, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(k), b.C(16), false))},
                  {0, b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(k), b.C(16), false))}};
    Block small;
    small.id = 5;
    small.insts = {b.Store(b.PAdd(b.L(p), b.L(k)), 1, b.C(0), 5)};
    small.succ = {{4, b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(k), b.C(3), false))},
                  {3, b.f.Add(Expr::Cmp(CmpOp::Gt, b.L(k), b.C(20), false))}};
    Block three;
    three.id = 4;
    three.insts = {b.Store(b.PAdd(b.L(q), b.Math(BinOp::Add, b.L(k), b.C(20))), 1, b.C(0), 6)};
    three.succ = {{0, kNone}};
    Block dead;
    dead.id = 3;
    dead.insts = {b.Store(b.PAdd(b.L(p), b.C(1000)), 1, b.C(0), 7)};
    dead.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    b.f.blocks = {entry, small, three, dead, exit};
    b.f.entry = 6;
    b.f.exit = 0;
    EXPECT_EQ(Reqs(b.Run()),
              "W have=ext(p0) obj=*p0 end=[1, 16] bound=check at=t.c:5 | "
              "W have=ext(p1) obj=*p1 end=[24, 24] bound=exact at=t.c:6 | open untracked_pointer at=t.c:3");
}

// ---- 循环（SPEC-0017）----

// 入口 5（初始化）→ 头 4（条件）→ 体 3 → 步进 2 → 头；头的假边 → 出口 0
struct Loop {
    Block entry, head, body, latch, exit;
    Loop()
    {
        entry.id = 5;
        head.id = 4;
        body.id = 3;
        latch.id = 2;
        exit.id = 0;
        entry.succ = {{4, kNone}};
        body.succ = {{2, kNone}};
        latch.succ = {{4, kNone}};
    }
    void Install(Builder &b, ExprId cond, ExprId negated)
    {
        head.succ = {{3, cond}, {0, negated}};
        b.f.blocks = {entry, head, body, latch, exit};
        b.f.entry = 5;
        b.f.exit = 0;
    }
};

// for (i = 0; i < bound; i++) p[i] = 0;（u8，第 7 行）
std::string ForLoop(CmpOp op, std::function<ExprId(Builder &, uint32_t)> bound,
                    std::vector<std::string> *trace = nullptr)
{
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("n", b.u32);
    uint32_t i = b.LocalVar("i", b.u32);
    Loop l;
    l.entry.insts = {b.Set(i, b.C(0))};
    l.body.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(i, b.Wrap(BinOp::Add, b.L(i), b.C(1)))};
    static const CmpOp negate[] = {CmpOp::Ne, CmpOp::Eq, CmpOp::Ge, CmpOp::Gt, CmpOp::Le, CmpOp::Lt};
    ExprId x = bound(b, n);
    l.Install(b, b.f.Add(Expr::Cmp(op, b.L(i), x, false)),
              b.f.Add(Expr::Cmp(negate[static_cast<int>(op)], b.L(i), x, false)));
    return Reqs(interp::AnalyzeFunction(b.m, b.f, trace));
}

TEST(Interp, LoopIndexBound)
{
    // i < n → i ∈ [0, p1 − 1]（loop），末端 [1, p1]；循环体执行过 → 守卫 p1 >= 1
    std::vector<std::string> trace;
    EXPECT_EQ(ForLoop(
                  CmpOp::Lt, [](Builder &b, uint32_t n) { return b.L(n); }, &trace),
              "W have=ext(p0) obj=*p0 end=[1, p1] bound=loop at=t.c:7 guard=p1 >= 1");
    bool head = false;
    for (const std::string &line : trace) {
        if (line.rfind("b4:", 0) == 0) {
            head = true;
            // 汇合 0 与 [1, p1]：p1 >= 0（无符号）→ 上界取 p1（SPEC-0023）
            EXPECT_EQ(line, "b4: p=*p0+0 n=p1 i=[0, p1]:loop (head)");
        }
        if (line.rfind("b3:", 0) == 0) {
            EXPECT_EQ(line, "b3: p=*p0+0 n=p1 i=[0, p1 - 1]:loop guards: p1 >= 1");
        }
    }
    EXPECT_TRUE(head);
    // i <= n / 32（LV2-05 Map_ClearWords）：除以常数保留；div(p1, 32) >= 0 本来就成立，不加守卫
    EXPECT_EQ(ForLoop(CmpOp::Le, [](Builder &b, uint32_t n) { return b.Wrap(BinOp::Div, b.L(n), b.C(32)); }),
              "W have=ext(p0) obj=*p0 end=[1, div(p1, 32) + 1] bound=loop at=t.c:7");
    // i < (n + 31) / 32（Map_ClearWords_OK）：n + 31 可能回绕，但它是"大的一边"、回绕只会变小 → 可用（SPEC-0017 §2.4）
    EXPECT_EQ(
        ForLoop(CmpOp::Lt, [](Builder &b,
                              uint32_t n) { return b.Wrap(BinOp::Div, b.Wrap(BinOp::Add, b.L(n), b.C(31)), b.C(32)); }),
        "W have=ext(p0) obj=*p0 end=[1, div(p1 + 31, 32)] bound=loop at=t.c:7 guard=div(p1 + 31, 32) >= 1");
    // i < n - 1：n − 1 可能回绕成很大（无符号减法），是 wrap32(p1 − 1)：忠实（wrap 按定义取模）
    EXPECT_EQ(ForLoop(CmpOp::Lt, [](Builder &b, uint32_t n) { return b.Wrap(BinOp::Sub, b.L(n), b.C(1)); }),
              "W have=ext(p0) obj=*p0 end=[1, wrap32(p1 - 1)] bound=loop at=t.c:7 guard=wrap32(p1 - 1) >= 1");
}

TEST(Interp, LoopSmallSideMayWrap)
{
    // i + 8 <= n：小的一边 i + 8 可能回绕（i 的区间加 8 超过类型最大值）→ 不收窄，访问没有上界 → 账本
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("n", b.u32);
    uint32_t i = b.LocalVar("i", b.u32);
    Loop l;
    l.entry.insts = {b.Set(i, b.C(0))};
    l.body.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(i, b.Wrap(BinOp::Add, b.L(i), b.C(1)))};
    ExprId sum = b.Wrap(BinOp::Add, b.L(i), b.C(8));
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Le, sum, b.L(n), false)), b.f.Add(Expr::Cmp(CmpOp::Gt, sum, b.L(n), false)));
    EXPECT_EQ(Reqs(b.Run()), "open type_range_only at=t.c:7");
}

TEST(Interp, LoopConstantBoundIsSafe)
{
    // for (i = 0; i < 4; i++) a[i] = 0，a 是 4 字节的局部数组：下降后 i ∈ [0, 3]，末端 4 ≤ 4 → 当场证明安全
    Builder b;
    uint32_t a = b.LocalVar("aucBuf", b.m.Intern(Type::Arr(b.u8, 4)), true);
    uint32_t i = b.LocalVar("i", b.u32);
    Loop l;
    l.entry.insts = {b.Set(i, b.C(0))};
    l.body.insts = {b.Store(b.PAdd(b.f.Add(Expr::LAddr(a)), b.L(i)), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(i, b.Wrap(BinOp::Add, b.L(i), b.C(1)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(i), b.C(4), false)),
              b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.C(4), false)));
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(Reqs(facts), "");
    ASSERT_TRUE(facts.safe.size() == 1);
    EXPECT_EQ(Print(facts.safe[0]), "W have=4 obj=local:aucBuf end=[1, 4] bound=loop at=t.c:7");
    // i <= 4：末端 5 > 4，照常生成 Req（常数越界由求解判，SPEC-0016 §2.2）
    Builder c;
    uint32_t a2 = c.LocalVar("aucBuf", c.m.Intern(Type::Arr(c.u8, 4)), true);
    uint32_t i2 = c.LocalVar("i", c.u32);
    Loop l2;
    l2.entry.insts = {c.Set(i2, c.C(0))};
    l2.body.insts = {c.Store(c.PAdd(c.f.Add(Expr::LAddr(a2)), c.L(i2)), 1, c.C(0), 7)};
    l2.latch.insts = {c.Set(i2, c.Wrap(BinOp::Add, c.L(i2), c.C(1)))};
    l2.Install(c, c.f.Add(Expr::Cmp(CmpOp::Le, c.L(i2), c.C(4), false)),
               c.f.Add(Expr::Cmp(CmpOp::Gt, c.L(i2), c.C(4), false)));
    EXPECT_EQ(Reqs(c.Run()), "W have=4 obj=local:aucBuf end=[1, 5] bound=loop at=t.c:7");
}

TEST(Interp, LoopPointerStepping)
{
    // while (n-- > 0) *p++ = *q++;（LV7-04 Buf_CopyStep）：偏移 widening 到 [0, +inf) → pointer_stepping，同一行读写只记一条
    Builder b;
    uint32_t p = b.Param("pucOut", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t q = b.Param("pucIn", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulCnt", b.u32);
    uint32_t t = b.LocalVar("t0", b.u32);
    uint32_t x = b.LocalVar("t1", b.u8);
    Loop l;
    l.entry.insts = {};
    l.head.insts = {b.Set(t, b.L(n)), b.Set(n, b.Wrap(BinOp::Sub, b.L(n), b.C(1)))};
    l.body.insts = {b.Load(x, b.L(q), 1, 27), b.Store(b.L(p), 1, b.L(x), 27), b.Set(p, b.PAdd(b.L(p), b.C(1))),
                    b.Set(q, b.PAdd(b.L(q), b.C(1)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Gt, b.L(t), b.C(0), false)),
              b.f.Add(Expr::Cmp(CmpOp::Le, b.L(t), b.C(0), false)));
    EXPECT_EQ(Reqs(b.Run()), "open pointer_stepping at=t.c:27");
}

TEST(Interp, LoopCallArgument)
{
    // for (i = 0; i < n; i++) Ue_InitOne(p, i)（LV4-01 Ue_InitTbl）：实参 i 的区间进 locals（loop），调用点带守卫 p1 >= 1
    Builder b;
    uint32_t p = b.Param("pstTbl", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulNum", b.u32);
    uint32_t i = b.LocalVar("i", b.u32);
    Loop l;
    Inst call;
    call.op = InstOp::Call;
    call.callee = "F@abc:Ue_InitOne";
    call.args = {b.L(p), b.L(i)};
    call.loc = {0, 59, 9};
    l.entry.insts = {b.Set(i, b.C(0))};
    l.body.insts = {call};
    l.latch.insts = {b.Set(i, b.Wrap(BinOp::Add, b.L(i), b.C(1)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(i), b.L(n), false)),
              b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.L(n), false)));
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 1);
    const summary::Call &c = facts.calls[0];
    EXPECT_EQ(c.args[1].expr, "i");
    ASSERT_TRUE(c.locals.hasValue());
    EXPECT_EQ(c.locals->at("i").lo, "0");
    EXPECT_EQ(c.locals->at("i").hi, "p1 - 1");
    EXPECT_EQ(c.locals->at("i").bound, "loop");
    ASSERT_TRUE(c.guards.hasValue());
    EXPECT_EQ((*c.guards)[0].cond, "p1 >= 1");
}

// ---- 模型表（SPEC-0018）----

Inst CallOf(const std::string &callee, std::vector<ExprId> args, unsigned line, uint32_t dst = kNone)
{
    Inst i;
    i.op = InstOp::Call;
    i.callee = callee;
    i.args = std::move(args);
    i.dst = dst;
    i.loc = {0, line, 5};
    return i;
}

TEST(Interp, MemcpySecurec)
{
    // LV2-03 Tlv_WriteVal：memcpy_s(pucPkt + ulOff, 128, pucVal, ulValLen) → 目的写 128 字节；源 ulValLen <= 128 时读
    Builder b;
    uint32_t p = b.Param("pucPkt", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t off = b.Param("ulOff", b.u32);
    uint32_t v = b.Param("pucVal", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulValLen", b.u32);
    b.Straight({CallOf("F:memcpy_s", {b.PAdd(b.L(p), b.L(off)), b.C(128), b.L(v), b.L(n)}, 31)});
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(Reqs(facts),
              "W have=ext(p0) obj=*p0 end=[p1 + 128, p1 + 128] bound=exact at=t.c:31 | "
              "R have=ext(p2) obj=*p2 end=[p3, p3] bound=exact at=t.c:31 guard=p3 <= 128");
    ASSERT_TRUE(facts.calls.size() == 1);
    EXPECT_EQ(facts.calls[0].model.getValueOr("-"), "access");
    // Tlv_WriteVal_OK：destMax = 128 − ulOff 可能回绕 → 守卫 p1 <= 128，目的末端 p1 + (128 − p1) = 128；源 p1 + p3 <= 128
    Builder c;
    uint32_t p2 = c.Param("pucPkt", c.m.Intern(Type::Ptr(c.u8)));
    uint32_t off2 = c.Param("ulOff", c.u32);
    uint32_t v2 = c.Param("pucVal", c.m.Intern(Type::Ptr(c.u8)));
    uint32_t n2 = c.Param("ulValLen", c.u32);
    c.Straight({CallOf("F:memcpy_s",
                       {c.PAdd(c.L(p2), c.L(off2)), c.Wrap(BinOp::Sub, c.C(128), c.L(off2)), c.L(v2), c.L(n2)}, 36)});
    // 第二段（SPEC-0020）：ulOff >= 2^31 + 129 时 wrap32(128 − ulOff) = 2^32 + 128 − ulOff 不超过 0x7fffffff，照样写；
    // 模型给出的段守卫总挂上（第一段的源上 p1 <= 128 被 p1 + p3 <= 128 蕴含，不挂）
    EXPECT_EQ(Reqs(c.Run()),
              "W have=ext(p0) obj=*p0 end=[128, 128] bound=exact at=t.c:36 guard=p1 <= 128 | "
              "R have=ext(p2) obj=*p2 end=[p3, p3] bound=exact at=t.c:36 guard=p1 + p3 <= 128 | "
              "W have=ext(p0) obj=*p0 end=[4294967424, 4294967424] bound=exact at=t.c:36 "
              "guard=p1 >= 2147483777 | "
              "R have=ext(p2) obj=*p2 end=[p3, p3] bound=exact at=t.c:36 guard=p1 + p3 <= 4294967424 "
              "guard=p1 >= 2147483777");
    // wrap16：E = 0x8000 − n，回绕后最多 65535，从不超过上限 → 第二段只要 E <= −1
    Builder w;
    uint32_t p3 = w.Param("p", w.m.Intern(Type::Ptr(w.u8)));
    uint32_t n3 = w.Param("usLen", w.m.Intern(Type::Int(16, false)));
    w.Straight({CallOf("F:memset_s", {w.L(p3), w.Wrap(BinOp::Sub, w.C(0x8000), w.L(n3), 16), w.C(0), w.C(1)}, 9)});
    EXPECT_EQ(Reqs(w.Run()),
              "W have=ext(p0) obj=*p0 end=[-p1 + 32768, -p1 + 32768] bound=exact at=t.c:9 guard=p1 <= 32768 | "
              "W have=ext(p0) obj=*p0 end=[-p1 + 98304, -p1 + 98304] bound=exact at=t.c:9 guard=p1 >= 32769");
}

TEST(Interp, SecurecNoAccess)
{
    // destMax 为 0 或大于 0x7fffffff：securec 直接返回；count > destMax（常数）：只清零目的、不读源；memset_s 只有目的
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t q = b.Param("q", b.m.Intern(Type::Ptr(b.u8)));
    b.Straight({CallOf("F:memcpy_s", {b.L(p), b.C(0), b.L(q), b.C(4)}, 3),
                CallOf("F:memcpy_s", {b.L(p), b.C(0x80000000LL), b.L(q), b.C(4)}, 4),
                CallOf("F:memcpy_s", {b.L(p), b.C(8), b.L(q), b.C(16)}, 5),
                CallOf("F:memset_s", {b.L(q), b.C(32), b.C(0), b.C(32)}, 6)});
    EXPECT_EQ(Reqs(b.Run()),
              "W have=ext(p0) obj=*p0 end=[8, 8] bound=exact at=t.c:5 | "
              "W have=ext(p1) obj=*p1 end=[32, 32] bound=exact at=t.c:6");
}

TEST(Interp, LibcAndAlloc)
{
    // memcpy(p, q, n)：两边各 n 字节（F-05）；VOS_MemAlloc(.., .., 64) 的返回值是 64 字节的堆对象；函数指针实参写 null
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t q = b.Param("q", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("n", b.u32);
    uint32_t h = b.LocalVar("pucHeap", b.m.Intern(Type::Ptr(b.u8)));
    b.Straight({CallOf("F:memcpy", {b.L(p), b.L(q), b.L(n)}, 3),
                CallOf("F:VOS_MemAlloc", {b.C(1), b.C(0), b.C(64)}, 4, h),
                b.Store(b.PAdd(b.L(h), b.C(70)), 1, b.C(0), 5),
                CallOf("F:OM_RegMsg", {b.C(1), b.f.Add(Expr::FAddr("F:Cb"))}, 6)});
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(Reqs(facts),
              "W have=ext(p0) obj=*p0 end=[p2, p2] bound=exact at=t.c:3 | "
              "R have=ext(p1) obj=*p1 end=[p2, p2] bound=exact at=t.c:3 | "
              "W have=64 obj=heap@t.c:4 end=[71, 71] bound=exact at=t.c:5");
    ASSERT_TRUE(facts.calls.size() == 3);
    EXPECT_EQ(facts.calls[1].model.getValueOr("-"), "alloc");
    EXPECT_FALSE(facts.calls[2].model.hasValue());  // 未知被调者在链接时判（S8c）
    EXPECT_TRUE(facts.calls[2].args[1].kind == summary::ArgValue::Kind::Null);
}

TEST(Interp, LoopExitEquality)
{
    // LV3-05 Name_Pad：for (i = 0; i < n; i++) p[i] = ' '; p[i] = 0;——循环头 [0, p1]，出口 i >= p1 → i == p1，循环后的写末端 p1 + 1（SPEC-0023）
    Builder b;
    uint32_t p = b.Param("pszName", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("ulLen", b.u32);
    uint32_t i = b.LocalVar("i", b.u32);
    Loop l;
    l.entry.insts = {b.Set(i, b.C(0))};
    l.body.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(32), 28)};
    l.latch.insts = {b.Set(i, b.Wrap(BinOp::Add, b.L(i), b.C(1)))};
    l.exit.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 30)};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(i), b.L(n), false)),
              b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.L(n), false)));
    std::vector<std::string> trace;
    interp::FunctionFacts facts = interp::AnalyzeFunction(b.m, b.f, &trace);
    EXPECT_EQ(Reqs(facts),
              "W have=ext(p0) obj=*p0 end=[1, p1] bound=loop at=t.c:28 guard=p1 >= 1 | "
              "W have=ext(p0) obj=*p0 end=[p1 + 1, p1 + 1] bound=exact at=t.c:30");
    bool exitSeen = false;
    for (const std::string &line : trace) {
        if (line.rfind("b0:", 0) == 0) {
            exitSeen = true;
            EXPECT_EQ(line, "b0: pszName=*p0+0 ulLen=p1 i=p1");
        }
    }
    EXPECT_TRUE(exitSeen);
}

TEST(Interp, RetNewEffect)
{
    // 返回模型分配器的结果 → ret_new（size 用本函数的接口符号写，eff_depth 1，SPEC-0024 §2.1）
    auto ret = [](ExprId v) {
        Inst i;
        i.op = InstOp::Ret;
        i.val = v;
        return i;
    };
    Builder b;
    uint32_t n = b.Param("ulSize", b.u32);
    uint32_t p = b.LocalVar("p", b.m.Intern(Type::Ptr(b.u8)));
    b.Straight({CallOf("F:VOS_MemAlloc", {b.C(1), b.C(0), b.L(n)}, 3, p), ret(b.L(p))});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.effs.size() == 1);
    EXPECT_EQ(facts.effs[0].kind, "ret_new");
    EXPECT_EQ(facts.effs[0].size.getValueOr("-"), "p0");
    EXPECT_EQ(facts.effs[0].effDepth.getValueOr(0), 1);
    EXPECT_TRUE(facts.effs[0].maybeNull.getValueOr(false));
    // 一支返回空指针、一支返回堆对象：仍是 ret_new
    Builder c;
    uint32_t n2 = c.Param("ulSize", c.u32);
    uint32_t p2 = c.LocalVar("p", c.m.Intern(Type::Ptr(c.u8)));
    Diamond d;
    d.entry.insts = {CallOf("F:malloc", {c.L(n2)}, 3, p2)};
    d.yes.insts = {ret(c.C(0))};
    d.yes.succ = {{0, kNone}};
    d.no.insts = {ret(c.L(p2))};
    d.no.succ = {{0, kNone}};
    d.Install(c, c.f.Add(Expr::Cmp(CmpOp::Eq, c.L(n2), c.C(0), false)),
              c.f.Add(Expr::Cmp(CmpOp::Ne, c.L(n2), c.C(0), false)));
    interp::FunctionFacts facts2 = c.Run();
    ASSERT_TRUE(facts2.effs.size() == 1);
    EXPECT_EQ(facts2.effs[0].size.getValueOr("-"), "p0");
    // 两支返回不同的对象：没有 ret_new
    Builder e;
    uint32_t q1 = e.LocalVar("q1", e.m.Intern(Type::Ptr(e.u8)));
    uint32_t q2 = e.LocalVar("q2", e.m.Intern(Type::Ptr(e.u8)));
    uint32_t k = e.Param("k", e.u32);
    Diamond d2;
    d2.entry.insts = {CallOf("F:malloc", {e.C(4)}, 3, q1), CallOf("F:malloc", {e.C(8)}, 4, q2)};
    d2.yes.insts = {ret(e.L(q1))};
    d2.yes.succ = {{0, kNone}};
    d2.no.insts = {ret(e.L(q2))};
    d2.no.succ = {{0, kNone}};
    d2.Install(e, e.f.Add(Expr::Cmp(CmpOp::Eq, e.L(k), e.C(0), false)),
               e.f.Add(Expr::Cmp(CmpOp::Ne, e.L(k), e.C(0), false)));
    EXPECT_TRUE(e.Run().effs.empty());
}

TEST(Interp, RetCondEffect)
{
    // ?: 的校验函数（LV4-06 Cell_IsValidId）：两支是不同的常数，汇合时带上各自的守卫 → ret_cond 两条 + ret_int（SPEC-0025）
    auto ret = [](ExprId v) {
        Inst i;
        i.op = InstOp::Ret;
        i.val = v;
        return i;
    };
    Builder b;
    uint32_t id = b.Param("ulCellId", b.u32);
    uint32_t t = b.LocalVar("t0", b.u32);
    Diamond d;
    d.yes.insts = {b.Set(t, b.C(1))};
    d.no.insts = {b.Set(t, b.C(0))};
    d.join.insts = {ret(b.L(t))};
    d.Install(b, b.f.Add(Expr::Cmp(CmpOp::Le, b.L(id), b.C(16), false)),
              b.f.Add(Expr::Cmp(CmpOp::Gt, b.L(id), b.C(16), false)));
    std::vector<std::string> trace;
    interp::FunctionFacts facts = interp::AnalyzeFunction(b.m, b.f, &trace);
    ASSERT_TRUE(facts.effs.size() == 3);
    EXPECT_EQ(facts.effs[0].kind, "ret_cond");
    EXPECT_EQ(facts.effs[0].when.getValueOr("-"), "ret == 0");
    ASSERT_TRUE(facts.effs[0].then.hasValue());
    EXPECT_EQ((*facts.effs[0].then)[0], "p0 >= 17");
    EXPECT_EQ(facts.effs[1].when.getValueOr("-"), "ret == 1");
    EXPECT_EQ((*facts.effs[1].then)[0], "p0 <= 16");
    EXPECT_EQ(facts.effs[2].kind, "ret_int");
    // Dump=state：汇合点的值带情形
    bool joined = false;
    for (const std::string &line : trace) {
        if (line.rfind("b1:", 0) == 0) {
            joined = true;
            EXPECT_EQ(line, "b1: ulCellId=p0 t0=[0, 1]:branch{0: p0 >= 17; 1: p0 <= 16}");
        }
    }
    EXPECT_TRUE(joined);
    // 有一条路径返回非常数：没有 ret_cond
    Builder c;
    uint32_t x = c.Param("x", c.u32);
    uint32_t t2 = c.LocalVar("t0", c.u32);
    Diamond d2;
    d2.yes.insts = {c.Set(t2, c.C(1))};
    d2.no.insts = {c.Set(t2, c.L(x))};
    d2.join.insts = {ret(c.L(t2))};
    d2.Install(c, c.f.Add(Expr::Cmp(CmpOp::Le, c.L(x), c.C(16), false)),
               c.f.Add(Expr::Cmp(CmpOp::Gt, c.L(x), c.C(16), false)));
    EXPECT_TRUE(c.Run().effs.empty());
}

TEST(Interp, RetCondAtCaller)
{
    // 调用方：if (Check(id) != 0) return; 之后加守卫 p0 <= 16；r == 2 的边不可达（SPEC-0025 §2.3、§2.4）
    Builder b;
    uint32_t id = b.Param("ulCellId", b.u32);
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t r = b.LocalVar("r", b.u32);
    summary::Summary check;
    check.fn = "Check";
    summary::Eff ok;
    ok.kind = "ret_cond";
    ok.when = "ret == 0";
    ok.then = std::vector<std::string>{"p0 <= 16"};
    summary::Eff range;
    range.kind = "ret_int";
    range.range = std::vector<std::string>{"0", "1"};
    range.bound = std::string("const");
    check.effs = {ok, range};
    std::map<std::string, summary::Summary> callees{{"Check", check}};
    Diamond d;
    d.entry.insts = {CallOf("F@abc:Check", {b.L(id)}, 3, r)};
    d.yes.succ = {{0, kNone}};  // != 0 → return
    d.no.insts = {b.Store(b.PAdd(b.L(p), b.L(id)), 1, b.C(0), 5)};
    d.Install(b, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(r), b.C(0), false)),
              b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(r), b.C(0), false)));
    interp::FunctionFacts facts = interp::AnalyzeFunction(b.m, b.f, nullptr, models::ModelTable::Default(), &callees);
    EXPECT_EQ(Reqs(facts), "W have=ext(p1) obj=*p1 end=[p0 + 1, p0 + 1] bound=exact at=t.c:5 guard=p0 <= 16");
    // r == 2：情形里没有 2 → 不可达
    Builder c;
    uint32_t id2 = c.Param("ulCellId", c.u32);
    uint32_t p2 = c.Param("p", c.m.Intern(Type::Ptr(c.u8)));
    uint32_t r2 = c.LocalVar("r", c.u32);
    Diamond d2;
    d2.entry.insts = {CallOf("F@abc:Check", {c.L(id2)}, 3, r2)};
    d2.yes.insts = {c.Store(c.PAdd(c.L(p2), c.C(100)), 1, c.C(0), 6)};
    d2.Install(c, c.f.Add(Expr::Cmp(CmpOp::Eq, c.L(r2), c.C(2), false)),
               c.f.Add(Expr::Cmp(CmpOp::Ne, c.L(r2), c.C(2), false)));
    EXPECT_EQ(Reqs(interp::AnalyzeFunction(c.m, c.f, nullptr, models::ModelTable::Default(), &callees)), "");
}

TEST(Interp, ArgFields)
{
    // 局部结构体存了两个字段再传 &st：调用点实参带 fields（指针字段 {obj, have}、整数字段表达式，SPEC-0026）；
    // 连续给字段赋值不会互相清掉；第一次调用之后格已清，第二次调用没有 fields
    Builder b;
    Record r;
    r.id = 0;
    r.name = "CTX_S";
    r.size = 8;
    r.fields = {{"pucBuf", 0, b.m.Intern(Type::Ptr(b.u8))}, {"ulLen", 4, b.u32}};
    b.m.records.push_back(r);
    uint32_t st = b.LocalVar("stCtx", b.m.Intern(Type::Rec(0)), true);
    uint32_t buf = b.LocalVar("aucBuf", b.m.Intern(Type::Arr(b.u8, 64)), true);
    ExprId addr = b.f.Add(Expr::LAddr(st));
    b.Straight({b.Store(addr, 4, b.f.Add(Expr::LAddr(buf)), 3), b.Store(b.PAdd(addr, b.C(4)), 4, b.C(128), 4),
                CallOf("F@abc:Enc", {addr}, 5), CallOf("F@abc:Enc", {addr}, 6)});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 2);
    const summary::ArgValue &a = facts.calls[0].args[0];
    ASSERT_TRUE(a.kind == summary::ArgValue::Kind::Ptr && a.ptr->fields.hasValue());
    EXPECT_EQ(a.ptr->fields->size(), 2u);
    EXPECT_EQ(a.ptr->fields->at("pucBuf").ptr->obj, "local:aucBuf");
    EXPECT_EQ(a.ptr->fields->at("pucBuf").ptr->have, "64");
    EXPECT_EQ(a.ptr->fields->at("ulLen").expr, "128");
    EXPECT_FALSE(facts.calls[1].args[0].ptr->fields.hasValue());
}

TEST(Interp, AliasFieldWritten)
{
    // 把自己的形参 p 原样传下去：没改过的字段不写（按别名代换成入口值）；改过的写当前值；不知道当前值写成未知；
    // 把 p 交给一个被调者之后，所有字段都可能被改（SPEC-0026，LV5-04）
    Builder b;
    Record r;
    r.id = 0;
    r.name = "CTX_S";
    r.size = 8;
    r.fields = {{"pucBuf", 0, b.m.Intern(Type::Ptr(b.u8))}, {"ulLen", 4, b.u32}};
    b.m.records.push_back(r);
    uint32_t p = b.Param("pstCtx", b.m.Intern(Type::Ptr(b.m.Intern(Type::Rec(0)))));
    uint32_t t = b.LocalVar("t0", b.u32);
    uint32_t u = b.LocalVar("t1", b.u32);
    ExprId len = b.PAdd(b.L(p), b.C(4));
    b.Straight({b.Load(t, len, 4, 4), b.Store(len, 4, b.Math(BinOp::Add, b.L(t), b.C(16)), 4),
                CallOf("F@abc:Put", {b.L(p)}, 5), CallOf("F@abc:Put", {b.L(p)}, 7)});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 2);
    auto fields = [&](size_t i) {
        std::string out;
        const summary::ArgValue &a = facts.calls[i].args[0];
        if (!a.ptr->fields) {
            return std::string("-");
        }
        for (const auto &f : *a.ptr->fields) {
            out += f.first + "=" +
                   (f.second.kind == summary::ArgValue::Kind::Ptr ? f.second.ptr->have : f.second.expr) + " ";
        }
        return out;
    };
    EXPECT_EQ(fields(0), "ulLen=p0->ulLen + 16 ");                         // 改了 ulLen，pucBuf 没改
    EXPECT_EQ(fields(1), "pucBuf=_unknown_pucBuf ulLen=_unknown_ulLen ");  // 前一次调用拿到了 p：都可能被改
    EXPECT_EQ(facts.calls[0].args[0].ptr->alias.getValueOr("-"), "p0");
    // 什么都没改：不写 fields；写了不确切的值：未知
    Builder c;
    c.m.records.push_back(r);
    uint32_t q = c.Param("pstCtx", c.m.Intern(Type::Ptr(c.m.Intern(Type::Rec(0)))));
    uint32_t v = c.LocalVar("t0", c.u32);
    c.Straight({CallOf("F@abc:Put", {c.L(q)}, 3), CallOf("F@abc:Get", {}, 4)});
    EXPECT_FALSE(c.Run().calls[0].args[0].ptr->fields.hasValue());
    Builder d;
    d.m.records.push_back(r);
    uint32_t q2 = d.Param("pstCtx", d.m.Intern(Type::Ptr(d.m.Intern(Type::Rec(0)))));
    uint32_t v2 = d.LocalVar("t0", d.u32);
    d.Straight({d.Load(v2, d.f.Add(Expr::Unknown("float")), 4, 3), d.Store(d.PAdd(d.L(q2), d.C(4)), 4, d.L(v2), 3),
                CallOf("F@abc:Put", {d.L(q2)}, 4)});
    interp::FunctionFacts facts3 = d.Run();
    ASSERT_TRUE(facts3.calls[0].args[0].ptr->fields.hasValue());
    EXPECT_EQ(facts3.calls[0].args[0].ptr->fields->at("ulLen").expr, "_unknown_ulLen");  // 读自未知内存，不是确切值
    (void)u;
    (void)v;
}

TEST(Interp, LocalFnPtrResolved)
{
    // S9.1（SPEC-0058）：只被赋过函数地址常量的局部函数指针。两个目标 → 各一条 alt 调用点记录；一个目标（另有空指针初值、
    // 隔着类型转换）→ 当直接调用；来自形参 / load 的 → 解析不出，记账 fptr
    Builder b;
    TypeId ptr = b.m.Intern(Type::Ptr(b.u8));
    uint32_t p = b.Param("p", ptr);
    uint32_t cb = b.Param("pfnCb", ptr);
    uint32_t sel = b.LocalVar("pfnSel", ptr);
    uint32_t tmp = b.LocalVar("t0", ptr);
    uint32_t one = b.LocalVar("pfnOne", ptr);
    uint32_t loaded = b.LocalVar("pfnLoaded", ptr);
    auto ind = [&](ExprId callee, unsigned line) {
        Inst i = CallOf("", {b.L(p), b.C(16)}, line);
        i.calleeExpr = callee;
        return i;
    };
    b.Straight({b.Set(tmp, b.f.Add(Expr::FAddr("F@abc:Nr"))), b.Set(tmp, b.f.Add(Expr::FAddr("F@abc:Lte"))),
                b.Set(sel, b.L(tmp)), ind(b.L(sel), 10), b.Set(one, b.C(0)),
                b.Set(one, b.f.Add(Expr::Cast(32, false, b.f.Add(Expr::FAddr("F@abc:Ok"))))), ind(b.L(one), 11),
                ind(b.L(cb), 12), b.Load(loaded, b.L(p), 4, 13), ind(b.L(loaded), 14)});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 3);
    EXPECT_EQ(facts.calls[0].callee, "Lte");
    EXPECT_EQ(facts.calls[1].callee, "Nr");
    for (int k : {0, 1}) {
        EXPECT_EQ(facts.calls[k].site, "t.c:10");
        EXPECT_TRUE(facts.calls[k].alt);
        EXPECT_TRUE(facts.calls[k].ind);
    }
    EXPECT_EQ(facts.calls[2].site, "t.c:11");
    EXPECT_EQ(facts.calls[2].callee, "Ok");
    EXPECT_FALSE(facts.calls[2].alt);
    EXPECT_TRUE(facts.calls[2].ind);
    ASSERT_TRUE(facts.opens.size() == 2);
    EXPECT_EQ(facts.opens[0].reason + "@" + facts.opens[0].at, "fptr@t.c:12");
    EXPECT_EQ(facts.opens[1].reason + "@" + facts.opens[1].at, "fptr@t.c:14");
}

TEST(Interp, UnsupportedConstructReason)
{
    // S5b-2（SPEC-0060）：下标 / 基址来自不支持的构造 → 账本原因 unsupported_construct / inline_asm；认识、只是值不知道的照旧
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t q = b.LocalVar("q", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t k = b.LocalVar("k", b.u32);
    Inst asmOut;
    asmOut.op = InstOp::Havoc;
    asmOut.dst = k;
    asmOut.why = "inline_asm";
    b.Straight({b.Store(b.PAdd(b.L(p), b.f.Add(Expr::Unknown("VAArgExpr"))), 1, b.C(0), 5),
                b.Set(q, b.f.Add(Expr::Unknown("CompoundLiteralExpr"))), b.Store(b.L(q), 1, b.C(0), 6), asmOut,
                b.Store(b.PAdd(b.L(p), b.L(k)), 1, b.C(0), 7),
                b.Store(b.PAdd(b.L(p), b.f.Add(Expr::Unknown("float"))), 1, b.C(0), 8)});
    interp::FunctionFacts facts = b.Run();
    std::string got;
    for (const summary::Open &o : facts.opens) {
        got += (got.empty() ? "" : " | ") + o.reason + "@" + o.at;
    }
    EXPECT_EQ(got,
              "unsupported_construct@t.c:5 | unsupported_construct@t.c:6 | inline_asm@t.c:7 | type_range_only@t.c:8");
}

TEST(Interp, RangeFieldArg)
{
    // SPEC-0069：两条分支给局部结构体的字段写 5 / 100，汇合后是 [5, 100]（branch）；调用点记录写成 _fld0_ulLen + locals
    Builder b;
    Record r;
    r.id = 0;
    r.name = "CTX_S";
    r.size = 4;
    r.fields = {{"ulLen", 0, b.u32}};
    b.m.records.push_back(r);
    uint32_t n = b.Param("n", b.u32);
    uint32_t st = b.LocalVar("st", b.m.Intern(Type::Rec(0)), true);
    ExprId addr = b.f.Add(Expr::LAddr(st));
    Block entry;
    entry.id = 4;
    entry.succ = {{3, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(n), b.C(0), false))},
                  {2, b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(n), b.C(0), false))}};
    Block big;
    big.id = 3;
    big.insts = {b.Store(addr, 4, b.C(100), 3)};
    big.succ = {{1, kNone}};
    Block small;
    small.id = 2;
    small.insts = {b.Store(addr, 4, b.C(5), 4)};
    small.succ = {{1, kNone}};
    Block join;
    join.id = 1;
    join.insts = {CallOf("F@abc:Fill", {addr}, 5)};
    join.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    b.f.blocks = {entry, big, small, join, exit};
    b.f.entry = 4;
    b.f.exit = 0;
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.calls.size() == 1);
    const summary::Call &c = facts.calls[0];
    ASSERT_TRUE(c.args[0].ptr->fields.hasValue());
    EXPECT_EQ(c.args[0].ptr->fields->at("ulLen").expr, "_fld0_ulLen");
    ASSERT_TRUE(c.locals.hasValue() && c.locals->count("_fld0_ulLen") == 1);
    const summary::Local &l = c.locals->at("_fld0_ulLen");
    EXPECT_EQ(l.lo + " " + l.hi + " " + l.bound, "5 100 branch");
}

TEST(Interp, RetPtrEffect)
{
    // SPEC-0070：每条返回都是 p + 4（或空指针）→ ret_ptr target p0 value 4；偏移不同 → 不记
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    b.f.ret = b.m.Intern(Type::Ptr(b.u8));
    Inst ret;
    ret.op = InstOp::Ret;
    ret.val = b.PAdd(b.L(p), b.C(4));
    b.Straight({ret});
    interp::FunctionFacts facts = b.Run();
    ASSERT_TRUE(facts.effs.size() == 1);
    EXPECT_EQ(facts.effs[0].kind, "ret_ptr");
    EXPECT_EQ(facts.effs[0].target.getValueOr(""), "p0");
    EXPECT_EQ(facts.effs[0].value.getValueOr(""), "4");
    Builder c;
    uint32_t q = c.Param("p", c.m.Intern(Type::Ptr(c.u8)));
    uint32_t n = c.Param("n", c.u32);
    c.f.ret = c.m.Intern(Type::Ptr(c.u8));
    Inst r1;
    r1.op = InstOp::Ret;
    r1.val = c.PAdd(c.L(q), c.C(2));
    Inst r2 = r1;
    r2.val = c.PAdd(c.L(q), c.C(6));
    Block entry;
    entry.id = 3;
    entry.succ = {{2, c.f.Add(Expr::Cmp(CmpOp::Ne, c.L(n), c.C(0), false))},
                  {1, c.f.Add(Expr::Cmp(CmpOp::Eq, c.L(n), c.C(0), false))}};
    Block a;
    a.id = 2;
    a.insts = {r1};
    a.succ = {{0, kNone}};
    Block d;
    d.id = 1;
    d.insts = {r2};
    d.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    c.f.blocks = {entry, a, d, exit};
    c.f.entry = 3;
    c.f.exit = 0;
    interp::FunctionFacts facts2 = c.Run();
    EXPECT_TRUE(std::none_of(facts2.effs.begin(), facts2.effs.end(),
                             [](const summary::Eff &e) { return e.kind == "ret_ptr"; }));
}

TEST(Interp, CalleeFieldResult)
{
    // SPEC-0072：被调者 ret_int [p0->ulLen, p0->ulLen]；调用方 st.ulLen = 100 后 GetLen(&st) → 100（原来被当成调用方自己的 p0->ulLen）
    Builder b;
    Record r;
    r.id = 0;
    r.name = "CTX_S";
    r.size = 4;
    r.fields = {{"ulLen", 0, b.u32}};
    b.m.records.push_back(r);
    uint32_t mine = b.Param("pstMine", b.m.Intern(Type::Ptr(b.m.Intern(Type::Rec(0)))));
    uint32_t a = b.LocalVar("a", b.m.Intern(Type::Arr(b.u8, 16)), true);
    uint32_t st = b.LocalVar("st", b.m.Intern(Type::Rec(0)), true);
    uint32_t t = b.LocalVar("t0", b.u32);
    b.Straight({b.Store(b.f.Add(Expr::LAddr(st)), 4, b.C(100), 3),
                CallOf("F@abc:GetLen", {b.f.Add(Expr::LAddr(st))}, 4, t),
                b.Store(b.PAdd(b.f.Add(Expr::LAddr(a)), b.L(t)), 1, b.C(0), 5)});
    summary::Summary callee;
    callee.fn = "GetLen";
    callee.key = "F@abc:GetLen";
    summary::Eff e;
    e.kind = "ret_int";
    e.range = std::vector<std::string>{"p0->ulLen", "p0->ulLen"};
    e.bound = "exact";
    callee.effs.push_back(e);
    summary::FieldInfo info;
    info.off = 0;
    info.size = 4;
    info.type = std::string("u32");
    callee.fields = std::map<std::string, summary::FieldInfo>{{"p0->ulLen", info}};
    std::map<std::string, summary::Summary> callees{{callee.key, callee}};
    interp::FunctionFacts facts =
        interp::AnalyzeFunction(b.m, b.f, nullptr, models::ModelTable::Default(), &callees, nullptr);
    // a[100] 在本函数里就判出来（末端 101 > 16），没有出现 p0->ulLen
    EXPECT_EQ(Reqs(facts), "W have=16 obj=local:a end=[101, 101] bound=exact at=t.c:5");
    (void)mine;
}

TEST(Interp, ScalarPointee)
{
    // SPEC-0071：*pLen（指向整数的形参）读成伪字段 p1->_0；经同类型的另一个整数指针形参写过之后不再是入口值
    Builder b;
    TypeId pu32 = b.m.Intern(Type::Ptr(b.u32));
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t len = b.Param("pLen", pu32);
    uint32_t t = b.LocalVar("t0", b.u32);
    b.Straight({b.Load(t, b.L(len), 4, 3), b.Store(b.PAdd(b.L(p), b.L(t)), 1, b.C(0), 4)});
    interp::FunctionFacts facts = b.Run();
    EXPECT_EQ(Reqs(facts),
              "R have=ext(p1) obj=*p1 end=[4, 4] bound=exact at=t.c:3 | "
              "W have=ext(p0) obj=*p0 end=[p1->_0 + 1, p1->_0 + 1] bound=exact at=t.c:4");
    ASSERT_TRUE(facts.fields.count("p1->_0") == 1);
    EXPECT_EQ(facts.fields.at("p1->_0").type.getValueOr(""), "u32");
    Builder c;
    TypeId q32 = c.m.Intern(Type::Ptr(c.u32));
    uint32_t q = c.Param("p", c.m.Intern(Type::Ptr(c.u8)));
    uint32_t out = c.Param("pOut", q32);
    uint32_t len2 = c.Param("pLen", q32);
    uint32_t t2 = c.LocalVar("t0", c.u32);
    c.Straight({c.Store(c.L(out), 4, c.C(100), 3), c.Load(t2, c.L(len2), 4, 4),
                c.Store(c.PAdd(c.L(q), c.L(t2)), 1, c.C(0), 5)});
    interp::FunctionFacts facts2 = c.Run();
    EXPECT_EQ(facts2.fields.count("p2->_0"), size_t(0));  // 可能就是 *pOut：不是入口值
    EXPECT_EQ(Reqs(facts2).find("p2->_0"), std::string::npos);
}

TEST(Interp, FieldEffect)
{
    // SPEC-0073：两条路径——一条写 *pLen = 32 后返回，一条没碰就返回 → field p0->_0 := 32（may）；
    // 两条路径写不同的常数 32 / 8 → range [8, 32]（SPEC-0074）
    auto build = [](bool third) {
        Builder b;
        uint32_t len = b.Param("pLen", b.m.Intern(Type::Ptr(b.u32)));
        uint32_t n = b.Param("n", b.u32);
        Inst ret;
        ret.op = InstOp::Ret;
        Block entry;
        entry.id = 3;
        entry.succ = {{2, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(n), b.C(0), false))},
                      {1, b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(n), b.C(0), false))}};
        Block w;
        w.id = 2;
        w.insts = {b.Store(b.L(len), 4, b.C(32), 3), ret};
        w.succ = {{0, kNone}};
        Block keep;
        keep.id = 1;
        keep.insts = {ret};
        if (third) {
            keep.insts = {b.Store(b.L(len), 4, b.C(8), 4), ret};
        }
        keep.succ = {{0, kNone}};
        Block exit;
        exit.id = 0;
        b.f.blocks = {entry, w, keep, exit};
        b.f.entry = 3;
        b.f.exit = 0;
        return b.Run();
    };
    interp::FunctionFacts facts = build(false);
    auto field =
        std::find_if(facts.effs.begin(), facts.effs.end(), [](const summary::Eff &e) { return e.kind == "field"; });
    ASSERT_TRUE(field != facts.effs.end());
    EXPECT_EQ(field->target.getValueOr(""), "p0->_0");
    EXPECT_EQ(field->value.getValueOr(""), "32");
    EXPECT_TRUE(field->may.getValueOr(false));
    ASSERT_TRUE(facts.fields.count("p0->_0") == 1);
    interp::FunctionFacts two = build(true);
    auto ranged =
        std::find_if(two.effs.begin(), two.effs.end(), [](const summary::Eff &e) { return e.kind == "field"; });
    ASSERT_TRUE(ranged != two.effs.end());
    EXPECT_FALSE(ranged->value.hasValue());
    ASSERT_TRUE(ranged->range.hasValue() && ranged->range->size() == 2);
    EXPECT_EQ((*ranged->range)[0] + " " + (*ranged->range)[1], "8 32");
}

TEST(Interp, DependentNarrowing)
{
    // SPEC-0075：t = n; n = (t − 1):u32; 分支 t != 0 → 真边上 n 按收窄后的 t 重算成 [0, p1 − 1]，p[n] 末端 p1
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("n", b.u32);
    uint32_t t = b.LocalVar("t0", b.u32);
    Loop l;
    l.body.insts = {b.Store(b.PAdd(b.L(p), b.L(n)), 1, b.C(0), 7)};
    // 循环头里先算 t = n、n = t − 1，再按 t 分支（Loop 把条件放在头上）
    l.head.insts = {b.Set(t, b.L(n)), b.Set(n, b.Wrap(BinOp::Sub, b.L(t), b.C(1)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(t), b.C(0), false)),
              b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(t), b.C(0), false)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[1, p1] bound=branch at=t.c:7");
}

TEST(Interp, SignedBoundFaithful)
{
    // SPEC-0076：for (i = n − 1; i >= 0; i--) p[i] = 0;（INT32）——循环体 i ∈ [0, p1 − 1]，末端 [1, p1]，没有负偏移的 Req
    Builder b;
    uint32_t p = b.Param("p", b.m.Intern(Type::Ptr(b.u8)));
    uint32_t n = b.Param("n", b.i32);
    uint32_t i = b.LocalVar("i", b.i32);
    uint32_t t = b.LocalVar("t0", b.i32);
    Loop l;
    l.entry.insts = {b.Set(i, b.Wrap(BinOp::Sub, b.L(n), b.C(1), 32, true))};
    l.body.insts = {b.Store(b.PAdd(b.L(p), b.L(i)), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(t, b.L(i)), b.Set(i, b.Wrap(BinOp::Sub, b.L(t), b.C(1), 32, true))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(i), b.C(0), true)),
              b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(i), b.C(0), true)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[1, p1] bound=branch at=t.c:7 guard=p1 >= 1");
}

TEST(Interp, GlobalEffect)
{
    // SPEC-0077：写整数全局 g_ulMax = 32 → global g:g_ulMax := 32；写完又调用了没有摘要的函数 → 不记
    auto build = [](bool callAfter) {
        Builder b;
        Global g;
        g.key = "G:g_ulMax";
        g.name = "g_ulMax";
        g.def = true;
        g.type = b.u32;
        b.m.globals.push_back(g);
        std::vector<Inst> insts = {b.Store(b.f.Add(Expr::GAddr("G:g_ulMax")), 4, b.C(32), 3)};
        if (callAfter) {
            insts.push_back(CallOf("F:Ext_Get", {}, 4));
        }
        b.Straight(insts);
        return b.Run();
    };
    interp::FunctionFacts facts = build(false);
    auto global =
        std::find_if(facts.effs.begin(), facts.effs.end(), [](const summary::Eff &e) { return e.kind == "global"; });
    ASSERT_TRUE(global != facts.effs.end());
    EXPECT_EQ(global->target.getValueOr(""), "g:g_ulMax");
    EXPECT_EQ(global->value.getValueOr(""), "32");
    EXPECT_FALSE(global->may.hasValue());
    interp::FunctionFacts after = build(true);
    EXPECT_TRUE(
        std::none_of(after.effs.begin(), after.effs.end(), [](const summary::Eff &e) { return e.kind == "global"; }));
}

TEST(Interp, ConditionalWriteEffect)
{
    // SPEC-0079：返回 1 的路径没写、返回 0 的路径写 *pLen = 32 → may 的那条之外另有 field p0->_0 := 32 when ret == 0
    Builder b;
    uint32_t len = b.Param("pLen", b.m.Intern(Type::Ptr(b.u32)));
    uint32_t n = b.Param("n", b.u32);
    b.f.ret = b.u32;
    Inst ret0;
    ret0.op = InstOp::Ret;
    ret0.val = b.C(0);
    Inst ret1 = ret0;
    ret1.val = b.C(1);
    Block entry;
    entry.id = 3;
    entry.succ = {{2, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(n), b.C(0), false))},
                  {1, b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(n), b.C(0), false))}};
    Block w;
    w.id = 2;
    w.insts = {b.Store(b.L(len), 4, b.C(32), 3), ret0};
    w.succ = {{0, kNone}};
    Block keep;
    keep.id = 1;
    keep.insts = {ret1};
    keep.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    b.f.blocks = {entry, w, keep, exit};
    b.f.entry = 3;
    b.f.exit = 0;
    interp::FunctionFacts facts = b.Run();
    auto cond = std::find_if(facts.effs.begin(), facts.effs.end(),
                             [](const summary::Eff &e) { return e.kind == "field" && e.when.hasValue(); });
    ASSERT_TRUE(cond != facts.effs.end());
    EXPECT_EQ(cond->target.getValueOr(""), "p0->_0");
    EXPECT_EQ(cond->value.getValueOr(""), "32");
    EXPECT_EQ(cond->when.getValueOr(""), "ret == 0");
    EXPECT_FALSE(cond->may.hasValue());
}

TEST(Interp, PointerCompareNarrows)
{
    // SPEC-0081：e = p + n; while (p < e) { *p = 0; p++; } → 循环体 p 的偏移 [0, p1 − 1]，末端 [1, p1]（原来 pointer_stepping）
    Builder b;
    TypeId pu8 = b.m.Intern(Type::Ptr(b.u8));
    uint32_t p = b.Param("p", pu8);
    uint32_t n = b.Param("n", b.u32);
    uint32_t e = b.LocalVar("e", pu8);
    Loop l;
    l.entry.insts = {b.Set(e, b.PAdd(b.L(p), b.L(n)))};
    l.body.insts = {b.Store(b.L(p), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(p, b.PAdd(b.L(p), b.C(1)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(p), b.L(e), false)),
              b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(p), b.L(e), false)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[1, p1] bound=loop at=t.c:7");
}

TEST(Interp, WidePointerCompareIsCoarse)
{
    // SPEC-0083：UINT32 *p; e = p + n; while (p < e) { *p = 0; p++; } → 按字节收窄到 4·p1 − 1，上界标 widen（不是代码给出的界）
    Builder b;
    TypeId pu32 = b.m.Intern(Type::Ptr(b.u32));
    uint32_t p = b.Param("p", pu32);
    uint32_t n = b.Param("n", b.u32);
    uint32_t e = b.LocalVar("e", pu32);
    Loop l;
    l.entry.insts = {b.Set(e, b.PAdd(b.L(p), b.f.Add(Expr::Bin(BinOp::Mul, b.L(n), b.C(4)))))};
    l.body.insts = {b.Store(b.L(p), 4, b.C(0), 7)};
    l.latch.insts = {b.Set(p, b.PAdd(b.L(p), b.C(4)))};
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Lt, b.L(p), b.L(e), false)),
              b.f.Add(Expr::Cmp(CmpOp::Ge, b.L(p), b.L(e), false)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[4, 4*p1 + 3] bound=widen at=t.c:7");
}

TEST(Interp, NeLoopThreshold)
{
    // SPEC-0082：for (i = 0; i != n; i++) p[i] = 0; → 头上 i ∈ [0, p1]（符号阈值），体内 [0, p1 − 1]（原来上界被 widen 掉）；
    // != 端点收窄不推守卫原子（与 < 不同，少一个守卫只是更保守）
    std::vector<std::string> trace;
    EXPECT_EQ(ForLoop(
                  CmpOp::Ne, [](Builder &b, uint32_t n) { return b.L(n); }, &trace),
              "W have=ext(p0) obj=*p0 end=[1, p1] bound=loop at=t.c:7");
    bool head = false;
    for (const std::string &line : trace) {
        if (line.rfind("b4:", 0) == 0) {
            head = true;
            EXPECT_EQ(line, "b4: p=*p0+0 n=p1 i=[0, p1]:loop (head)");
        }
        if (line.rfind("b3:", 0) == 0) {
            EXPECT_EQ(line, "b3: p=*p0+0 n=p1 i=[0, p1 - 1]:loop");
        }
    }
    EXPECT_TRUE(head);
    // 指针：for (q = p; q != p + n; q++) *q = 0;
    Builder b;
    TypeId pu8 = b.m.Intern(Type::Ptr(b.u8));
    uint32_t p = b.Param("p", pu8);
    uint32_t n = b.Param("n", b.u32);
    uint32_t q = b.LocalVar("q", pu8);
    Loop l;
    l.entry.insts = {b.Set(q, b.L(p))};
    l.body.insts = {b.Store(b.L(q), 1, b.C(0), 7)};
    l.latch.insts = {b.Set(q, b.PAdd(b.L(q), b.C(1)))};
    ExprId end = b.PAdd(b.L(p), b.L(n));
    l.Install(b, b.f.Add(Expr::Cmp(CmpOp::Ne, b.L(q), end, false)), b.f.Add(Expr::Cmp(CmpOp::Eq, b.L(q), end, false)));
    EXPECT_EQ(Reqs(b.Run()), "W have=ext(p0) obj=*p0 end=[1, p1] bound=loop at=t.c:7");
}
