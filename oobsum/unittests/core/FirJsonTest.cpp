//===-- FirJsonTest.cpp - Tests for core/FirJson ----------------*- C++ -*-===//
//
// FIR 的 JSON 读写（SPEC-0005）：06- §5.2 的例子往返相同；全部表达式 / 指令 / 类型往返；坏输入逐条报错且带位置；文件读写。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/FirJson.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using namespace oobsum::fir;

namespace {

// 06- §5.2 的例子，逐字照抄
const char *const kSpecExample = R"json(
{ "schema": "oobsum-fir/1", "tu": "abe9fde904012438", "file": "/src/ue/ue_init.c",
  "target": { "triple": "aarch64_be-unknown-linux-gnu", "ptr": 4, "long": 4, "big_endian": true },
  "files": [ "/src/ue/ue_init.c", "/src/inc/ue.h" ],
  "records": [ { "id": 0, "name": "UE_TBL_S", "size": 16,
                 "fields": [ { "name": "ulId", "off": 0, "t": { "k": "int", "w": 32, "s": false } },
                             { "name": "aucData", "off": 4, "t": { "k": "arr", "n": 12, "elem": { "k": "int", "w": 8, "s": false } } } ] } ],
  "globals": [ { "key": "G:g_ulUeNum", "name": "g_ulUeNum", "def": true, "t": { "k": "int", "w": 32, "s": false }, "loc": [0, 12, 8] } ],
  "funcs": [ { "key": "F:Ue_InitOne", "name": "Ue_InitOne", "ext": true, "loc": [0, 40, 6],
               "ret": { "k": "void" },
               "locals": [ { "id": 0, "name": "pstTbl", "param": 0, "t": { "k": "ptr", "to": { "k": "rec", "id": 0 } } },
                           { "id": 1, "name": "ulIdx",  "param": 1, "t": { "k": "int", "w": 32, "s": false } } ],
               "entry": 2, "exit": 0,
               "blocks": [ { "id": 2, "insts": [
                   { "op": "store", "addr": ["padd", ["l", 0], ["bin", "mul", ["l", 1], ["c", 16]]], "size": 4, "val": ["c", 0], "loc": [0, 42, 5] } ],
                 "succ": [ { "to": 0 } ] },
                 { "id": 0, "insts": [], "succ": [] } ] } ] }
)json";

std::string Print(const llvm::json::Value &v)
{
    std::string text;
    llvm::raw_string_ostream os(text);
    os << v;
    return os.str();
}

// 读入；成功返回 "<ok>"，失败返回带位置的错误
std::string ErrorOf(const std::string &text, Module *out = nullptr)
{
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
    if (!v) {
        return "parse: " + llvm::toString(v.takeError());
    }
    Module m;
    std::string error;
    if (!ModuleFromJson(*v, m, error)) {
        return error;
    }
    if (out != nullptr) {
        *out = std::move(m);
    }
    return "<ok>";
}

// 把例子里恰好出现一次的 from 换成 to（出现次数不对时让用例失败）
std::string Mutate(const std::string &from, const std::string &to)
{
    std::string text = kSpecExample;
    size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
        return "<mutation '" + from + "' does not occur exactly once>";
    }
    return text.replace(at, from.size(), to);
}

