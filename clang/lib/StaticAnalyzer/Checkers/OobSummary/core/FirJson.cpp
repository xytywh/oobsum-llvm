//===-- FirJson.cpp - FIR <-> JSON (oobsum-fir/1) ---------------*- C++ -*-===//
//
// 见 FirJson.h。读入按"先收集号、再读内容"的顺序：先读 files、记录号、局部号、块号，读内容时引用当场检查，
// 出错位置就是引用所在处。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/FirJson.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

namespace oobsum {
namespace fir {

using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

namespace {

const char *const kTypeKinds[] = {"int", "ptr", "arr", "rec", "void", "other"};
const char *const kInstOps[] = {"set", "load", "store", "call", "havoc", "ret"};
const char *const kExprOps[] = {"c",     "l",   "bin",   "cmp",  "lnot", "neg",  "bnot", "cast",  "laddr",
                                "gaddr", "str", "faddr", "padd", "fld",  "psub", "unk",  "subobj"};
constexpr unsigned kMaxExprDepth = 256;  // FIR 的表达式很浅（D-13）；防异常输入把栈打穿（A-96）

// ---- 写出 ----

Value TypeToJson(const Module &m, TypeId id)
{
    const Type &t = m.types[id];
    Object o{{"k", kTypeKinds[static_cast<unsigned>(t.kind)]}};
    switch (t.kind) {
        case TypeKind::Int:
            o["w"] = static_cast<int64_t>(t.width);
            o["s"] = t.isSigned;
            break;
        case TypeKind::Ptr:
            o["to"] = TypeToJson(m, t.to);
            break;
        case TypeKind::Arr:
            o["n"] = t.count < 0 ? Value(nullptr) : Value(t.count);
            o["elem"] = TypeToJson(m, t.to);
            break;
        case TypeKind::Rec:
            o["id"] = static_cast<int64_t>(t.record);
            break;
        case TypeKind::Void:
            break;
        case TypeKind::Other:
            o["size"] = t.size;
            break;
    }
    return std::move(o);
}

Value LocToJson(const Loc &loc)
{
    return Array{static_cast<int64_t>(loc.file), static_cast<int64_t>(loc.line), static_cast<int64_t>(loc.col)};
}

Value ExprToJson(const Function &f, ExprId id)
{
    const Expr &e = f.exprs[id];
    Array a{kExprOps[static_cast<unsigned>(e.op)]};
    switch (e.op) {
        case ExprOp::Const:
        case ExprOp::Local:
        case ExprOp::LAddr:
        case ExprOp::Str:
            a.push_back(e.value);
            break;
        case ExprOp::Bin:
            a.push_back(BinOpName(static_cast<BinOp>(e.sub)));
            a.push_back(ExprToJson(f, e.a));
            a.push_back(ExprToJson(f, e.b));
            if (e.width > 0) {
                a.push_back(static_cast<int64_t>(e.width));
                a.push_back(e.isSigned);
            }
            break;
        case ExprOp::Cmp:
            a.push_back(CmpOpName(static_cast<CmpOp>(e.sub)));
            a.push_back(ExprToJson(f, e.a));
            a.push_back(ExprToJson(f, e.b));
            a.push_back(e.isSigned);
            break;
        case ExprOp::LNot:
        case ExprOp::Neg:
        case ExprOp::BNot:
            a.push_back(ExprToJson(f, e.a));
            break;
        case ExprOp::Cast:
            a.push_back(static_cast<int64_t>(e.width));
            a.push_back(e.isSigned);
            a.push_back(ExprToJson(f, e.a));
            break;
        case ExprOp::GAddr:
        case ExprOp::FAddr:
        case ExprOp::Unk:
            a.push_back(e.text);
            break;
        case ExprOp::PAdd:
            a.push_back(ExprToJson(f, e.a));
            a.push_back(ExprToJson(f, e.b));
            break;
        case ExprOp::Fld:
            a.push_back(ExprToJson(f, e.a));
            a.push_back(e.value);
            a.push_back(e.size);
            a.push_back(e.text);
            break;
        case ExprOp::PSub:
            a.push_back(ExprToJson(f, e.a));
            a.push_back(ExprToJson(f, e.b));
            a.push_back(e.value);
            break;
        case ExprOp::SubObj:
            a.push_back(ExprToJson(f, e.a));
            a.push_back(e.size);
            break;
    }
    return std::move(a);
}

Value OptionalExpr(const Function &f, ExprId id)
{
    return id == kNone ? Value(nullptr) : ExprToJson(f, id);
}

Value InstToJson(const Function &f, const Inst &inst)
{
    Object o{{"op", kInstOps[static_cast<unsigned>(inst.op)]}, {"loc", LocToJson(inst.loc)}};
    switch (inst.op) {
        case InstOp::Set:
            o["dst"] = static_cast<int64_t>(inst.dst);
            o["val"] = ExprToJson(f, inst.val);
            break;
        case InstOp::Load:
            o["dst"] = static_cast<int64_t>(inst.dst);
            o["addr"] = ExprToJson(f, inst.addr);
            o["size"] = inst.size;
            break;
        case InstOp::Store:
            o["addr"] = ExprToJson(f, inst.addr);
            o["size"] = inst.size;
            o["val"] = OptionalExpr(f, inst.val);
            break;
        case InstOp::Call: {
            if (inst.dst != kNone) {
                o["dst"] = static_cast<int64_t>(inst.dst);
            }
            o["callee"] =
                inst.calleeExpr == kNone ? Value(inst.callee) : Value(Array{"ind", ExprToJson(f, inst.calleeExpr)});
            Array args;
            for (ExprId arg : inst.args) {
                args.push_back(ExprToJson(f, arg));
            }
            o["args"] = std::move(args);
            o["ord"] = static_cast<int64_t>(inst.ord);
            break;
        }
        case InstOp::Havoc:
            o["dst"] = static_cast<int64_t>(inst.dst);
            o["why"] = inst.why;
            break;
        case InstOp::Ret:
            o["val"] = OptionalExpr(f, inst.val);
            break;
    }
    if (inst.sub && (inst.op == InstOp::Load || inst.op == InstOp::Store)) {
        o["sub"] = true;
    }
    return std::move(o);
}

Value FunctionToJson(const Module &m, const Function &f)
{
    Array locals;
    for (const Local &local : f.locals) {
        Object o{{"id", static_cast<int64_t>(local.id)}, {"name", local.name}, {"t", TypeToJson(m, local.type)}};
        if (local.param >= 0) {
            o["param"] = static_cast<int64_t>(local.param);
        }
        if (local.obj) {
            o["obj"] = true;
        }
        if (local.tmp) {
            o["tmp"] = true;
        }
        locals.push_back(std::move(o));
    }
    Array blocks;
    for (const Block &block : f.blocks) {
        Array insts;
        for (const Inst &inst : block.insts) {
            insts.push_back(InstToJson(f, inst));
        }
        Array succ;
        for (const Edge &edge : block.succ) {
            Object e{{"to", static_cast<int64_t>(edge.to)}};
            if (edge.assume != kNone) {
                e["assume"] = ExprToJson(f, edge.assume);
            }
            succ.push_back(std::move(e));
        }
        blocks.push_back(
            Object{{"id", static_cast<int64_t>(block.id)}, {"insts", std::move(insts)}, {"succ", std::move(succ)}});
    }
    Object o{{"key", f.key},
             {"name", f.name},
             {"ext", f.ext},
             {"loc", LocToJson(f.loc)},
             {"ret", TypeToJson(m, f.ret)},
             {"locals", std::move(locals)},
             {"entry", static_cast<int64_t>(f.entry)},
             {"exit", static_cast<int64_t>(f.exit)},
             {"blocks", std::move(blocks)}};
    if (!f.flags.empty()) {
        Array flags;
        for (const std::string &flag : f.flags) {
            flags.push_back(flag);
        }
        o["flags"] = std::move(flags);
    }
    return std::move(o);
}

// ---- 读入 ----

class Reader {
public:
    Reader(Module &m, std::string &error) : m(m), error(error)
    {
    }

