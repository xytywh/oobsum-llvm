//===-- Summary.cpp - Function summaries (oobsum-summary/1) -----*- C++ -*-===//
//
// 见 Summary.h。校验规则对照 replay.py 的 check_shape 与 summary.schema.json（SPEC-0012 §2.1）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Summary.h"
#include "OobSummary/core/AtomicFile.h"

#include "OobSummary/core/LinExpr.h"
#include "OobSummary/core/Range.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <set>

namespace oobsum {
namespace summary {

using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

namespace {

// D-27 的账本原因（同 replay.py 的 REASONS）
const std::set<std::string> &Reasons()
{
    static const std::set<std::string> reasons = {"fptr",
                                                  "cxx_call",  // SPEC-0121
                                                  "unknown_callee",
                                                  "recursion",
                                                  "msg_edge",
                                                  "global_value",
                                                  "global_ptr_target",
                                                  "field_written_by_callee",
                                                  "alloc_size_unknown",
                                                  "flexible_array",
                                                  "struct_overlay",
                                                  "pointer_stepping",
                                                  "string_length",
                                                  "nonlinear",
                                                  "type_range_only",
                                                  "entry_unproven",
                                                  "cond_truncated",
                                                  "typed_field",
                                                  "subobject_overflow",  // SPEC-0104
                                                  "alias_unverified",    // SPEC-0106
                                                  "ambiguous_callee",
                                                  "not_exported",
                                                  "not_in_tu",
                                                  "cfg_too_large",
                                                  "timeout",
                                                  "iter_limit",
                                                  "unsupported_construct",
                                                  "struct_by_value",
                                                  "inline_asm",
                                                  "imprecise_join",
                                                  "untracked_pointer"};
    return reasons;
}

// 对象写法（同 replay.py 的 OBJ）：g:名 | local:名 | heap@位置 | *pK | *pK->f | pK->f；via：g:名.字段 | local:名.字段
bool IsObj(llvm::StringRef s)
{
    static llvm::Regex re("^((g|local):[A-Za-z0-9_]+|heap@[A-Za-z0-9_./-]+:[0-9]+|\\*?p[0-9]+(->[A-Za-z0-9_]+)?)$");
    return re.match(s);
}
bool IsVia(llvm::StringRef s)
{
    static llvm::Regex re("^(g|local):[A-Za-z0-9_]+\\.[A-Za-z0-9_]+$");
    return re.match(s);
}

class Reader {
public:
    explicit Reader(std::string &error) : error(error)
    {
    }