// 每种表达式、指令、类型、可选键 / null 键各至少一次
Module BuildEverything()
{
    Module m;
    m.tu = "0123456789abcdef";
    m.file = "/a.c";
    m.target = {"armebv7-unknown-linux-gnueabi", 4, 4, true};
    m.files = {"/a.c", "/inc/a.h"};
    TypeId i8 = m.Intern(Type::Int(8, false));
    TypeId i32 = m.Intern(Type::Int(32, true));
    TypeId u64 = m.Intern(Type::Int(64, false));
    TypeId rec = m.Intern(Type::Rec(5));
    Record r;
    r.id = 5;
    r.name = "NODE_S";
    r.size = 8;
    r.fields = {{"lVal", 0, i32}, {"pstNext", 4, m.Intern(Type::Ptr(rec))}};  // 自引用的记录
    m.records.push_back(r);
    m.globals.push_back({"G:g_aucBuf", "g_aucBuf", false, m.Intern(Type::Arr(i8, -1)), {1, 3, 15}});
    m.globals.push_back({"G@0123456789abcdef:s_x", "s_x", true, m.Intern(Type::Other(12)), {0, 4, 12}});

    Function f;
    f.key = "F:Everything";
    f.name = "Everything";
    f.ext = true;
    f.loc = {0, 10, 6};
    f.ret = m.Intern(Type::Void());
    f.flags = {"cfg_too_large"};
    Local p0;
    p0.id = 0;
    p0.name = "pstNode";
    p0.param = 0;
    p0.type = m.Intern(Type::Ptr(rec));
    Local arr;
    arr.id = 1;
    arr.name = "alBuf";
    arr.type = m.Intern(Type::Arr(i32, 4));
    arr.obj = true;
    Local tmp;
    tmp.id = 2;
    tmp.name = "tmp0";
    tmp.type = i32;
    tmp.tmp = true;
    Local n;
    n.id = 9;
    n.name = "ulLen";
    n.param = 1;
    n.type = u64;
    f.locals = {p0, arr, tmp, n};

    ExprId l0 = f.Add(Expr::LocalValue(0));
    ExprId l9 = f.Add(Expr::LocalValue(9));
    ExprId c16 = f.Add(Expr::Const(16));
    ExprId neg = f.Add(Expr::Const(-5));
    ExprId math = f.Add(Expr::Bin(BinOp::Mul, l9, c16));
    ExprId wrap = f.Add(Expr::Bin(BinOp::Sub, l9, neg, 32, false));
    ExprId cmp = f.Add(Expr::Cmp(CmpOp::Lt, l9, c16, false));
    ExprId lnot = f.Add(Expr::Unary(ExprOp::LNot, cmp));
    ExprId negx = f.Add(Expr::Unary(ExprOp::Neg, l9));
    ExprId bnot = f.Add(Expr::Unary(ExprOp::BNot, l9));
    ExprId cast = f.Add(Expr::Cast(16, true, wrap));
    ExprId laddr = f.Add(Expr::LAddr(1));
    ExprId gaddr = f.Add(Expr::GAddr("G:g_aucBuf"));
    ExprId str = f.Add(Expr::Str(5));
    ExprId faddr = f.Add(Expr::FAddr("F:Callee"));
    ExprId padd = f.Add(Expr::PAdd(laddr, math));
    ExprId fld = f.Add(Expr::Fld(l0, 0, 4, "lVal"));
    ExprId psub = f.Add(Expr::PSub(padd, laddr, 4));
    ExprId unk = f.Add(Expr::Unknown("float"));

    Block entry;
    entry.id = 3;
    Inst set;
    set.op = InstOp::Set;
    set.dst = 2;
    set.val = psub;
    set.loc = {0, 11, 5};
    Inst load;
    load.op = InstOp::Load;
    load.dst = 2;
    load.addr = fld;
    load.size = 4;
    load.sub = true;
    load.loc = {0, 12, 5};
    Inst store;
    store.op = InstOp::Store;
    store.addr = padd;
    store.size = 4;
    store.val = kNone;  // 写入值不关心
    store.loc = {0, 13, 5};
    Inst call;
    call.op = InstOp::Call;
    call.dst = 2;
    call.callee = "F:Callee";
    call.args = {gaddr, str, cast, lnot, negx, bnot, unk};
    call.ord = 0;
    call.loc = {1, 2, 3};
    Inst ind;
    ind.op = InstOp::Call;
    ind.calleeExpr = faddr;
    ind.ord = 1;
    ind.loc = {0, 14, 5};
    Inst havoc;
    havoc.op = InstOp::Havoc;
    havoc.dst = 2;
    havoc.why = "inline_asm";
    havoc.loc = {0, 15, 5};
    entry.insts = {set, load, store, call, ind, havoc};
    entry.succ = {{1, cmp}, {0, kNone}};
    Block body;
    body.id = 1;
    Inst ret;
    ret.op = InstOp::Ret;
    ret.val = kNone;
    ret.loc = {0, 16, 5};
    body.insts = {ret};
    body.succ = {{0, kNone}};
    Block exit;
    exit.id = 0;
    f.blocks = {entry, body, exit};
    f.entry = 3;
    f.exit = 0;
    m.funcs.push_back(std::move(f));
    return m;
}

}  // namespace

TEST(FirJson, SpecExampleRoundTrips)
{
    Module m;
    ASSERT_TRUE(ErrorOf(kSpecExample, &m) == "<ok>");
    EXPECT_EQ(m.funcs.size(), size_t(1));
    EXPECT_EQ(m.funcs[0].entry, 2u);
    EXPECT_EQ(m.funcs[0].FindLocal(1)->param, 1);
    llvm::Expected<llvm::json::Value> original = llvm::json::parse(kSpecExample);
    ASSERT_TRUE(static_cast<bool>(original));
    EXPECT_EQ(Print(ModuleToJson(m)), Print(*original));
}

TEST(FirJson, EverythingRoundTrips)
{
    std::string first = Print(ModuleToJson(BuildEverything()));
    Module back;
    ASSERT_TRUE(ErrorOf(first, &back) == "<ok>");
    EXPECT_EQ(Print(ModuleToJson(back)), first);
    // §2.3 的写法
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["bin","mul",["l",9],["c",16]])"));          // 数学整数：4 个元素
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["bin","sub",["l",9],["c",-5],32,false])"));  // 回绕：6 个元素
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["cmp","lt",["l",9],["c",16],false])"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("n":null)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("callee":["ind",["faddr","F:Callee"]])"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("val":null)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("sub":true)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("tmp":true)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("flags":["cfg_too_large"])"));
    EXPECT_FALSE(llvm::StringRef(first).contains(R"("obj":false)"));  // 默认值省略（§2.3 #6）
}