    bool ReadModule(const Value &v);

private:
    // 出错位置（funcs[0].blocks[1].insts[0].addr[2]）：进入一个键 / 下标时追加一段，lambda 返回时去掉
    template <typename F>
    bool In(llvm::StringRef key, F body)
    {
        size_t len = where.size();
        where += (where.empty() ? "" : ".") + key.str();
        bool ok = body();
        where.resize(len);
        return ok;
    }
    template <typename F>
    bool At(size_t index, F body)
    {
        size_t len = where.size();
        where += "[" + std::to_string(index) + "]";
        bool ok = body();
        where.resize(len);
        return ok;
    }

    bool Fail(const llvm::Twine &message)
    {
        if (error.empty()) {
            error = (where.empty() ? std::string("<root>") : where) + ": " + message.str();
        }
        return false;
    }

    // 对象：必有键都在、没有不认识的键（SPEC-0005 §2.3 #8）
    const Object *AsObject(const Value &v, llvm::ArrayRef<const char *> required,
                           llvm::ArrayRef<const char *> optional);
    const Array *AsArray(const Value &v);
    bool ReadInt(const Value &v, int64_t &out, int64_t min = INT64_MIN, int64_t max = INT64_MAX);
    bool ReadU32(const Value &v, uint32_t &out);
    bool ReadBool(const Value &v, bool &out);
    bool ReadString(const Value &v, std::string &out);
    bool ReadStrings(const Value &v, std::vector<std::string> &out);

