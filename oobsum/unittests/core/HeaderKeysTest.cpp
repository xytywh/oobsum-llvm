//===-- HeaderKeysTest.cpp - Tests for core/HeaderKeys ----------*- C++ -*-===//
//
// 头文件函数的键加 #<FIR 哈希前 8 位>（SPEC-0059；06- D-02、D-08）。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/HeaderKeys.h"

using namespace oobsum::fir;

namespace {

// 一个 TU：主文件的 id 是 tu，头文件 buf.h 的 id 是 bbbb。
//   G（头文件）：局部结构体 S；store1 [p + bufLen] = 0
//   H（头文件）：调用 G、取 G 的地址、读函数内 static cnt
//   M（主文件，外部链接）：调用 H
// otherRecord：在 S 前面多一个记录（S 的记录号不同）；filesSwapped：文件表顺序不同
Module Tu(const std::string &tu, int64_t bufLen, bool otherRecord = false, bool filesSwapped = false)
{
    Module m;
    m.tu = tu;
    m.file = "/x/" + tu + ".c";
    m.files =
        filesSwapped ? std::vector<std::string>{"/inc/buf.h", m.file} : std::vector<std::string>{m.file, "/inc/buf.h"};
    uint32_t hdr = filesSwapped ? 0 : 1;
    TypeId u8 = m.Intern(Type::Int(8, false));
    TypeId ptr = m.Intern(Type::Ptr(u8));
    if (otherRecord) {
        Record other;
        other.id = 0;
        other.name = "OTHER_S";
        other.size = 4;
        m.records.push_back(other);
    }
    Record s;
    s.id = static_cast<uint32_t>(m.records.size());
    s.name = "S";
    s.size = 8;
    s.fields = {{"a", 0, m.Intern(Type::Int(32, false))}, {"b", 4, m.Intern(Type::Int(32, false))}};
    m.records.push_back(s);
    TypeId rec = m.Intern(Type::Rec(s.id));
    Global cnt;
    cnt.key = "S@F@bbbb:H:cnt";
    cnt.name = "cnt";
    cnt.def = true;
    cnt.type = m.Intern(Type::Int(32, false));
    m.globals.push_back(cnt);

    auto fn = [&](const std::string &key, const std::string &name) {
        Function f;
        f.key = key;
        f.name = name;
        f.ret = m.Intern(Type::Void());
        f.loc = {hdr, 1, 1};
        Local p;
        p.id = 0;
        p.name = "p";
        p.param = 0;
        p.type = ptr;
        f.locals.push_back(p);
        f.entry = 1;
        f.exit = 0;
        f.blocks.resize(2);
        f.blocks[0].id = 1;
        f.blocks[0].succ = {{0, kNone}};
        f.blocks[1].id = 0;
        return f;
    };
    auto call = [](const std::string &callee, ExprId arg, unsigned line) {
        Inst i;
        i.op = InstOp::Call;
        i.callee = callee;
        i.args = {arg};
        i.loc = {0, line, 5};
        return i;
    };
    Function g = fn("F@bbbb:G", "G");
    Local st;
    st.id = 1;
    st.name = "st";
    st.type = rec;
    st.obj = true;
    g.locals.push_back(st);
    Inst store;
    store.op = InstOp::Store;
    store.addr = g.Add(Expr::PAdd(g.Add(Expr::LocalValue(0)), g.Add(Expr::Const(bufLen))));
    store.size = 1;
    store.val = g.Add(Expr::Const(0));
    store.loc = {hdr, 2, 3};
    g.blocks[0].insts = {store};

    Function h = fn("F@bbbb:H", "H");
    Inst take;
    take.op = InstOp::Set;
    take.dst = 0;
    take.val = h.Add(Expr::FAddr("F@bbbb:G"));
    take.loc = {hdr, 3, 9};
    Inst load;
    load.op = InstOp::Load;
    load.dst = 0;
    load.addr = h.Add(Expr::GAddr("S@F@bbbb:H:cnt"));
    load.size = 4;
    load.loc = {hdr, 4, 3};
    h.blocks[0].insts = {call("F@bbbb:G", h.Add(Expr::LocalValue(0)), 3), take, load};
    h.blocks[0].insts[0].loc.file = hdr;

    Function main = fn("F:M", "M");
    main.ext = true;
    main.loc = {filesSwapped ? 1u : 0u, 10, 1};
    main.blocks[0].insts = {call("F@bbbb:H", main.Add(Expr::LocalValue(0)), 11)};
    main.blocks[0].insts[0].loc.file = filesSwapped ? 1 : 0;
    m.funcs = {g, h, main};
    return m;
}

const Function *Fn(const Module &m, const std::string &name)
{
    for (const Function &f : m.funcs) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

bool HashedFrom(const std::string &key, const std::string &base)
{
    return key.size() == base.size() + 9 && key.compare(0, base.size(), base) == 0 && key[base.size()] == '#' &&
           key.find_first_not_of("0123456789abcdef", base.size() + 1) == std::string::npos;
}

}  // namespace

TEST(HeaderKeys, OnlyHeaderFunctions)
{
    Module m = Tu("aaaa", 16);
    EXPECT_TRUE(IsHeaderFunctionKey(m, "F@bbbb:G"));
    EXPECT_FALSE(IsHeaderFunctionKey(m, "F@aaaa:Local"));  // 主文件里的 static
    EXPECT_FALSE(IsHeaderFunctionKey(m, "F:M"));
    EXPECT_FALSE(IsHeaderFunctionKey(m, "F@bbbb:G#0123abcd"));  // 已经加过
    std::map<std::string, std::string> renamed = FinalizeHeaderKeys(m);
    EXPECT_EQ(renamed.size(), size_t(2));
    EXPECT_TRUE(HashedFrom(Fn(m, "G")->key, "F@bbbb:G"));
    EXPECT_TRUE(HashedFrom(Fn(m, "H")->key, "F@bbbb:H"));
    EXPECT_EQ(Fn(m, "M")->key, "F:M");
}

TEST(HeaderKeys, RenamesReferences)
{
    // 调用、函数地址、函数内 static（globals 与 &S…）一起改
    Module m = Tu("aaaa", 16);
    FinalizeHeaderKeys(m);
    const Function *g = Fn(m, "G");
    const Function *h = Fn(m, "H");
    EXPECT_EQ(Fn(m, "M")->blocks[0].insts[0].callee, h->key);
    EXPECT_EQ(h->blocks[0].insts[0].callee, g->key);
    EXPECT_EQ(h->exprs[h->blocks[0].insts[1].val].text, g->key);
    EXPECT_EQ(h->exprs[h->blocks[0].insts[2].addr].text, "S@" + h->key + ":cnt");
    EXPECT_EQ(m.globals[0].key, "S@" + h->key + ":cnt");
}

TEST(HeaderKeys, SameBodySameKey)
{
    // 函数体相同：记录号、文件表顺序、TU 不同都不影响（D-08：只求解一次）
    Module a = Tu("aaaa", 16);
    Module c = Tu("cccc", 16, true, true);
    FinalizeHeaderKeys(a);
    FinalizeHeaderKeys(c);
    EXPECT_EQ(Fn(a, "G")->key, Fn(c, "G")->key);
    EXPECT_EQ(Fn(a, "H")->key, Fn(c, "H")->key);
}

TEST(HeaderKeys, DifferentBodyDifferentKey)
{
    // 宏展开不同（常数 16 / 32）：G 的键不同；H 只是调用 G，键也跟着不同（Merkle）
    Module a = Tu("aaaa", 16);
    Module b = Tu("bbbb2", 32);
    FinalizeHeaderKeys(a);
    FinalizeHeaderKeys(b);
    EXPECT_FALSE(Fn(a, "G")->key == Fn(b, "G")->key);
    EXPECT_FALSE(Fn(a, "H")->key == Fn(b, "H")->key);
    // 记录布局不同（字段偏移）也算不同
    Module d = Tu("dddd", 16);
    d.records[0].fields[1].off = 6;
    FinalizeHeaderKeys(d);
    EXPECT_FALSE(Fn(a, "G")->key == Fn(d, "G")->key);
}

TEST(HeaderKeys, MutualRecursion)
{
    // 互相调用的两个头文件函数：同一个分量，互相引用写基础键；与它们在模块里的顺序无关
    auto build = [](bool reversed) {
        Module m = Tu("aaaa", 16);
        Function *g = &m.funcs[0];
        Inst back;
        back.op = InstOp::Call;
        back.callee = "F@bbbb:H";
        back.args = {g->Add(Expr::LocalValue(0))};
        back.loc = {1, 2, 9};
        g->blocks[0].insts.push_back(back);
        if (reversed) {
            std::swap(m.funcs[0], m.funcs[1]);
        }
        FinalizeHeaderKeys(m);
        return m;
    };
    Module x = build(false);
    Module y = build(true);
    EXPECT_TRUE(HashedFrom(Fn(x, "G")->key, "F@bbbb:G"));
    EXPECT_EQ(Fn(x, "G")->key, Fn(y, "G")->key);
    EXPECT_EQ(Fn(x, "H")->key, Fn(y, "H")->key);
    EXPECT_EQ(Fn(x, "G")->blocks[0].insts.back().callee, Fn(x, "H")->key);
}