TEST(FirJson, ErrorsCarryLocation)
{
    EXPECT_EQ(ErrorOf(Mutate("oobsum-fir/1", "oobsum-fir/2")), "schema: expected 'oobsum-fir/1', got 'oobsum-fir/2'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["foo", 0])")),
              "funcs[0].blocks[0].insts[0].val: unknown expression op 'foo'");
    EXPECT_EQ(ErrorOf(Mutate(R"("size": 4, )", R"("size": 4, "note": 1, )")),
              "funcs[0].blocks[0].insts[0]: unknown key 'note'");
    EXPECT_EQ(ErrorOf(Mutate(R"("size": 4, )", "")), "funcs[0].blocks[0].insts[0]: missing key 'size'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["padd", ["l", 0], )", R"(["padd", )")),
              "funcs[0].blocks[0].insts[0].addr: 'padd' with 2 elements");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 16]])", R"(["c", 16], 32])")),
              "funcs[0].blocks[0].insts[0].addr[2]: 'bin' with 5 elements");
    EXPECT_EQ(ErrorOf(Mutate(R"(["padd", ["l", 0])", R"(["padd", ["l", 5])")),
              "funcs[0].blocks[0].insts[0].addr[1][1]: unknown local id 5");
    EXPECT_EQ(ErrorOf(Mutate(R"("succ": [ { "to": 0 } ])", R"("succ": [ { "to": 7 } ])")),
              "funcs[0].blocks[0].succ[0].to: unknown block id 7");
    EXPECT_EQ(ErrorOf(Mutate(R"("entry": 2)", R"("entry": 9)")), "funcs[0].entry: unknown block id 9");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "k": "rec", "id": 0 })", R"({ "k": "rec", "id": 3 })")),
              "funcs[0].locals[0].t.to.id: unknown record id 3");
    EXPECT_EQ(ErrorOf(Mutate(R"("loc": [0, 42, 5])", R"("loc": [3, 42, 5])")),
              "funcs[0].blocks[0].insts[0].loc[0]: file index 3 out of range (2 files)");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 18446744073709551615])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    // LLVM 14 的 json::parse 把超出 int64 的数饱和成 INT64_MAX / INT64_MIN，所以这两个值本身也拒绝（§2.3 #5）
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 9223372036854775807])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", -9223372036854775809])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 9223372036854775806])")), "<ok>");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "id": 0, "insts": [], "succ": [] })", R"({ "id": 2, "insts": [], "succ": [] })")),
              "funcs[0].blocks[1].id: duplicate block id 2");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "k": "void" })", R"({ "k": "float" })")),
              "funcs[0].ret.k: unknown type kind 'float'");
    EXPECT_EQ(ErrorOf(Mutate(R"("op": "store")", R"("op": "memcpy")")),
              "funcs[0].blocks[0].insts[0].op: unknown instruction op 'memcpy'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["bin", "mul")", R"(["bin", "pow")")),
              "funcs[0].blocks[0].insts[0].addr[2][1]: unknown bin op 'pow'");
    EXPECT_EQ(ErrorOf(Mutate(R"("param": 1)", R"("param": 0)")), "funcs[0].locals[1].param: duplicate param index 0");
    EXPECT_EQ(ErrorOf(Mutate(R"("big_endian": true)", R"("big_endian": 1)")),
              "target.big_endian: expected true / false");
}

TEST(FirJson, NullableKeys)
{
    // store / ret 的 val 可为 null；set 的不行（§2.3 #7）
    EXPECT_EQ(ErrorOf(Mutate(R"("val": ["c", 0])", R"("val": null)")), "<ok>");
    EXPECT_EQ(ErrorOf(Mutate(R"("op": "store", "addr")", R"("op": "set", "dst": 1, "addr")")),
              "funcs[0].blocks[0].insts[0]: unknown key 'addr'");
    EXPECT_EQ(ErrorOf(Mutate(R"("insts": [], "succ": [] })",
                             R"("insts": [ { "op": "set", "dst": 1, "val": null, "loc": [0, 1, 1] } ], "succ": [] })")),
              "funcs[0].blocks[1].insts[0].val: expected an array");
}

TEST(FirJson, DeepNestingIsRejected)
{
    std::string deep = "[\"c\", 1]";
    for (int i = 0; i < 300; ++i) {
        deep = "[\"neg\", " + deep + "]";
    }
    std::string error = ErrorOf(Mutate(R"(["c", 0])", deep));
    EXPECT_TRUE(llvm::StringRef(error).endswith("expression nested too deep"));
}

TEST(FirJson, FileRoundTrip)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    Module m = BuildEverything();
    std::string error;
    ASSERT_TRUE(WriteModuleFile(path, m, error));
    EXPECT_FALSE(llvm::sys::fs::exists(path + ".tmp"));  // 写完改名（X-01）
    Module back;
    ASSERT_TRUE(ReadModuleFile(path, back, error));
    EXPECT_EQ(Print(ModuleToJson(back)), Print(ModuleToJson(m)));
    llvm::SmallString<128> missing(dir);
    llvm::sys::path::append(missing, "missing.json");
    EXPECT_FALSE(ReadModuleFile(missing, back, error));
    EXPECT_TRUE(llvm::StringRef(error).startswith("cannot read "));
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}