    bool ReadType(const Value &v, TypeId &out);
    bool ReadLoc(const Value &v, Loc &out);
    bool ReadLocalRef(const Value &v, uint32_t &out);
    bool ReadBlockRef(const Value &v, uint32_t &out);
    bool ReadExpr(const Value &v, Function &f, ExprId &out, unsigned depth = 0);
    bool ReadInst(const Value &v, Function &f, Inst &out);
    bool ReadLocals(const Value &v, Function &out);
    bool ReadBlocks(const Value &v, Function &out);
    bool ReadFunction(const Value &v, Function &out);
    bool ReadRecord(const Value &v, Record &out);
    bool ReadGlobal(const Value &v, Global &out);
    // 先收集号再读内容：引用可以指向数组里后面的元素
    bool CollectIds(const Array &items, const char *what, std::set<uint32_t> &ids);

    Module &m;
    std::string &error;
    std::string where;
    std::set<uint32_t> recordIds;
    std::set<uint32_t> localIds;  // 当前函数
    std::set<uint32_t> blockIds;  // 当前函数
};

const Object *Reader::AsObject(const Value &v, llvm::ArrayRef<const char *> required,
                               llvm::ArrayRef<const char *> optional)
{
    const Object *o = v.getAsObject();
    if (o == nullptr) {
        Fail("expected an object");
        return nullptr;
    }
    for (const char *key : required) {
        if (o->get(key) == nullptr) {
            Fail(llvm::Twine("missing key '") + key + "'");
            return nullptr;
        }
    }
    // 按字母序报第一个不认识的键（Object 的遍历顺序不确定，错误信息要稳定）
    std::set<std::string> unknown;
    for (const auto &entry : *o) {
        llvm::StringRef key = entry.first;
        if (!llvm::is_contained(required, key) && !llvm::is_contained(optional, key)) {
            unknown.insert(key.str());
        }
    }
    if (!unknown.empty()) {
        Fail("unknown key '" + *unknown.begin() + "'");
        return nullptr;
    }
    return o;
}

const Array *Reader::AsArray(const Value &v)
{
    const Array *a = v.getAsArray();
    if (a == nullptr) {
        Fail("expected an array");
    }
    return a;
}

bool Reader::ReadInt(const Value &v, int64_t &out, int64_t min, int64_t max)
{
    llvm::Optional<int64_t> n = v.getAsInteger();
    // LLVM 14 的 json::parse 把超出 int64 的整数悄悄饱和成 INT64_MAX / INT64_MIN（实测，SPEC-0005 §2.3 #5），
    // 读回来分不清，所以这两个值本身也拒绝；降级遇到这么大的常量写 ["unk", "const_overflow"]
    if (!n || *n == INT64_MAX || *n == INT64_MIN) {
        return Fail("expected an integer strictly inside the int64 range");
    }
    if (*n < min || *n > max) {
        return Fail("integer " + llvm::Twine(*n) + " out of range");
    }
    out = *n;
    return true;
}

bool Reader::ReadU32(const Value &v, uint32_t &out)
{
    int64_t n = 0;
    if (!ReadInt(v, n, 0, static_cast<int64_t>(kNone) - 1)) {
        return false;
    }
    out = static_cast<uint32_t>(n);
    return true;
}

bool Reader::ReadBool(const Value &v, bool &out)
{
    llvm::Optional<bool> b = v.getAsBoolean();
    if (!b) {
        return Fail("expected true / false");
    }
    out = *b;
    return true;
}

bool Reader::ReadString(const Value &v, std::string &out)
{
    llvm::Optional<llvm::StringRef> s = v.getAsString();
    if (!s) {
        return Fail("expected a string");
    }
    out = s->str();
    return true;
}

bool Reader::ReadStrings(const Value &v, std::vector<std::string> &out)
{
    const Array *a = AsArray(v);
    if (a == nullptr) {
        return false;
    }
    for (size_t i = 0; i < a->size(); ++i) {
        std::string text;
        if (!At(i, [&] { return ReadString((*a)[i], text); })) {
            return false;
        }
        out.push_back(text);
    }
    return true;
}

bool Reader::ReadType(const Value &v, TypeId &out)
{
    const Object *probe = v.getAsObject();
    llvm::Optional<llvm::StringRef> k = probe != nullptr ? probe->getString("k") : llvm::None;
    if (!k) {
        return Fail("expected a type object with a string 'k'");
    }
    Type t;
    const Object *o = nullptr;
    if (*k == "int") {
        int64_t w = 0;
        bool isSigned = false;
        if (!(o = AsObject(v, {"k", "w", "s"}, {})) || !In("w", [&] { return ReadInt(*o->get("w"), w, 1, 128); }) ||
            !In("s", [&] { return ReadBool(*o->get("s"), isSigned); })) {
            return false;
        }
        t = Type::Int(static_cast<unsigned>(w), isSigned);
    } else if (*k == "ptr") {
        TypeId to = kNone;
        if (!(o = AsObject(v, {"k", "to"}, {})) || !In("to", [&] { return ReadType(*o->get("to"), to); })) {
            return false;
        }
        t = Type::Ptr(to);
    } else if (*k == "arr") {
        int64_t n = -1;
        TypeId elem = kNone;
        if (!(o = AsObject(v, {"k", "n", "elem"}, {})) ||
            !In("n", [&] { return o->get("n")->getAsNull() || ReadInt(*o->get("n"), n, 0); }) ||
            !In("elem", [&] { return ReadType(*o->get("elem"), elem); })) {
            return false;
        }
        t = Type::Arr(elem, n);
    } else if (*k == "rec") {
        uint32_t id = 0;
        if (!(o = AsObject(v, {"k", "id"}, {})) || !In("id", [&] {
                return ReadU32(*o->get("id"), id) &&
                       (recordIds.count(id) != 0 || Fail("unknown record id " + llvm::Twine(id)));
            })) {
            return false;
        }
        t = Type::Rec(id);
    } else if (*k == "void") {
        if (!AsObject(v, {"k"}, {})) {
            return false;
        }
        t = Type::Void();
    } else if (*k == "other") {
        int64_t size = 0;
        if (!(o = AsObject(v, {"k", "size"}, {})) || !In("size", [&] { return ReadInt(*o->get("size"), size, 0); })) {
            return false;
        }
        t = Type::Other(size);
    } else {
        return In("k", [&] { return Fail("unknown type kind '" + *k + "'"); });
    }
    out = m.Intern(t);
    return true;
}

bool Reader::ReadLoc(const Value &v, Loc &out)
{
    const Array *a = AsArray(v);
    if (a == nullptr) {
        return false;
    }
    if (a->size() != 3) {
        return Fail("expected [file, line, col]");
    }
    int64_t line = 0;
    int64_t col = 0;
    if (!At(0, [&] { return ReadU32((*a)[0], out.file); }) ||
        !At(1, [&] { return ReadInt((*a)[1], line, 0, UINT32_MAX); }) ||
        !At(2, [&] { return ReadInt((*a)[2], col, 0, UINT32_MAX); })) {
        return false;
    }
    if (out.file >= m.files.size()) {
        return At(0, [&] {
            return Fail("file index " + llvm::Twine(out.file) + " out of range (" + llvm::Twine(m.files.size()) +
                        " files)");
        });
    }
    out.line = static_cast<unsigned>(line);
    out.col = static_cast<unsigned>(col);
    return true;
}

bool Reader::ReadLocalRef(const Value &v, uint32_t &out)
{
    return ReadU32(v, out) && (localIds.count(out) != 0 || Fail("unknown local id " + llvm::Twine(out)));
}

bool Reader::ReadBlockRef(const Value &v, uint32_t &out)
{
    return ReadU32(v, out) && (blockIds.count(out) != 0 || Fail("unknown block id " + llvm::Twine(out)));
}

bool Reader::ReadExpr(const Value &v, Function &f, ExprId &out, unsigned depth)
{
    if (depth > kMaxExprDepth) {
        return Fail("expression nested too deep");
    }
    const Array *a = AsArray(v);
    if (a == nullptr) {
        return false;
    }
    llvm::Optional<llvm::StringRef> name = a->empty() ? llvm::None : (*a)[0].getAsString();
    if (!name) {
        return Fail("expected [op, ...] with a string op");
    }
    // 元素个数（含 op）；bin 是 4（数学整数）或 6（带宽度与符号，SPEC-0005 §2.3 #1）
    static const size_t arity[] = {2, 2, 4, 5, 2, 2, 2, 4, 2, 2, 2, 2, 3, 5, 4, 2, 3};
    size_t index = 0;
    while (index < llvm::array_lengthof(kExprOps) && *name != kExprOps[index]) {
        ++index;
    }
    if (index == llvm::array_lengthof(kExprOps)) {
        return Fail("unknown expression op '" + *name + "'");
    }
    Expr e;
    e.op = static_cast<ExprOp>(index);
    bool sizeOk = a->size() == arity[index] || (e.op == ExprOp::Bin && a->size() == 6);
    if (!sizeOk) {
        return Fail("'" + *name + "' with " + llvm::Twine(a->size()) + " elements");
    }
    auto expr = [&](size_t i, ExprId &into) { return At(i, [&] { return ReadExpr((*a)[i], f, into, depth + 1); }); };
    auto integer = [&](size_t i, int64_t &into, int64_t min) {
        return At(i, [&] { return ReadInt((*a)[i], into, min); });
    };
    auto boolean = [&](size_t i, bool &into) { return At(i, [&] { return ReadBool((*a)[i], into); }); };
    auto text = [&](size_t i, std::string &into) { return At(i, [&] { return ReadString((*a)[i], into); }); };
    auto width = [&](size_t i) {
        int64_t w = 0;
        bool ok = At(i, [&] { return ReadInt((*a)[i], w, 1, 128); });
        e.width = static_cast<unsigned>(w);
        return ok;
    };
    bool ok = true;
    switch (e.op) {
        case ExprOp::Const:
            ok = integer(1, e.value, INT64_MIN);
            break;
        case ExprOp::Local:
        case ExprOp::LAddr: {
            uint32_t id = 0;
            ok = At(1, [&] { return ReadLocalRef((*a)[1], id); });
            e.value = id;
            break;
        }
        case ExprOp::Str:
            ok = integer(1, e.value, 0);
            break;
        case ExprOp::Bin:
        case ExprOp::Cmp: {
            std::string op;
            ok = text(1, op) && At(1, [&] {
                     unsigned n = e.op == ExprOp::Bin ? 10 : 6;
                     for (unsigned i = 0; i < n; ++i) {
                         llvm::StringRef candidate =
                             e.op == ExprOp::Bin ? BinOpName(static_cast<BinOp>(i)) : CmpOpName(static_cast<CmpOp>(i));
                         if (candidate == op) {
                             e.sub = static_cast<uint8_t>(i);
                             return true;
                         }
                     }
                     return Fail("unknown " + *name + " op '" + op + "'");
                 });
            ok = ok && expr(2, e.a) && expr(3, e.b);
            if (e.op == ExprOp::Cmp) {
                ok = ok && boolean(4, e.isSigned);
            } else if (a->size() == 6) {
                ok = ok && width(4) && boolean(5, e.isSigned);
            }
            break;
        }
        case ExprOp::LNot:
        case ExprOp::Neg:
        case ExprOp::BNot:
            ok = expr(1, e.a);
            break;
        case ExprOp::Cast:
            ok = width(1) && boolean(2, e.isSigned) && expr(3, e.a);
            break;
        case ExprOp::GAddr:
        case ExprOp::FAddr:
        case ExprOp::Unk:
            ok = text(1, e.text);
            break;
        case ExprOp::PAdd:
            ok = expr(1, e.a) && expr(2, e.b);
            break;
        case ExprOp::Fld:
            ok = expr(1, e.a) && integer(2, e.value, 0) && integer(3, e.size, 0) && text(4, e.text);
            break;
        case ExprOp::PSub:
            ok = expr(1, e.a) && expr(2, e.b) && integer(3, e.value, 1);
            break;
        case ExprOp::SubObj:
            ok = expr(1, e.a) && integer(2, e.size, 1);
            break;
    }
    if (!ok) {
        return false;
    }
    out = f.Add(std::move(e));
    return true;
}

bool Reader::ReadInst(const Value &v, Function &f, Inst &out)
{
    const Object *probe = v.getAsObject();
    llvm::Optional<llvm::StringRef> op = probe != nullptr ? probe->getString("op") : llvm::None;
    if (!op) {
        return Fail("expected an instruction object with a string 'op'");
    }
    const Object *o = nullptr;
    if (*op == "set") {
        out.op = InstOp::Set;
        o = AsObject(v, {"op", "dst", "val", "loc"}, {});
    } else if (*op == "load") {
        out.op = InstOp::Load;
        o = AsObject(v, {"op", "dst", "addr", "size", "loc"}, {"sub"});
    } else if (*op == "store") {
        out.op = InstOp::Store;
        o = AsObject(v, {"op", "addr", "size", "val", "loc"}, {"sub"});
    } else if (*op == "call") {
        out.op = InstOp::Call;
        o = AsObject(v, {"op", "callee", "args", "ord", "loc"}, {"dst"});
    } else if (*op == "havoc") {
        out.op = InstOp::Havoc;
        o = AsObject(v, {"op", "dst", "why", "loc"}, {});
    } else if (*op == "ret") {
        out.op = InstOp::Ret;
        o = AsObject(v, {"op", "val", "loc"}, {});
    } else {
        return In("op", [&] { return Fail("unknown instruction op '" + *op + "'"); });
    }
    if (o == nullptr || !In("loc", [&] { return ReadLoc(*o->get("loc"), out.loc); })) {
        return false;
    }
    const Value *dst = o->get("dst");
    const Value *addr = o->get("addr");
    const Value *size = o->get("size");
    const Value *sub = o->get("sub");
    const Value *val = o->get("val");
    if ((dst && !In("dst", [&] { return ReadLocalRef(*dst, out.dst); })) ||
        (addr && !In("addr", [&] { return ReadExpr(*addr, f, out.addr); })) ||
        (size && !In("size", [&] { return ReadInt(*size, out.size, 0); })) ||
        (sub && !In("sub", [&] { return ReadBool(*sub, out.sub); }))) {
        return false;
    }
    // set 的 val 必有；store / ret 的 val 可为 null（SPEC-0005 §2.3 #7）
    if (val && !In("val", [&] { return (out.op != InstOp::Set && val->getAsNull()) || ReadExpr(*val, f, out.val); })) {
        return false;
    }
    if (out.op == InstOp::Havoc && !In("why", [&] { return ReadString(*o->get("why"), out.why); })) {
        return false;
    }
    if (out.op != InstOp::Call) {
        return true;
    }
    const Array *args = nullptr;
    return In("callee",
              [&] {
                  const Value &callee = *o->get("callee");
                  const Array *ind = callee.getAsArray();
                  if (ind == nullptr) {
                      return ReadString(callee, out.callee);
                  }
                  if (ind->size() != 2 || (*ind)[0].getAsString() != llvm::Optional<llvm::StringRef>("ind")) {
                      return Fail("expected a function key or [\"ind\", E]");
                  }
                  return At(1, [&] { return ReadExpr((*ind)[1], f, out.calleeExpr); });
              }) &&
           In("args",
              [&] {
                  if (!(args = AsArray(*o->get("args")))) {
                      return false;
                  }
                  for (size_t i = 0; i < args->size(); ++i) {
                      ExprId arg = kNone;
                      if (!At(i, [&] { return ReadExpr((*args)[i], f, arg); })) {
                          return false;
                      }
                      out.args.push_back(arg);
                  }
                  return true;
              }) &&
           In("ord", [&] { return ReadU32(*o->get("ord"), out.ord); });
}

bool Reader::CollectIds(const Array &items, const char *what, std::set<uint32_t> &ids)
{
    ids.clear();
    for (size_t i = 0; i < items.size(); ++i) {
        bool ok = At(i, [&] {
            const Object *o = items[i].getAsObject();
            if (o == nullptr || o->get("id") == nullptr) {
                return Fail(llvm::Twine("expected a ") + what + " object with 'id'");
            }
            uint32_t id = 0;
            return In("id", [&] {
                return ReadU32(*o->get("id"), id) &&
                       (ids.insert(id).second || Fail(llvm::Twine("duplicate ") + what + " id " + llvm::Twine(id)));
            });
        });
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool Reader::ReadLocals(const Value &v, Function &out)
{
    const Array *locals = AsArray(v);
    if (locals == nullptr || !CollectIds(*locals, "local", localIds)) {
        return false;
    }
    std::set<int64_t> params;
    for (size_t i = 0; i < locals->size(); ++i) {
        Local local;
        int64_t p = -1;
        bool ok = At(i, [&] {
            const Object *o = AsObject((*locals)[i], {"id", "name", "t"}, {"param", "obj", "tmp"});
            if (o == nullptr) {
                return false;
            }
            const Value *param = o->get("param");
            const Value *obj = o->get("obj");
            const Value *tmp = o->get("tmp");
            return In("id", [&] { return ReadU32(*o->get("id"), local.id); }) &&
                   In("name", [&] { return ReadString(*o->get("name"), local.name); }) &&
                   In("t", [&] { return ReadType(*o->get("t"), local.type); }) &&
                   (!param || In("param",
                                 [&] {
                                     return ReadInt(*param, p, 0, 1 << 16) &&
                                            (params.insert(p).second ||
                                             Fail("duplicate param index " + llvm::Twine(p)));
                                 })) &&
                   (!obj || In("obj", [&] { return ReadBool(*obj, local.obj); })) &&
                   (!tmp || In("tmp", [&] { return ReadBool(*tmp, local.tmp); }));
        });
        if (!ok) {
            return false;
        }
        local.param = static_cast<int>(p);
        out.locals.push_back(std::move(local));
    }
    return true;
}

bool Reader::ReadBlocks(const Value &v, Function &out)
{
    const Array *blocks = AsArray(v);
    if (blocks == nullptr || !CollectIds(*blocks, "block", blockIds)) {
        return false;
    }
    for (size_t i = 0; i < blocks->size(); ++i) {
        Block block;
        bool ok = At(i, [&] {
            const Object *o = AsObject((*blocks)[i], {"id", "insts", "succ"}, {});
            if (o == nullptr || !In("id", [&] { return ReadU32(*o->get("id"), block.id); })) {
                return false;
            }
            return In("insts",
                      [&] {
                          const Array *insts = AsArray(*o->get("insts"));
                          if (insts == nullptr) {
                              return false;
                          }
                          for (size_t j = 0; j < insts->size(); ++j) {
                              Inst inst;
                              if (!At(j, [&] { return ReadInst((*insts)[j], out, inst); })) {
                                  return false;
                              }
                              block.insts.push_back(std::move(inst));
                          }
                          return true;
                      }) &&
                   In("succ", [&] {
                       const Array *succ = AsArray(*o->get("succ"));
                       if (succ == nullptr) {
                           return false;
                       }
                       for (size_t j = 0; j < succ->size(); ++j) {
                           Edge edge;
                           bool edgeOk = At(j, [&] {
                               const Object *eo = AsObject((*succ)[j], {"to"}, {"assume"});
                               const Value *assume = eo != nullptr ? eo->get("assume") : nullptr;
                               return eo != nullptr &&
                                      In("to", [&] { return ReadBlockRef(*eo->get("to"), edge.to); }) &&
                                      (!assume || In("assume", [&] { return ReadExpr(*assume, out, edge.assume); }));
                           });
                           if (!edgeOk) {
                               return false;
                           }
                           block.succ.push_back(edge);
                       }
                       return true;
                   });
        });
        if (!ok) {
            return false;
        }
        out.blocks.push_back(std::move(block));
    }
    return true;
}

bool Reader::ReadFunction(const Value &v, Function &out)
{
    const Object *o = AsObject(v, {"key", "name", "ext", "loc", "ret", "locals", "entry", "exit", "blocks"}, {"flags"});
    if (o == nullptr) {
        return false;
    }
    const Value *flags = o->get("flags");
    // locals 先于 blocks（指令引用局部）；entry / exit 在 blocks 之后（引用块号）
    return In("key", [&] { return ReadString(*o->get("key"), out.key); }) &&
           In("name", [&] { return ReadString(*o->get("name"), out.name); }) &&
           In("ext", [&] { return ReadBool(*o->get("ext"), out.ext); }) &&
           In("loc", [&] { return ReadLoc(*o->get("loc"), out.loc); }) &&
           In("ret", [&] { return ReadType(*o->get("ret"), out.ret); }) &&
           In("locals", [&] { return ReadLocals(*o->get("locals"), out); }) &&
           In("blocks", [&] { return ReadBlocks(*o->get("blocks"), out); }) &&
           In("entry", [&] { return ReadBlockRef(*o->get("entry"), out.entry); }) &&
           In("exit", [&] { return ReadBlockRef(*o->get("exit"), out.exit); }) &&
           (!flags || In("flags", [&] { return ReadStrings(*flags, out.flags); }));
}

bool Reader::ReadRecord(const Value &v, Record &out)
{
    const Object *o = AsObject(v, {"id", "name", "size", "fields"}, {});
    return o != nullptr && In("id", [&] { return ReadU32(*o->get("id"), out.id); }) &&
           In("name", [&] { return ReadString(*o->get("name"), out.name); }) &&
           In("size", [&] { return ReadInt(*o->get("size"), out.size, 0); }) && In("fields", [&] {
               const Array *fields = AsArray(*o->get("fields"));
               if (fields == nullptr) {
                   return false;
               }
               for (size_t i = 0; i < fields->size(); ++i) {
                   Field field;
                   bool ok = At(i, [&] {
                       const Object *fo = AsObject((*fields)[i], {"name", "off", "t"}, {});
                       return fo != nullptr && In("name", [&] { return ReadString(*fo->get("name"), field.name); }) &&
                              In("off", [&] { return ReadInt(*fo->get("off"), field.off, 0); }) &&
                              In("t", [&] { return ReadType(*fo->get("t"), field.type); });
                   });
                   if (!ok) {
                       return false;
                   }
                   out.fields.push_back(std::move(field));
               }
               return true;
           });
}

bool Reader::ReadGlobal(const Value &v, Global &out)
{
    const Object *o = AsObject(v, {"key", "name", "def", "t", "loc"}, {});
    return o != nullptr && In("key", [&] { return ReadString(*o->get("key"), out.key); }) &&
           In("name", [&] { return ReadString(*o->get("name"), out.name); }) &&
           In("def", [&] { return ReadBool(*o->get("def"), out.def); }) &&
           In("t", [&] { return ReadType(*o->get("t"), out.type); }) &&
           In("loc", [&] { return ReadLoc(*o->get("loc"), out.loc); });
}

bool Reader::ReadModule(const Value &v)
{
    const Object *o = AsObject(v, {"schema", "tu", "file", "target", "files", "records", "globals", "funcs"}, {});
    if (o == nullptr) {
        return false;
    }
    std::string schema;
    // files 先于一切 loc；记录号先于一切类型
    return In("schema",
              [&] {
                  return ReadString(*o->get("schema"), schema) &&
                         (schema == kFirSchema ||
                          Fail("expected '" + llvm::Twine(kFirSchema) + "', got '" + schema + "'"));
              }) &&
           In("tu", [&] { return ReadString(*o->get("tu"), m.tu); }) &&
           In("file", [&] { return ReadString(*o->get("file"), m.file); }) &&
           In("target",
              [&] {
                  const Object *t = AsObject(*o->get("target"), {"triple", "ptr", "long", "big_endian"}, {});
                  uint32_t ptr = 0;
                  uint32_t longSize = 0;
                  bool ok = t != nullptr &&
                            In("triple", [&] { return ReadString(*t->get("triple"), m.target.triple); }) &&
                            In("ptr", [&] { return ReadU32(*t->get("ptr"), ptr); }) &&
                            In("long", [&] { return ReadU32(*t->get("long"), longSize); }) &&
                            In("big_endian", [&] { return ReadBool(*t->get("big_endian"), m.target.bigEndian); });
                  m.target.ptr = ptr;
                  m.target.longSize = longSize;
                  return ok;
              }) &&
           In("files", [&] { return ReadStrings(*o->get("files"), m.files); }) &&
           In("records",
              [&] {
                  const Array *records = AsArray(*o->get("records"));
                  if (records == nullptr || !CollectIds(*records, "record", recordIds)) {
                      return false;
                  }
                  for (size_t i = 0; i < records->size(); ++i) {
                      Record record;
                      if (!At(i, [&] { return ReadRecord((*records)[i], record); })) {
                          return false;
                      }
                      m.records.push_back(std::move(record));
                  }
                  return true;
              }) &&
           In("globals",
              [&] {
                  const Array *globals = AsArray(*o->get("globals"));
                  if (globals == nullptr) {
                      return false;
                  }
                  for (size_t i = 0; i < globals->size(); ++i) {
                      Global global;
                      if (!At(i, [&] { return ReadGlobal((*globals)[i], global); })) {
                          return false;
                      }
                      m.globals.push_back(std::move(global));
                  }
                  return true;
              }) &&
           In("funcs", [&] {
               const Array *funcs = AsArray(*o->get("funcs"));
               if (funcs == nullptr) {
                   return false;
               }
               for (size_t i = 0; i < funcs->size(); ++i) {
                   Function f;
                   if (!At(i, [&] { return ReadFunction((*funcs)[i], f); })) {
                       return false;
                   }
                   m.funcs.push_back(std::move(f));
               }
               return true;
           });
}

}  // namespace

llvm::json::Value ModuleToJson(const Module &module)
{
    Array files;
    for (const std::string &file : module.files) {
        files.push_back(file);
    }
    Array records;
    for (const Record &record : module.records) {
        Array fields;
        for (const Field &field : record.fields) {
            fields.push_back(Object{{"name", field.name}, {"off", field.off}, {"t", TypeToJson(module, field.type)}});
        }
        records.push_back(Object{{"id", static_cast<int64_t>(record.id)},
                                 {"name", record.name},
                                 {"size", record.size},
                                 {"fields", std::move(fields)}});
    }
    Array globals;
    for (const Global &global : module.globals) {
        globals.push_back(Object{{"key", global.key},
                                 {"name", global.name},
                                 {"def", global.def},
                                 {"t", TypeToJson(module, global.type)},
                                 {"loc", LocToJson(global.loc)}});
    }
    Array funcs;
    for (const Function &f : module.funcs) {
        funcs.push_back(FunctionToJson(module, f));
    }
    return Object{{"schema", kFirSchema},
                  {"tu", module.tu},
                  {"file", module.file},
                  {"target", Object{{"triple", module.target.triple},
                                    {"ptr", static_cast<int64_t>(module.target.ptr)},
                                    {"long", static_cast<int64_t>(module.target.longSize)},
                                    {"big_endian", module.target.bigEndian}}},
                  {"files", std::move(files)},
                  {"records", std::move(records)},
                  {"globals", std::move(globals)},
                  {"funcs", std::move(funcs)}};
}

bool ModuleFromJson(const llvm::json::Value &value, Module &out, std::string &error)
{
    error.clear();
    out = Module();
    Reader reader(out, error);
    return reader.ReadModule(value);
}

bool WriteModuleFile(llvm::StringRef path, const Module &module, std::string &error)
{
    std::string tmp = (path + ".tmp").str();
    {
        std::error_code ec;
        llvm::raw_fd_ostream os(tmp, ec, llvm::sys::fs::OF_Text);
        if (ec) {
            error = "cannot write " + tmp + ": " + ec.message();
            return false;
        }
        os << llvm::formatv("{0:1}", ModuleToJson(module)) << "\n";  // 缩进写，人能读（SPEC-0114）
        os.close();
        if (os.has_error()) {
            error = "cannot write " + tmp + ": " + os.error().message();
            os.clear_error();
            return false;
        }
    }
    if (std::error_code ec = llvm::sys::fs::rename(tmp, path)) {
        error = "cannot rename " + tmp + " to " + path.str() + ": " + ec.message();
        return false;
    }
    return true;
}

bool ReadModuleFile(llvm::StringRef path, Module &out, std::string &error)
{
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer) {
        error = "cannot read " + path.str() + ": " + buffer.getError().message();
        return false;
    }
    llvm::Expected<llvm::json::Value> value = llvm::json::parse((*buffer)->getBuffer());
    if (!value) {
        error = path.str() + ": " + llvm::toString(value.takeError());
        return false;
    }
    if (!ModuleFromJson(*value, out, error)) {
        error = path.str() + ": " + error;
        return false;
    }
    return true;
}

}  // namespace fir
}  // namespace oobsum
