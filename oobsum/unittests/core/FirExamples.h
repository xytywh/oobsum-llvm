//===-- FirExamples.h - Shared FIR fixtures for unit tests ------*- C++ -*-===//
//
// 单元测试共用的 FIR：06- §5.2 的例子（JSON 原文）与"每种表达式 / 指令 / 类型各至少一次"的模块（SPEC-0005、SPEC-0006）。
//
//===----------------------------------------------------------------------===//

#ifndef OOBSUM_UNITTESTS_CORE_FIREXAMPLES_H
#define OOBSUM_UNITTESTS_CORE_FIREXAMPLES_H

#include "OobSummary/core/Fir.h"

namespace oobsum {
namespace fir {
namespace examples {

// 06- §5.2 的例子，逐字照抄
static const char *const kSpecExample = R"json(
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

// 每种表达式、指令、类型、可选键 / null 键各至少一次
inline Module BuildEverything()
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
    ExprId subobj = f.Add(Expr::SubObj(fld, 4));  // SPEC-0104

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
    call.args = {gaddr, str, cast, lnot, negx, bnot, unk, subobj};
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

}  // namespace examples
}  // namespace fir
}  // namespace oobsum

#endif