    bool ReadSummary(const Value &v, Summary &s);

private:
    // ---- 位置与失败 ----
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
    bool At(size_t i, F body)
    {
        size_t len = where.size();
        where += "[" + std::to_string(i) + "]";
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

    // ---- 基本类型 ----
    const Object *Obj(const Value &v, llvm::ArrayRef<const char *> required, llvm::ArrayRef<const char *> optional)
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
    bool Str(const Value &v, std::string &out)
    {
        llvm::Optional<llvm::StringRef> s = v.getAsString();
        if (!s) {
            return Fail("expected a string");
        }
        out = s->str();
        return true;
    }
    bool Int(const Value &v, int64_t &out)
    {
        llvm::Optional<int64_t> n = v.getAsInteger();
        if (!n || *n == INT64_MAX || *n == INT64_MIN) {  // SPEC-0005 §2.3 #5：LLVM 14 的 json::parse 会把大数饱和
            return Fail("expected an integer strictly inside the int64 range");
        }
        out = *n;
        return true;
    }
    bool Bool(const Value &v, bool &out)
    {
        llvm::Optional<bool> b = v.getAsBoolean();
        if (!b) {
            return Fail("expected true / false");
        }
        out = *b;
        return true;
    }
    bool Expr(const Value &v, std::string &out)
    {
        LinExpr e;
        std::string why;
        return Str(v, out) && (ParseLin(out, e, why) || Fail("bad expression '" + out + "': " + why));
    }
    bool Cond(const Value &v, std::string &out)
    {
        std::vector<LinExpr> le0;
        std::string why;
        return Str(v, out) && (ParseCond(out, le0, why) || Fail("bad condition '" + out + "': " + why));
    }
    bool TagName(const Value &v, std::string &out)
    {
        return Str(v, out) && (ParseTag(out).hasValue() || Fail("unknown bound tag '" + out + "'"));
    }
    bool ObjName(const Value &v, std::string &out)
    {
        return Str(v, out) && (IsObj(out) || Fail("bad object '" + out + "'"));
    }
    bool OneOf(const Value &v, std::string &out, llvm::ArrayRef<const char *> allowed)
    {
        return Str(v, out) &&
               (llvm::is_contained(allowed, llvm::StringRef(out)) || Fail("unexpected value '" + out + "'"));
    }
    bool ReasonName(const Value &v, std::string &out)
    {
        return Str(v, out) && (Reasons().count(out) != 0 || Fail("unknown ledger reason '" + out + "'"));
    }
    template <typename T, typename F>
    bool List(const Value &v, std::vector<T> &out, F element)
    {
        const Array *a = v.getAsArray();
        if (a == nullptr) {
            return Fail("expected an array");
        }
        for (size_t i = 0; i < a->size(); ++i) {
            T item;
            if (!At(i, [&] { return element((*a)[i], item); })) {
                return false;
            }
            out.push_back(std::move(item));
        }
        return true;
    }
    bool Pair(const Value &v, std::string &lo, std::string &hi)
    {
        const Array *a = v.getAsArray();
        if (a == nullptr || a->size() != 2) {
            return Fail("expected [lo, hi]");
        }
        return At(0, [&] { return Expr((*a)[0], lo); }) && At(1, [&] { return Expr((*a)[1], hi); });
    }

    // 对象里的一个键：必有 / 可选
    template <typename T, typename F>
    bool Key(const Object &o, llvm::StringRef key, T &out, F read)
    {
        return In(key, [&] { return read(*o.get(key), out); });
    }
    template <typename T, typename F>
    bool OptKey(const Object &o, llvm::StringRef key, llvm::Optional<T> &out, F read)
    {
        if (o.get(key) == nullptr) {
            return true;
        }
        T value{};  // 值初始化：T 是 bool 时 GCC 的 -Wmaybe-uninitialized
        if (!Key(o, key, value, read)) {
            return false;
        }
        out = std::move(value);
        return true;
    }

    // ---- 各部分 ----
    bool ReadAccess(const Value &v, Access &a);
    bool ReadReq(const Value &v, Req &r);
    bool ReadEff(const Value &v, Eff &e);
    bool ReadArg(const Value &v, ArgValue &a, bool allowNull);
    bool ReadPtr(const Value &v, PtrArg &p);
    bool ReadApply(const Value &v, Apply &a);
    bool ReadResult(const Value &v, Result &r);
    bool ReadCall(const Value &v, Call &c);
    bool ReadRep(const Value &v, Rep &r);
    bool ReadOpen(const Value &v, Open &o);

    std::string &error;
    std::string where;
};

#define OOBSUM_M(fn) [&](const Value &x, auto &y) { return fn(x, y); }

bool Reader::ReadAccess(const Value &v, Access &a)
{
    const Object *o = Obj(v, {"fn", "key", "at", "end"}, {});
    return o != nullptr && Key(*o, "fn", a.fn, OOBSUM_M(Str)) && Key(*o, "key", a.key, OOBSUM_M(Str)) &&
           Key(*o, "at", a.at, OOBSUM_M(Str)) && Key(*o, "end", a.end, OOBSUM_M(Expr));
}

bool Reader::ReadReq(const Value &v, Req &r)
{
    const Object *o =
        Obj(v, {"id", "kind", "have", "obj", "end", "bound", "guard", "access", "path", "depth", "eff_depth", "blind"},
            {"start", "absorbed", "dir"});
    auto kind = [&](const Value &x, std::string &y) { return OneOf(x, y, {"R", "W"}); };
    llvm::Optional<std::string> dir;  // SPEC-0039
    if (o != nullptr && !OptKey(*o, "dir", dir, [&](const Value &x, std::string &y) {
            return OneOf(x, y, {"back", "sub", "subback"});
        })) {
        return false;
    }
    r.dir = dir.getValueOr("");
    auto absorbed = [&](const Value &x, Absorbed &y) {
        const Object *ao = Obj(x, {"kind", "end", "at"}, {});
        return ao != nullptr && Key(*ao, "kind", y.kind, kind) &&
               In("end", [&] { return Pair(*ao->get("end"), y.endLo, y.endHi); }) &&
               Key(*ao, "at", y.at, OOBSUM_M(Str));
    };
    return o != nullptr && Key(*o, "id", r.id, OOBSUM_M(Str)) && Key(*o, "kind", r.kind, kind) &&
           Key(*o, "have", r.have, OOBSUM_M(Expr)) && Key(*o, "obj", r.obj, OOBSUM_M(ObjName)) &&
           In("end", [&] { return Pair(*o->get("end"), r.endLo, r.endHi); }) &&
           OptKey(*o, "start", r.start, OOBSUM_M(Expr)) && Key(*o, "bound", r.bound, OOBSUM_M(TagName)) &&
           In("guard", [&] { return List(*o->get("guard"), r.guard, OOBSUM_M(Cond)); }) &&
           Key(*o, "access", r.access, OOBSUM_M(ReadAccess)) &&
           In("path", [&] { return List(*o->get("path"), r.path, OOBSUM_M(Str)); }) &&
           Key(*o, "depth", r.depth, OOBSUM_M(Int)) && Key(*o, "eff_depth", r.effDepth, OOBSUM_M(Int)) &&
           Key(*o, "blind", r.blind, OOBSUM_M(Bool)) &&
           OptKey(*o, "absorbed", r.absorbed,
                  [&](const Value &x, std::vector<Absorbed> &y) { return List(x, y, absorbed); });
}

bool Reader::ReadEff(const Value &v, Eff &e)
{
    const Object *o = Obj(v, {"kind"},
                          {"size", "maybe_null", "range", "when", "then", "target", "value", "eff_depth", "bound",
                           "may", "sub", "sub_off"});
    auto strings = [&](const Value &x, std::vector<std::string> &y) { return List(x, y, OOBSUM_M(Expr)); };
    auto conds = [&](const Value &x, std::vector<std::string> &y) { return List(x, y, OOBSUM_M(Cond)); };
    return o != nullptr && Key(*o, "kind", e.kind, OOBSUM_M(Str)) && OptKey(*o, "size", e.size, OOBSUM_M(Expr)) &&
           OptKey(*o, "maybe_null", e.maybeNull, OOBSUM_M(Bool)) && OptKey(*o, "range", e.range, strings) &&
           OptKey(*o, "when", e.when, OOBSUM_M(Cond)) && OptKey(*o, "then", e.then, conds) &&
           OptKey(*o, "target", e.target, OOBSUM_M(Str)) && OptKey(*o, "value", e.value, OOBSUM_M(Expr)) &&
           OptKey(*o, "eff_depth", e.effDepth, OOBSUM_M(Int)) && OptKey(*o, "bound", e.bound, OOBSUM_M(TagName)) &&
           OptKey(*o, "may", e.may, OOBSUM_M(Bool)) && OptKey(*o, "sub", e.sub, OOBSUM_M(Expr)) &&
           OptKey(*o, "sub_off", e.subOff, OOBSUM_M(Expr));
}

bool Reader::ReadPtr(const Value &v, PtrArg &p)
{
    const Object *o =
        Obj(v, {"obj", "have"},
            {"alias", "eff_depth", "fields", "parent", "back", "off_lo", "off_hi", "off_bound", "sub", "sub_back"});
    auto outer = [&](const Value &x, OuterObj &y) {
        const Object *po = Obj(x, {"obj", "have"}, {});
        return po != nullptr && Key(*po, "obj", y.obj, OOBSUM_M(ObjName)) && Key(*po, "have", y.have, OOBSUM_M(Expr));
    };
    auto fields = [&](const Value &x, std::map<std::string, ArgValue> &y) {
        const Object *fo = x.getAsObject();
        if (fo == nullptr) {
            return Fail("expected an object");
        }
        for (const auto &entry : *fo) {
            ArgValue arg;
            if (!In(entry.first, [&] { return ReadArg(entry.second, arg, false); })) {
                return false;
            }
            y[entry.first.str()] = std::move(arg);
        }
        return true;
    };
    return o != nullptr && Key(*o, "obj", p.obj, OOBSUM_M(ObjName)) && Key(*o, "have", p.have, OOBSUM_M(Expr)) &&
           OptKey(*o, "alias", p.alias, OOBSUM_M(Str)) && OptKey(*o, "eff_depth", p.effDepth, OOBSUM_M(Int)) &&
           OptKey(*o, "fields", p.fields, fields) && OptKey(*o, "parent", p.parent, outer) &&
           OptKey(*o, "back", p.back, OOBSUM_M(Expr)) && OptKey(*o, "off_lo", p.offLo, OOBSUM_M(Expr)) &&
           OptKey(*o, "off_hi", p.offHi, OOBSUM_M(Expr)) && OptKey(*o, "off_bound", p.offBound, OOBSUM_M(TagName)) &&
           OptKey(*o, "sub", p.sub, OOBSUM_M(Expr)) && OptKey(*o, "sub_back", p.subBack, OOBSUM_M(Expr));
}

bool Reader::ReadArg(const Value &v, ArgValue &a, bool allowNull)
{
    if (v.getAsNull()) {
        if (!allowNull) {
            return Fail("unexpected null");
        }
        a.kind = ArgValue::Kind::Null;
        return true;
    }
    if (v.getAsString()) {
        a.kind = ArgValue::Kind::Expr;
        return Expr(v, a.expr);
    }
    a.kind = ArgValue::Kind::Ptr;
    a.ptr = std::make_shared<PtrArg>();
    return ReadPtr(v, *a.ptr);
}

bool Reader::ReadApply(const Value &v, Apply &a)
{
    const Object *o = Obj(v, {"kind"}, {"size", "maybe_null", "range", "when", "then", "arg", "field", "value"});
    auto strings = [&](const Value &x, std::vector<std::string> &y) { return List(x, y, OOBSUM_M(Expr)); };
    auto conds = [&](const Value &x, std::vector<std::string> &y) { return List(x, y, OOBSUM_M(Cond)); };
    return o != nullptr && Key(*o, "kind", a.kind, OOBSUM_M(Str)) && OptKey(*o, "size", a.size, OOBSUM_M(Expr)) &&
           OptKey(*o, "maybe_null", a.maybeNull, OOBSUM_M(Bool)) && OptKey(*o, "range", a.range, strings) &&
           OptKey(*o, "when", a.when, OOBSUM_M(Cond)) && OptKey(*o, "then", a.then, conds) &&
           OptKey(*o, "arg", a.arg, OOBSUM_M(Int)) && OptKey(*o, "field", a.field, OOBSUM_M(Str)) &&
           OptKey(*o, "value", a.value, OOBSUM_M(Expr));
}

bool Reader::ReadResult(const Value &v, Result &r)
{
    const Object *probe = v.getAsObject();
    if (probe == nullptr) {
        return Fail("expected an object");
    }
    if (probe->get("req") != nullptr) {
        r.kind = Result::Kind::Req;
        const Object *o = Obj(v, {"req", "outcome"}, {"as", "report", "reason", "parent"});
        llvm::Optional<bool> parent;
        if (o != nullptr && !OptKey(*o, "parent", parent, OOBSUM_M(Bool))) {
            return false;
        }
        r.parent = parent.getValueOr(false);
        if (o == nullptr || !Key(*o, "req", r.req, OOBSUM_M(Str)) ||
            !Key(*o, "outcome", r.outcome, [&](const Value &x, std::string &y) {
                return OneOf(x, y, {"up", "safe", "L1", "L2", "open"});
            })) {
            return false;
        }
        // 每种结局只带它自己的那个键（schema 的 result|0…|3）
        const char *need = r.outcome == "up"                          ? "as"
                           : (r.outcome == "L1" || r.outcome == "L2") ? "report"
                           : r.outcome == "open"                      ? "reason"
                                                                      : nullptr;
        for (const char *key : {"as", "report", "reason"}) {
            bool has = o->get(key) != nullptr;
            bool want = need != nullptr && llvm::StringRef(key) == need;
            if (has != want) {
                return Fail(llvm::Twine(want ? "missing key '" : "unexpected key '") + key + "' for outcome " +
                            r.outcome);
            }
        }
        return (r.outcome != "up" || Key(*o, "as", r.as, OOBSUM_M(Str))) &&
               ((r.outcome != "L1" && r.outcome != "L2") || Key(*o, "report", r.report, OOBSUM_M(Str))) &&
               (r.outcome != "open" || Key(*o, "reason", r.reason, OOBSUM_M(ReasonName)));
    }
    if (probe->get("eff") != nullptr) {
        r.kind = Result::Kind::Eff;
        const Object *o = Obj(v, {"eff", "apply"}, {});
        return o != nullptr && Key(*o, "eff", r.eff, OOBSUM_M(Int)) && Key(*o, "apply", r.apply, OOBSUM_M(ReadApply));
    }
    if (probe->get("gen") != nullptr) {
        r.kind = Result::Kind::Gen;
        const Object *o = Obj(v, {"gen"}, {});
        return o != nullptr && In("gen", [&] { return List(*o->get("gen"), r.gen, OOBSUM_M(Str)); });
    }
    if (probe->get("gen_eff") != nullptr) {
        r.kind = Result::Kind::GenEff;
        const Object *o = Obj(v, {"gen_eff"}, {});
        return o != nullptr && In("gen_eff", [&] { return List(*o->get("gen_eff"), r.genEff, OOBSUM_M(Int)); });
    }
    if (probe->get("pure") != nullptr) {
        r.kind = Result::Kind::Pure;
        const Object *o = Obj(v, {"pure"}, {});
        bool pure = false;
        return o != nullptr && Key(*o, "pure", pure, OOBSUM_M(Bool)) && (pure || Fail("'pure' must be true"));
    }
    if (probe->get("open") != nullptr) {
        r.kind = Result::Kind::Open;
        const Object *o = Obj(v, {"open"}, {});
        return o != nullptr && Key(*o, "open", r.open, OOBSUM_M(ReasonName));
    }
    return Fail("unrecognized result");
}

bool Reader::ReadCall(const Value &v, Call &c)
{
    const Object *o = Obj(v, {"site", "callee", "args", "results"},
                          {"model", "locals", "guards", "callee_key", "alt", "ind", "gvals"});
    llvm::Optional<bool> alt;  // SPEC-0058
    llvm::Optional<bool> ind;
    if (o != nullptr && (!OptKey(*o, "alt", alt, OOBSUM_M(Bool)) || !OptKey(*o, "ind", ind, OOBSUM_M(Bool)))) {
        return false;
    }
    c.alt = alt.getValueOr(false);
    c.ind = ind.getValueOr(false);
    auto locals = [&](const Value &x, std::map<std::string, Local> &y) {
        const Object *lo = x.getAsObject();
        if (lo == nullptr) {
            return Fail("expected an object");
        }
        for (const auto &entry : *lo) {
            Local local;
            bool ok = In(entry.first, [&] {
                const Object *e = Obj(entry.second, {"range", "bound"}, {});
                return e != nullptr && In("range", [&] { return Pair(*e->get("range"), local.lo, local.hi); }) &&
                       Key(*e, "bound", local.bound, OOBSUM_M(TagName));
            });
            if (!ok) {
                return false;
            }
            y[entry.first.str()] = std::move(local);
        }
        return true;
    };
    auto gvals = [&](const Value &x, std::map<std::string, std::string> &y) {  // SPEC-0064
        const Object *go = x.getAsObject();
        if (go == nullptr) {
            return Fail("expected an object");
        }
        for (const auto &entry : *go) {
            std::string value;
            bool ok = In(entry.first,
                         [&] { return Str(entry.second, value) && (value == "?" || Expr(entry.second, value)); });
            if (!ok) {
                return false;
            }
            y[entry.first.str()] = value;
        }
        return true;
    };
    auto guards = [&](const Value &x, std::vector<CallGuard> &y) {
        return List(x, y, [&](const Value &g, CallGuard &out) {
            const Object *go = Obj(g, {"cond", "bound"}, {});
            return go != nullptr && Key(*go, "cond", out.cond, OOBSUM_M(Cond)) &&
                   Key(*go, "bound", out.bound, OOBSUM_M(TagName));
        });
    };
    auto model = [&](const Value &x, std::string &y) {
        return OneOf(x, y, {"access", "alloc", "pure", "unknown", "free"});
    };
    return o != nullptr && Key(*o, "site", c.site, OOBSUM_M(Str)) && Key(*o, "callee", c.callee, OOBSUM_M(Str)) &&
           OptKey(*o, "callee_key", c.calleeKey, OOBSUM_M(Str)) && OptKey(*o, "model", c.model, model) &&
           In("args",
              [&] {
                  return List(*o->get("args"), c.args,
                              [&](const Value &x, ArgValue &y) { return ReadArg(x, y, true); });
              }) &&
           OptKey(*o, "locals", c.locals, locals) && OptKey(*o, "guards", c.guards, guards) &&
           OptKey(*o, "gvals", c.gvals, gvals) &&
           In("results", [&] { return List(*o->get("results"), c.results, OOBSUM_M(ReadResult)); });
}

bool Reader::ReadRep(const Value &v, Rep &r)
{
    const Object *o = Obj(v,
                          {"id", "level", "reported", "site", "access", "kind", "obj", "have", "end", "bound", "depth",
                           "eff_depth", "chain", "fp"},
                          {"via", "dir"});
    auto via = [&](const Value &x, std::string &y) { return Str(x, y) && (IsVia(y) || Fail("bad via '" + y + "'")); };
    llvm::Optional<std::string> dir;  // SPEC-0039
    if (o != nullptr && !OptKey(*o, "dir", dir, [&](const Value &x, std::string &y) {
            return OneOf(x, y, {"back", "sub", "subback"});
        })) {
        return false;
    }
    r.dir = dir.getValueOr("");
    return o != nullptr && Key(*o, "id", r.id, OOBSUM_M(Str)) &&
           Key(*o, "level", r.level,
               [&](const Value &x, std::string &y) {
                   return OneOf(x, y, {"L1", "L2"});
               }) &&
           Key(*o, "reported", r.reported, OOBSUM_M(Bool)) && Key(*o, "site", r.site, OOBSUM_M(Str)) &&
           Key(*o, "access", r.access, OOBSUM_M(ReadAccess)) &&
           Key(*o, "kind", r.kind,
               [&](const Value &x, std::string &y) {
                   return OneOf(x, y, {"R", "W"});
               }) &&
           Key(*o, "obj", r.obj, OOBSUM_M(ObjName)) && OptKey(*o, "via", r.via, via) &&
           Key(*o, "have", r.have, OOBSUM_M(Expr)) &&
           In("end", [&] { return Pair(*o->get("end"), r.endLo, r.endHi); }) &&
           Key(*o, "bound", r.bound, OOBSUM_M(TagName)) && Key(*o, "depth", r.depth, OOBSUM_M(Int)) &&
           Key(*o, "eff_depth", r.effDepth, OOBSUM_M(Int)) &&
           In("chain", [&] { return List(*o->get("chain"), r.chain, OOBSUM_M(Str)); }) &&
           Key(*o, "fp", r.fp, OOBSUM_M(Str));
}

bool Reader::ReadOpen(const Value &v, Open &op)
{
    const Object *o = Obj(v, {"reason", "at"}, {"req"});
    return o != nullptr && Key(*o, "reason", op.reason, OOBSUM_M(ReasonName)) && Key(*o, "at", op.at, OOBSUM_M(Str)) &&
           OptKey(*o, "req", op.req, OOBSUM_M(Str));
}

bool Reader::ReadSummary(const Value &v, Summary &s)
{
    const Object *o =
        Obj(v, {"schema", "fn", "key", "loc", "params", "status", "reqs", "effs", "calls", "reps", "opens"},
            {"tu", "fields", "mods", "assumes"});
    if (o == nullptr) {
        return false;
    }
    std::string schema;
    auto params = [&](const Value &x, Param &y) {
        const Object *po = Obj(x, {"name", "type"}, {"size"});
        return po != nullptr && Key(*po, "name", y.name, OOBSUM_M(Str)) && Key(*po, "type", y.type, OOBSUM_M(Str)) &&
               OptKey(*po, "size", y.size, OOBSUM_M(Int));
    };
    auto fields = [&](const Value &x, std::map<std::string, FieldInfo> &y) {
        const Object *fo = x.getAsObject();
        if (fo == nullptr) {
            return Fail("expected an object");
        }
        for (const auto &entry : *fo) {
            FieldInfo info;
            bool ok = In(entry.first, [&] {
                const Object *e = Obj(entry.second, {"off", "size"}, {"type"});
                return e != nullptr && Key(*e, "off", info.off, OOBSUM_M(Int)) &&
                       Key(*e, "size", info.size, OOBSUM_M(Int)) && OptKey(*e, "type", info.type, OOBSUM_M(Str));
            });
            if (!ok) {
                return false;
            }
            y[entry.first.str()] = std::move(info);
        }
        return true;
    };
    auto pairOf = [&](const Value &x, std::pair<std::string, std::string> &y) {
        const Array *a = x.getAsArray();
        llvm::Optional<llvm::StringRef> first = a != nullptr && a->size() == 2 ? (*a)[0].getAsString() : llvm::None;
        llvm::Optional<llvm::StringRef> second = a != nullptr && a->size() == 2 ? (*a)[1].getAsString() : llvm::None;
        if (!first || !second) {
            return Fail("expected [root, root]");
        }
        y = {first->str(), second->str()};
        return true;
    };
    return Key(*o, "schema", schema, OOBSUM_M(Str)) &&
           In("schema",
              [&] { return schema == kSummarySchema || Fail("expected '" + llvm::Twine(kSummarySchema) + "'"); }) &&
           Key(*o, "fn", s.fn, OOBSUM_M(Str)) && Key(*o, "key", s.key, OOBSUM_M(Str)) &&
           OptKey(*o, "tu", s.tu, OOBSUM_M(Str)) && Key(*o, "loc", s.loc, OOBSUM_M(Str)) &&
           In("params", [&] { return List(*o->get("params"), s.params, params); }) &&
           OptKey(*o, "fields", s.fields, fields) && Key(*o, "status", s.status, OOBSUM_M(Str)) &&
           (o->get("mods") == nullptr || In("mods", [&] { return List(*o->get("mods"), s.mods, OOBSUM_M(Str)); })) &&
           (o->get("assumes") == nullptr ||
            In("assumes", [&] { return List(*o->get("assumes"), s.assumes, pairOf); })) &&
           In("reqs", [&] { return List(*o->get("reqs"), s.reqs, OOBSUM_M(ReadReq)); }) &&
           In("effs", [&] { return List(*o->get("effs"), s.effs, OOBSUM_M(ReadEff)); }) &&
           In("calls", [&] { return List(*o->get("calls"), s.calls, OOBSUM_M(ReadCall)); }) &&
           In("reps", [&] { return List(*o->get("reps"), s.reps, OOBSUM_M(ReadRep)); }) &&
           In("opens", [&] { return List(*o->get("opens"), s.opens, OOBSUM_M(ReadOpen)); });
}

#undef OOBSUM_M

// ---- 写出 ----

Array Strings(const std::vector<std::string> &v)
{
    Array a;
    for (const std::string &s : v) {
        a.push_back(s);
    }
    return a;
}

template <typename T>
void Put(Object &o, llvm::StringRef key, const llvm::Optional<T> &v)
{
    if (v) {
        o[key] = *v;
    }
}

void PutList(Object &o, llvm::StringRef key, const llvm::Optional<std::vector<std::string>> &v)
{
    if (v) {
        o[key] = Strings(*v);
    }
}

Value AccessJson(const Access &a)
{
    return Object{{"fn", a.fn}, {"key", a.key}, {"at", a.at}, {"end", a.end}};
}

Value ArgJson(const ArgValue &a);

Value PtrJson(const PtrArg &p)
{
    Object o{{"obj", p.obj}, {"have", p.have}};
    Put(o, "alias", p.alias);
    Put(o, "eff_depth", p.effDepth);
    if (p.parent) {
        o["parent"] = Object{{"obj", p.parent->obj}, {"have", p.parent->have}};
    }
    Put(o, "back", p.back);
    Put(o, "off_lo", p.offLo);
    Put(o, "off_hi", p.offHi);
    Put(o, "off_bound", p.offBound);
    Put(o, "sub", p.sub);           // SPEC-0104
    Put(o, "sub_back", p.subBack);  // SPEC-0107
    if (p.fields) {
        Object f;
        for (const auto &entry : *p.fields) {
            f[entry.first] = ArgJson(entry.second);
        }
        o["fields"] = std::move(f);
    }
    return std::move(o);
}

Value ArgJson(const ArgValue &a)
{
    switch (a.kind) {
        case ArgValue::Kind::Null:
            return nullptr;
        case ArgValue::Kind::Expr:
            return a.expr;
        case ArgValue::Kind::Ptr:
            return PtrJson(*a.ptr);
    }
    return nullptr;
}

Value ApplyJson(const Apply &a)
{
    Object o{{"kind", a.kind}};
    Put(o, "size", a.size);
    Put(o, "maybe_null", a.maybeNull);
    PutList(o, "range", a.range);
    Put(o, "when", a.when);
    PutList(o, "then", a.then);
    Put(o, "arg", a.arg);
    Put(o, "field", a.field);
    Put(o, "value", a.value);
    return std::move(o);
}

Value ResultJson(const Result &r)
{
    switch (r.kind) {
        case Result::Kind::Req: {
            Object o{{"req", r.req}, {"outcome", r.outcome}};
            if (r.parent) {
                o["parent"] = true;
            }
            if (r.outcome == "up") {
                o["as"] = r.as;
            } else if (r.outcome == "L1" || r.outcome == "L2") {
                o["report"] = r.report;
            } else if (r.outcome == "open") {
                o["reason"] = r.reason;
            }
            return std::move(o);
        }
        case Result::Kind::Eff:
            return Object{{"eff", r.eff}, {"apply", ApplyJson(r.apply)}};
        case Result::Kind::Gen:
            return Object{{"gen", Strings(r.gen)}};
        case Result::Kind::GenEff: {
            Array a;
            for (int64_t i : r.genEff) {
                a.push_back(i);
            }
            return Object{{"gen_eff", std::move(a)}};
        }
        case Result::Kind::Pure:
            return Object{{"pure", true}};
        case Result::Kind::Open:
            return Object{{"open", r.open}};
    }
    return nullptr;
}

}  // namespace

Value StripDoc(const Value &v)
{
    if (const Object *o = v.getAsObject()) {
        Object out;
        for (const auto &entry : *o) {
            if (entry.first != "doc") {
                out[entry.first] = StripDoc(entry.second);
            }
        }
        return std::move(out);
    }
    if (const Array *a = v.getAsArray()) {
        Array out;
        for (const Value &x : *a) {
            out.push_back(StripDoc(x));
        }
        return std::move(out);
    }
    return v;
}

bool ReadSummary(const Value &v, Summary &out, std::string &error)
{
    error.clear();
    out = Summary();
    Reader reader(error);
    return reader.ReadSummary(StripDoc(v), out);
}

Value SummaryToJson(const Summary &s)
{
    Array params;
    for (const Param &p : s.params) {
        Object po{{"name", p.name}, {"type", p.type}};
        Put(po, "size", p.size);
        params.push_back(std::move(po));
    }
    Array reqs;
    for (const Req &r : s.reqs) {
        Object o{{"id", r.id},
                 {"kind", r.kind},
                 {"have", r.have},
                 {"obj", r.obj},
                 {"end", Array{r.endLo, r.endHi}},
                 {"bound", r.bound},
                 {"guard", Strings(r.guard)},
                 {"access", AccessJson(r.access)},
                 {"path", Strings(r.path)},
                 {"depth", r.depth},
                 {"eff_depth", r.effDepth},
                 {"blind", r.blind}};
        Put(o, "start", r.start);
        if (!r.dir.empty()) {
            o["dir"] = r.dir;
        }
        if (r.absorbed) {
            Array a;
            for (const Absorbed &x : *r.absorbed) {
                a.push_back(Object{{"kind", x.kind}, {"end", Array{x.endLo, x.endHi}}, {"at", x.at}});
            }
            o["absorbed"] = std::move(a);
        }
        reqs.push_back(std::move(o));
    }
    Array effs;
    for (const Eff &e : s.effs) {
        Object o{{"kind", e.kind}};
        Put(o, "size", e.size);
        Put(o, "maybe_null", e.maybeNull);
        PutList(o, "range", e.range);
        Put(o, "when", e.when);
        PutList(o, "then", e.then);
        Put(o, "target", e.target);
        Put(o, "value", e.value);
        Put(o, "eff_depth", e.effDepth);
        Put(o, "bound", e.bound);
        Put(o, "may", e.may);
        Put(o, "sub", e.sub);  // SPEC-0107
        Put(o, "sub_off", e.subOff);
        effs.push_back(std::move(o));
    }
    Array calls;
    for (const Call &c : s.calls) {
        Array args;
        for (const ArgValue &a : c.args) {
            args.push_back(ArgJson(a));
        }
        Array results;
        for (const Result &r : c.results) {
            results.push_back(ResultJson(r));
        }
        Object o{{"site", c.site}, {"callee", c.callee}, {"args", std::move(args)}, {"results", std::move(results)}};
        Put(o, "model", c.model);
        Put(o, "callee_key", c.calleeKey);
        if (c.alt) {
            o["alt"] = true;  // SPEC-0058
        }
        if (c.ind) {
            o["ind"] = true;
        }
        if (c.locals) {
            Object l;
            for (const auto &entry : *c.locals) {
                l[entry.first] =
                    Object{{"range", Array{entry.second.lo, entry.second.hi}}, {"bound", entry.second.bound}};
            }
            o["locals"] = std::move(l);
        }
        if (c.guards) {
            Array g;
            for (const CallGuard &x : *c.guards) {
                g.push_back(Object{{"cond", x.cond}, {"bound", x.bound}});
            }
            o["guards"] = std::move(g);
        }
        if (c.gvals) {
            Object g;
            for (const auto &entry : *c.gvals) {
                g[entry.first] = entry.second;
            }
            o["gvals"] = std::move(g);
        }
        calls.push_back(std::move(o));
    }
    Array reps;
    for (const Rep &r : s.reps) {
        Object o{{"id", r.id},
                 {"level", r.level},
                 {"reported", r.reported},
                 {"site", r.site},
                 {"access", AccessJson(r.access)},
                 {"kind", r.kind},
                 {"obj", r.obj},
                 {"have", r.have},
                 {"end", Array{r.endLo, r.endHi}},
                 {"bound", r.bound},
                 {"depth", r.depth},
                 {"eff_depth", r.effDepth},
                 {"chain", Strings(r.chain)},
                 {"fp", r.fp}};
        Put(o, "via", r.via);
        if (!r.dir.empty()) {
            o["dir"] = r.dir;
        }
        reps.push_back(std::move(o));
    }
    Array opens;
    for (const Open &op : s.opens) {
        Object o{{"reason", op.reason}, {"at", op.at}};
        Put(o, "req", op.req);
        opens.push_back(std::move(o));
    }
    Object out{{"schema", kSummarySchema},
               {"fn", s.fn},
               {"key", s.key},
               {"loc", s.loc},
               {"params", std::move(params)},
               {"status", s.status},
               {"reqs", std::move(reqs)},
               {"effs", std::move(effs)},
               {"calls", std::move(calls)},
               {"reps", std::move(reps)},
               {"opens", std::move(opens)}};
    Put(out, "tu", s.tu);
    if (!s.mods.empty()) {
        out["mods"] = Strings(s.mods);  // SPEC-0045
    }
    if (!s.assumes.empty()) {
        Array a;
        for (const auto &p : s.assumes) {
            a.push_back(Array{p.first, p.second});  // SPEC-0106
        }
        out["assumes"] = std::move(a);
    }
    if (s.fields) {
        Object f;
        for (const auto &entry : *s.fields) {
            Object info{{"off", entry.second.off}, {"size", entry.second.size}};
            Put(info, "type", entry.second.type);
            f[entry.first] = std::move(info);
        }
        out["fields"] = std::move(f);
    }
    return std::move(out);
}

bool ReadSummaryFile(llvm::StringRef path, Summary &out, std::string &error)
{
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer) {
        error = "cannot read " + path.str() + ": " + buffer.getError().message();
        return false;
    }
    llvm::Expected<Value> value = llvm::json::parse((*buffer)->getBuffer());
    if (!value) {
        error = path.str() + ": " + llvm::toString(value.takeError());
        return false;
    }
    if (!ReadSummary(*value, out, error)) {
        error = path.str() + ": " + error;
        return false;
    }
    return true;
}

bool WriteSummaryFile(llvm::StringRef path, const Summary &s, std::string &error)
{
    // 缩进写（SPEC-0114）；临时文件带进程号、写完改名（SPEC-0118）
    return WriteFileAtomic(
        path, [&](llvm::raw_ostream &os) { os << llvm::formatv("{0:1}", SummaryToJson(s)) << "\n"; }, error);
}

}  // namespace summary
}  // namespace oobsum
