//===-- Interp.cpp - Intraprocedural abstract interpretation ----*- C++ -*-===//
//
// 见 Interp.h。规则见 SPEC-0014 §2。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Interp.h"

#include "OobSummary/core/FirText.h"
#include "OobSummary/core/Key.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <functional>
#include <set>

namespace oobsum {
namespace interp {

using fir::ExprId;
using fir::ExprOp;

// ---- IntVal / PtrVal ----

IntVal IntVal::Exact(const LinExpr &e)
{
    if (e.IsUnknown()) {
        return Top("overflow");
    }
    IntVal v;
    v.kind = Kind::Exact;
    v.value = e;
    return v;
}

IntVal IntVal::Range(const llvm::Optional<LinExpr> &lo, const llvm::Optional<LinExpr> &hi, Tag tag)
{
    IntVal v;
    v.kind = Kind::Range;
    v.hasLo = lo.hasValue() && !lo->IsUnknown();
    v.hasHi = hi.hasValue() && !hi->IsUnknown();
    if (v.hasLo) {
        v.lo = *lo;
    }
    if (v.hasHi) {
        v.hi = *hi;
    }
    v.tag = tag;
    if (!v.hasLo && !v.hasHi && tag == Tag::None) {
        return Top("unbounded");
    }
    return v;
}

IntVal IntVal::Top(llvm::StringRef why)
{
    IntVal v;
    v.why = why.str();
    return v;
}

llvm::Optional<LinExpr> IntVal::Lower() const
{
    if (kind == Kind::Exact) {
        return value;
    }
    if (kind == Kind::Range && hasLo) {
        return lo;
    }
    return llvm::None;
}

llvm::Optional<LinExpr> IntVal::Upper() const
{
    if (kind == Kind::Exact) {
        return value;
    }
    if (kind == Kind::Range && hasHi) {
        return hi;
    }
    return llvm::None;
}

bool IntVal::operator==(const IntVal &o) const
{
    if (kind != o.kind) {
        return false;
    }
    switch (kind) {
        case Kind::Exact:
            return value == o.value;
        case Kind::Range:
            return hasLo == o.hasLo && hasHi == o.hasHi && (!hasLo || lo == o.lo) && (!hasHi || hi == o.hi) &&
                   tag == o.tag;
        case Kind::Top:
            return why == o.why;
    }
    return false;
}

std::string IntVal::Show() const
{
    switch (kind) {
        case Kind::Exact:
            return value.Show();
        case Kind::Range:
            return "[" + (hasLo ? lo.Show() : std::string("-inf")) + ", " + (hasHi ? hi.Show() : std::string("+inf")) +
                   "]" + (tag == Tag::None ? "" : ":" + TagName(tag).str());
        case Kind::Top:
            return "?(" + why + ")";
    }
    return "?";
}

bool PtrVal::operator==(const PtrVal &o) const
{
    return kind == o.kind && id == o.id && off == o.off && why == o.why;
}

// ---- 类型 ----

bool TypeSize(const fir::Module &m, fir::TypeId type, int64_t &out)
{
    if (type >= m.types.size()) {
        return false;
    }
    const fir::Type &t = m.types[type];
    switch (t.kind) {
        case fir::TypeKind::Int:
            out = t.width / 8;
            return out > 0;
        case fir::TypeKind::Ptr:
            out = m.target.ptr;
            return true;
        case fir::TypeKind::Arr: {
            int64_t elem = 0;
            if (t.count < 0 || !TypeSize(m, t.to, elem)) {
                return false;
            }
            out = elem * t.count;
            return true;
        }
        case fir::TypeKind::Rec: {
            const fir::Record *r = m.FindRecord(t.record);
            if (r == nullptr) {
                return false;
            }
            out = r->size;
            return true;
        }
        case fir::TypeKind::Other:
            out = t.size;
            return t.size > 0;
        case fir::TypeKind::Void:
            return false;
    }
    return false;
}

std::string SiteOf(const fir::Module &m, const fir::Loc &loc)
{
    std::string file = loc.file < m.files.size() ? m.files[loc.file] : std::string("?");
    return llvm::sys::path::filename(file, llvm::sys::path::Style::posix).str() + ":" + std::to_string(loc.line);
}

namespace {

struct IntType {
    unsigned width = 0;
    bool isSigned = false;
};

// 类型范围（宽度 ≥ 63 时视为无界）
IntVal TypeRange(unsigned width, bool isSigned, Tag tag)
{
    if (width == 0 || width >= 63) {
        return IntVal::Top("type");
    }
    if (isSigned) {
        int64_t half = int64_t(1) << (width - 1);
        return IntVal::Range(LinExpr::Const(-half), LinExpr::Const(half - 1), tag);
    }
    return IntVal::Range(LinExpr::Const(0), LinExpr::Const((int64_t(1) << width) - 1), tag);
}

// 常数按目标宽度截断 / 符号扩展
int64_t Truncate(int64_t c, unsigned width, bool isSigned)
{
    if (width == 0 || width >= 64) {
        return c;
    }
    uint64_t mask = (uint64_t(1) << width) - 1;
    uint64_t v = static_cast<uint64_t>(c) & mask;
    if (isSigned && (v >> (width - 1)) != 0) {
        return static_cast<int64_t>(v | ~mask);
    }
    return static_cast<int64_t>(v);
}

Tag TagOf(const IntVal &v)
{
    return v.kind == IntVal::Kind::Range ? v.tag : Tag::None;
}

llvm::Optional<LinExpr> SumOpt(const llvm::Optional<LinExpr> &a, const llvm::Optional<LinExpr> &b)
{
    if (!a || !b) {
        return llvm::None;
    }
    LinExpr s = *a + *b;
    return s.IsUnknown() ? llvm::Optional<LinExpr>() : s;
}

IntVal AddVals(const IntVal &a, const IntVal &b)
{
    if (a.kind == IntVal::Kind::Exact && b.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(a.value + b.value);
    }
    if (a.kind == IntVal::Kind::Top || b.kind == IntVal::Kind::Top) {
        return IntVal::Top(a.kind == IntVal::Kind::Top ? a.why : b.why);
    }
    return IntVal::Range(SumOpt(a.Lower(), b.Lower()), SumOpt(a.Upper(), b.Upper()), WorseTag(TagOf(a), TagOf(b)));
}

IntVal ScaleVal(const IntVal &a, int64_t c)
{
    if (a.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(a.value.Scale(c));
    }
    if (a.kind == IntVal::Kind::Top) {
        return a;
    }
    llvm::Optional<LinExpr> lo = a.Lower();
    llvm::Optional<LinExpr> hi = a.Upper();
    auto scale = [&](const llvm::Optional<LinExpr> &x) {
        return x ? llvm::Optional<LinExpr>(x->Scale(c)) : llvm::Optional<LinExpr>();
    };
    return c >= 0 ? IntVal::Range(scale(lo), scale(hi), a.tag) : IntVal::Range(scale(hi), scale(lo), a.tag);
}

IntVal NegVal(const IntVal &a)
{
    return ScaleVal(a, -1);
}

IntVal MulVals(const IntVal &a, const IntVal &b)
{
    if (b.IsConst()) {
        return ScaleVal(a, b.value.Constant());
    }
    if (a.IsConst()) {
        return ScaleVal(b, a.value.Constant());
    }
    if (a.kind == IntVal::Kind::Exact && b.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(LinExpr::Mul(a.value, b.value));  // A-22：确切的不透明项
    }
    return IntVal::Top("nonlinear");
}

IntVal DivConst(const IntVal &a, int64_t c)
{
    if (c <= 0) {
        return IntVal::Top("div");
    }
    if (a.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(LinExpr::Div(a.value, c));  // A-23：不丢成 ∞
    }
    if (a.kind == IntVal::Kind::Top) {
        return a;
    }
    auto div = [&](const llvm::Optional<LinExpr> &x) {
        return x ? llvm::Optional<LinExpr>(LinExpr::Div(*x, c)) : llvm::Optional<LinExpr>();
    };
    return IntVal::Range(div(a.Lower()), div(a.Upper()), a.tag);
}

IntVal ModConst(const IntVal &a, int64_t c)
{
    if (c <= 0) {
        return IntVal::Top("mod");
    }
    if (a.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(LinExpr::Mod(a.value, c));  // 界 [0, c − 1]（mask）由 SymEnv 给出
    }
    return IntVal::Range(LinExpr::Const(0), LinExpr::Const(c - 1), Tag::Mask);  // A-24
}

IntVal AndConst(const IntVal &a, int64_t m)
{
    if (m < 0) {
        return IntVal::Top("and");
    }
    bool lowMask = ((m + 1) & m) == 0;  // m = 2^k − 1：x & m 就是 x mod 2^k（向下取整的余数，负数也成立）
    if (lowMask && m < (int64_t(1) << 62)) {
        return ModConst(a, m + 1);
    }
    return IntVal::Range(LinExpr::Const(0), LinExpr::Const(m), Tag::Mask);
}

// 两个值合并（A-15 的子集）
llvm::Optional<LinExpr> MinOpt(const llvm::Optional<LinExpr> &a, const llvm::Optional<LinExpr> &b)
{
    if (!a || !b) {
        return llvm::None;
    }
    LinExpr d = *a - *b;
    if (!d.IsConst()) {
        return llvm::None;
    }
    return d.Constant() <= 0 ? *a : *b;
}

llvm::Optional<LinExpr> MaxOpt(const llvm::Optional<LinExpr> &a, const llvm::Optional<LinExpr> &b)
{
    if (!a || !b) {
        return llvm::None;
    }
    LinExpr d = *a - *b;
    if (!d.IsConst()) {
        return llvm::None;
    }
    return d.Constant() >= 0 ? *a : *b;
}

IntVal JoinInt(const IntVal &a, const IntVal &b)
{
    if (a == b) {
        return a;
    }
    if (a.kind == IntVal::Kind::Top || b.kind == IntVal::Kind::Top) {
        return IntVal::Top("imprecise_join");
    }
    llvm::Optional<LinExpr> lo = MinOpt(a.Lower(), b.Lower());
    llvm::Optional<LinExpr> hi = MaxOpt(a.Upper(), b.Upper());
    if (!lo && !hi) {
        return IntVal::Top("imprecise_join");
    }
    // 两条路上的确切值合成区间，来源是分支（D-22 的 branch）
    Tag tag = WorseTag(a.kind == IntVal::Kind::Exact ? Tag::Branch : a.tag,
                       b.kind == IntVal::Kind::Exact ? Tag::Branch : b.tag);
    return IntVal::Range(lo, hi, tag);
}

PtrVal UnknownPtr(llvm::StringRef why)
{
    PtrVal p;
    p.kind = BaseKind::Unknown;
    p.why = why.str();
    p.off = IntVal::Top(why);
    return p;
}

Value JoinValue(const Value &a, const Value &b)
{
    if (a == b) {
        return a;
    }
    Value out;
    if (a.isPtr != b.isPtr) {
        out.isPtr = a.isPtr || b.isPtr;
        out.p = UnknownPtr("base_join");
        out.i = IntVal::Top("imprecise_join");
        return out;
    }
    out.isPtr = a.isPtr;
    if (!a.isPtr) {
        out.i = JoinInt(a.i, b.i);
        return out;
    }
    if (a.p.kind == BaseKind::Unknown || b.p.kind == BaseKind::Unknown || a.p.id != b.p.id) {
        out.p = UnknownPtr("base_join");
        return out;
    }
    out.p = a.p;
    out.p.off = JoinInt(a.p.off, b.p.off);
    return out;
}

// 内存格（A-12）：(基址身份, 常数偏移, 字节数) → 值
using CellKey = std::tuple<std::string, int64_t, int64_t>;

struct State {
    bool reached = false;
    std::map<uint32_t, Value> locals;
    std::map<CellKey, Value> cells;
    std::vector<LinExpr> guards;  // A-14：只含接口符号的 L ≤ 0，至多 8 个，先进先出（SPEC-0016）
};

const size_t kMaxGuards = 8;

State Join(const State &a, const State &b)
{
    if (!a.reached) {
        return b;
    }
    if (!b.reached) {
        return a;
    }
    State out;
    out.reached = true;
    for (const auto &entry : a.locals) {
        auto it = b.locals.find(entry.first);
        out.locals[entry.first] = it == b.locals.end() ? Value() : JoinValue(entry.second, it->second);
    }
    for (const auto &entry : a.cells) {
        auto it = b.cells.find(entry.first);
        if (it != b.cells.end() && it->second == entry.second) {
            out.cells.insert(entry);  // 两边都有且相同才保留
        }
    }
    for (const LinExpr &g : a.guards) {
        if (llvm::is_contained(b.guards, g)) {
            out.guards.push_back(g);  // A-15：守卫取交集
        }
    }
    return out;
}

// 线性式里出现的符号名（不透明项看里面；ext(x) 记成 ext:x），用来挑与 Req 相关的守卫（A-44，SPEC-0016 §2.2）
void CollectSyms(const LinExpr &e, std::set<std::string> &out)
{
    for (const auto &term : e.Terms()) {
        const Atom &atom = term.first;
        switch (atom.kind) {
            case Atom::Kind::Sym:
                out.insert(atom.name);
                break;
            case Atom::Kind::Ext:
                out.insert("ext:" + atom.name);
                break;
            default:
                if (atom.a) {
                    CollectSyms(*atom.a, out);
                }
                if (atom.b) {
                    CollectSyms(*atom.b, out);
                }
                break;
        }
    }
}

bool IsCondExpr(const fir::Function &f, ExprId e)
{
    if (e >= f.exprs.size()) {
        return false;
    }
    const fir::Expr &x = f.exprs[e];
    if (x.op == ExprOp::Cmp || x.op == ExprOp::LNot) {
        return true;
    }
    return x.op == ExprOp::Bin && static_cast<fir::BinOp>(x.sub) == fir::BinOp::And && IsCondExpr(f, x.a) &&
           IsCondExpr(f, x.b);
}

// 一次访问（覆盖合并之前）
struct RawAccess {
    std::string kind;
    LinExpr have;
    std::string obj;
    LinExpr lo;
    LinExpr hi;
    llvm::Optional<LinExpr> start;
    Tag tag = Tag::Exact;
    std::string at;
    std::vector<std::string> guard;  // 与 have / end 有公共符号的守卫（ShowLe0）
};

class Analyzer {
public:
    Analyzer(const fir::Module &m, const fir::Function &f) : m(m), f(f)
    {
    }

    FunctionFacts Run();

private:
    // ---- 局部的类型 ----
    const fir::Type *LocalType(uint32_t id) const
    {
        const fir::Local *l = f.FindLocal(id);
        return l != nullptr && l->type < m.types.size() ? &m.types[l->type] : nullptr;
    }
    bool IsPtrLocal(uint32_t id) const
    {
        const fir::Type *t = LocalType(id);
        return t != nullptr && t->kind == fir::TypeKind::Ptr;
    }
    llvm::Optional<IntType> IntTypeOf(ExprId e) const
    {
        const fir::Expr &x = f.exprs[e];
        if (x.op == ExprOp::Local) {
            const fir::Type *t = LocalType(static_cast<uint32_t>(x.value));
            if (t != nullptr && t->kind == fir::TypeKind::Int) {
                return IntType{t->width, t->isSigned};
            }
        }
        if ((x.op == ExprOp::Bin && x.width > 0) || x.op == ExprOp::Cast) {
            return IntType{x.width, x.isSigned};
        }
        return llvm::None;
    }

    // ---- 求值 ----
    Value Eval(ExprId e, const State &s);
    IntVal EvalInt(ExprId e, const State &s)
    {
        Value v = Eval(e, s);
        return v.isPtr ? IntVal::Top("ptr_as_int") : v.i;
    }
    PtrVal EvalPtr(ExprId e, const State &s)
    {
        Value v = Eval(e, s);
        if (v.isPtr) {
            return v.p;
        }
        return UnknownPtr(v.i.IsConst() && v.i.value.Constant() == 0 ? "null" : "int_as_ptr");
    }
    IntVal Bin(const fir::Expr &x, const State &s);
    IntVal Cast(const fir::Expr &x, const State &s);
    PtrVal Base(BaseKind kind, const std::string &id, const std::string &obj, const llvm::Optional<LinExpr> &size) const
    {
        PtrVal p;
        p.kind = kind;
        p.id = id;
        p.obj = obj;
        p.hasSize = size.hasValue();
        if (size) {
            p.size = *size;
        }
        return p;
    }

    // ---- 指令 ----
    void Step(const fir::Inst &inst, State &s);
    Value LoadValue(const PtrVal &addr, int64_t size, uint32_t dst, const State &s) const;
    void Access(const char *kind, const PtrVal &addr, int64_t size, const fir::Loc &loc, const State &s);

    // ---- 条件边与守卫（SPEC-0016）----
    SymEnv Env(const State &s) const;
    bool Faithful(ExprId e, const IntVal &v, const SymEnv &env) const;
    bool Assume(ExprId c, bool negate, State &s);
    bool AssumeCmp(fir::CmpOp op, ExprId a, ExprId b, State &s);
    bool Narrow(ExprId e, fir::CmpOp op, const LinExpr &c, State &s);
    static void AddGuard(State &s, const LinExpr &le0);
    void SetLocal(State &s, uint32_t dst, Value v) const
    {
        bool ptr = IsPtrLocal(dst);
        if (ptr && !v.isPtr) {
            Value p;
            p.isPtr = true;
            p.p = UnknownPtr(v.i.IsConst() && v.i.value.Constant() == 0 ? "null" : "int_as_ptr");
            v = p;
        } else if (!ptr && v.isPtr) {
            Value i;
            i.i = IntVal::Top("ptr_as_int");
            v = i;
        }
        s.locals[dst] = v;
    }

    State Entry() const;
    std::vector<summary::Req> Merge() const;
    summary::Req ToReq(const RawAccess &a, size_t number) const;
    void RecordCall(const fir::Inst &inst, const State &s);
    std::string ShowState(const fir::Block &b, const State &s) const;

public:
    std::vector<std::string> *trace = nullptr;

private:
    const fir::Module &m;
    const fir::Function &f;
    std::vector<RawAccess> accesses;
    std::vector<RawAccess> safe;
    std::vector<summary::Open> opens;
    std::vector<summary::Call> calls;
    mutable std::map<std::string, summary::FieldInfo> fields;
};

IntVal Analyzer::Bin(const fir::Expr &x, const State &s)
{
    fir::BinOp op = static_cast<fir::BinOp>(x.sub);
    IntVal a = EvalInt(x.a, s);
    IntVal b = EvalInt(x.b, s);
    IntVal r;
    switch (op) {
        case fir::BinOp::Add:
            r = AddVals(a, b);
            break;
        case fir::BinOp::Sub:
            r = AddVals(a, NegVal(b));
            break;
        case fir::BinOp::Mul:
            r = MulVals(a, b);
            break;
        case fir::BinOp::Div:
            r = b.IsConst() ? DivConst(a, b.value.Constant()) : IntVal::Top("div");
            break;
        case fir::BinOp::Rem:
            r = b.IsConst() ? ModConst(a, b.value.Constant()) : IntVal::Top("mod");
            break;
        case fir::BinOp::Shl:
            r = b.IsConst() && b.value.Constant() >= 0 && b.value.Constant() < 62
                    ? ScaleVal(a, int64_t(1) << b.value.Constant())
                    : IntVal::Top("shl");
            break;
        case fir::BinOp::Shr:
            r = b.IsConst() && b.value.Constant() >= 0 && b.value.Constant() < 62
                    ? DivConst(a, int64_t(1) << b.value.Constant())
                    : IntVal::Top("shr");
            break;
        case fir::BinOp::And:
            r = b.IsConst() ? AndConst(a, b.value.Constant())
                            : (a.IsConst() ? AndConst(b, a.value.Constant()) : IntVal::Top("and"));
            break;
        case fir::BinOp::Or:
        case fir::BinOp::Xor:
            r = a.IsConst() && b.IsConst()
                    ? IntVal::Exact(LinExpr::Const(op == fir::BinOp::Or ? (a.value.Constant() | b.value.Constant())
                                                                        : (a.value.Constant() ^ b.value.Constant())))
                    : IntVal::Top("bitwise");
            break;
    }
    if (x.width == 0) {
        return r;  // 数学整数（SPEC-0005 §2.3 #1）
    }
    // 回绕形：常数按 2^W 取模；无符号减法不能证明非负 → wrapW（A-26，SPEC-0014 §2.2）
    if (r.IsConst()) {
        return IntVal::Exact(LinExpr::Const(Truncate(r.value.Constant(), x.width, x.isSigned)));
    }
    if (op == fir::BinOp::Sub && !x.isSigned) {
        llvm::Optional<LinExpr> lo = r.Lower();
        // 下界是非负常数，或在守卫与类型范围下能证明非负（SPEC-0016 §2.2：if (n == 0) return; 之后 n − 1 不回绕）
        bool nonNegative =
            lo && ((lo->IsConst() && lo->Constant() >= 0) ||
                   (!lo->IsUnknown() && !s.guards.empty() && EntailsLe0(lo->Scale(-1), s.guards, Env(s))));
        if (!nonNegative) {
            if (r.kind == IntVal::Kind::Exact && (x.width == 32 || x.width == 16)) {
                return IntVal::Exact(LinExpr::Wrap(x.width, r.value));
            }
            return TypeRange(x.width, false, Tag::Type);
        }
    }
    if (r.kind == IntVal::Kind::Top && op != fir::BinOp::Add && op != fir::BinOp::Sub && op != fir::BinOp::Mul) {
        return TypeRange(x.width, x.isSigned, Tag::Type);  // A-25：位运算等落到类型范围
    }
    return r;
}

IntVal Analyzer::Cast(const fir::Expr &x, const State &s)
{
    IntVal a = EvalInt(x.a, s);
    if (a.IsConst()) {
        return IntVal::Exact(LinExpr::Const(Truncate(a.value.Constant(), x.width, x.isSigned)));
    }
    llvm::Optional<IntType> src = IntTypeOf(x.a);
    if (a.kind == IntVal::Kind::Exact && src) {
        bool preserving = (src->isSigned == x.isSigned && x.width >= src->width) ||
                          (!src->isSigned && x.isSigned && x.width > src->width);
        if (preserving) {
            return a;
        }
    }
    if (a.kind == IntVal::Kind::Range) {
        IntVal t = TypeRange(x.width, x.isSigned, Tag::Type);
        llvm::Optional<LinExpr> lo = a.Lower();
        llvm::Optional<LinExpr> hi = a.Upper();
        bool fits = lo && hi && lo->IsConst() && hi->IsConst() && t.kind == IntVal::Kind::Range &&
                    lo->Constant() >= t.lo.Constant() && hi->Constant() <= t.hi.Constant();
        if (fits) {
            return a;
        }
    }
    return TypeRange(x.width, x.isSigned, Tag::Type);  // A-27
}

Value Analyzer::Eval(ExprId e, const State &s)
{
    Value v;
    if (e >= f.exprs.size()) {
        v.i = IntVal::Top("bad_expr");
        return v;
    }
    const fir::Expr &x = f.exprs[e];
    switch (x.op) {
        case ExprOp::Const:
            v.i = IntVal::Exact(LinExpr::Const(x.value));
            return v;
        case ExprOp::Local: {
            auto it = s.locals.find(static_cast<uint32_t>(x.value));
            if (it != s.locals.end()) {
                return it->second;
            }
            v.isPtr = IsPtrLocal(static_cast<uint32_t>(x.value));
            v.p = UnknownPtr("uninit");
            v.i = IntVal::Top("uninit");
            return v;
        }
        case ExprOp::Bin:
            v.i = Bin(x, s);
            return v;
        case ExprOp::Cmp:
        case ExprOp::LNot:
            v.i = IntVal::Range(LinExpr::Const(0), LinExpr::Const(1), Tag::None);
            return v;
        case ExprOp::Neg:
            v.i = NegVal(EvalInt(x.a, s));
            return v;
        case ExprOp::BNot:
            v.i = IntVal::Top("bnot");
            return v;
        case ExprOp::Cast:
            v.i = Cast(x, s);
            return v;
        case ExprOp::LAddr: {
            const fir::Local *l = f.FindLocal(static_cast<uint32_t>(x.value));
            int64_t size = 0;
            bool known = l != nullptr && TypeSize(m, l->type, size);
            v.isPtr = true;
            v.p = Base(BaseKind::Local, "L" + std::to_string(x.value),
                       "local:" + (l != nullptr ? l->name : std::string("?")),
                       known ? llvm::Optional<LinExpr>(LinExpr::Const(size)) : llvm::None);
            return v;
        }
        case ExprOp::GAddr: {
            v.isPtr = true;
            const fir::Global *g = nullptr;
            for (const fir::Global &x2 : m.globals) {
                if (x2.key == x.text) {
                    g = &x2;
                }
            }
            if (g == nullptr) {
                v.p = UnknownPtr("unknown_global");
                return v;
            }
            int64_t size = 0;
            bool known = TypeSize(m, g->type, size);
            v.p = Base(BaseKind::Global, "G" + g->key, "g:" + g->name,
                       known ? llvm::Optional<LinExpr>(LinExpr::Const(size)) : llvm::None);
            return v;
        }
        case ExprOp::Str:
            v.isPtr = true;
            v.p = Base(BaseKind::Str, "S" + std::to_string(e), "g:__str", LinExpr::Const(x.value + 1));
            return v;
        case ExprOp::FAddr:
            v.isPtr = true;
            v.p = Base(BaseKind::Func, "F" + x.text, "", llvm::None);
            return v;
        case ExprOp::PAdd: {
            PtrVal p = EvalPtr(x.a, s);
            if (p.kind != BaseKind::Unknown) {
                p.off = AddVals(p.off, EvalInt(x.b, s));
            }
            v.isPtr = true;
            v.p = p;
            return v;
        }
        case ExprOp::Fld: {
            PtrVal p = EvalPtr(x.a, s);
            v.isPtr = true;
            // 形参所指结构体里的数组成员：以成员为界（D-20 的 p0->aucBody、D-31）；其余基址本步只加偏移（WP9）
            if (p.kind == BaseKind::Param && p.off.IsConst() && p.off.value.Constant() == 0) {
                std::string param = p.obj.substr(1);  // "*p0" → "p0"
                v.p = Base(BaseKind::ParamField, p.id + "." + x.text, param + "->" + x.text, LinExpr::Const(x.size));
                return v;
            }
            if (p.kind != BaseKind::Unknown) {
                p.off = AddVals(p.off, IntVal::Exact(LinExpr::Const(x.value)));
            }
            v.p = p;
            return v;
        }
        case ExprOp::PSub: {
            PtrVal a = EvalPtr(x.a, s);
            PtrVal b = EvalPtr(x.b, s);
            if (a.kind != BaseKind::Unknown && a.id == b.id) {
                v.i = DivConst(AddVals(a.off, NegVal(b.off)), x.value);
            } else {
                v.i = IntVal::Top("psub");
            }
            return v;
        }
        case ExprOp::Unk:
            v.i = IntVal::Top(x.text);
            v.p = UnknownPtr(x.text);
            return v;
    }
    v.i = IntVal::Top("expr");
    return v;
}

// load 的值：命中格；形参所指结构体的字段 → pi->字段（指针字段 → *pi->字段 的基址）；全局标量 → g:名（A-28）；其余 data
Value Analyzer::LoadValue(const PtrVal &addr, int64_t size, uint32_t dst, const State &s) const
{
    Value v;
    v.isPtr = IsPtrLocal(dst);
    llvm::Optional<int64_t> off;
    if (addr.off.IsConst()) {
        off = addr.off.value.Constant();
    }
    if (addr.kind != BaseKind::Unknown && off) {
        auto it = s.cells.find(CellKey(addr.id, *off, size));
        if (it != s.cells.end()) {
            return it->second;
        }
    }
    if (addr.kind == BaseKind::Param && off) {
        // 形参的类型 → 所指的记录 → 这个偏移处的字段
        int param = std::atoi(addr.obj.c_str() + 2);  // "*p0"
        for (const fir::Local &l : f.locals) {
            if (l.param != param || l.type >= m.types.size() || m.types[l.type].kind != fir::TypeKind::Ptr) {
                continue;
            }
            fir::TypeId pointee = m.types[l.type].to;
            if (pointee >= m.types.size() || m.types[pointee].kind != fir::TypeKind::Rec) {
                break;
            }
            const fir::Record *r = m.FindRecord(m.types[pointee].record);
            for (const fir::Field &field : r != nullptr ? r->fields : std::vector<fir::Field>()) {
                int64_t fieldSize = 0;
                if (field.off != *off || !TypeSize(m, field.type, fieldSize) || fieldSize != size) {
                    continue;
                }
                std::string name = "p" + std::to_string(param) + "->" + field.name;
                const fir::Type &ft = m.types[field.type];
                summary::FieldInfo info;
                info.off = field.off;
                info.size = fieldSize;
                info.type = fir::TypeText(m, field.type);
                fields[name] = info;
                if (ft.kind == fir::TypeKind::Ptr) {
                    v.isPtr = true;
                    v.p = Base(BaseKind::ParamPtr, "PP" + std::to_string(param) + "." + field.name, "*" + name,
                               LinExpr::Ext(name));
                    return v;
                }
                if (ft.kind == fir::TypeKind::Int) {
                    v.i = IntVal::Exact(LinExpr::Sym(name));
                    return v;
                }
            }
            break;
        }
    }
    if (addr.kind == BaseKind::Global && off && *off == 0 && !v.isPtr) {
        for (const fir::Global &g : m.globals) {
            int64_t gsize = 0;
            if ("G" + g.key == addr.id && g.type < m.types.size() && m.types[g.type].kind == fir::TypeKind::Int &&
                TypeSize(m, g.type, gsize) && gsize == size) {
                v.i = IntVal::Exact(LinExpr::Sym("g:" + g.name));
                return v;
            }
        }
    }
    if (v.isPtr) {
        v.p = UnknownPtr("data");
        return v;
    }
    const fir::Type *t = LocalType(dst);
    v.i = t != nullptr && t->kind == fir::TypeKind::Int ? TypeRange(t->width, t->isSigned, Tag::Data)
                                                        : IntVal::Top("data");
    return v;
}

void Analyzer::Access(const char *kind, const PtrVal &addr, int64_t size, const fir::Loc &loc, const State &s)
{
    std::string at = SiteOf(m, loc);
    auto open = [&](const char *reason) {
        summary::Open o;
        o.reason = reason;
        o.at = at;
        opens.push_back(o);
    };
    if (addr.kind == BaseKind::Unknown || addr.kind == BaseKind::Func) {
        open("untracked_pointer");
        return;
    }
    if (!addr.hasSize) {
        open("alloc_size_unknown");
        return;
    }
    llvm::Optional<LinExpr> hi = addr.off.Upper();
    if (!hi) {
        open("type_range_only");
        return;
    }
    llvm::Optional<LinExpr> lo = addr.off.Lower();
    RawAccess a;
    a.kind = kind;
    a.have = addr.size;
    a.obj = addr.obj;
    a.hi = *hi + LinExpr::Const(size);
    a.lo = (lo ? *lo : LinExpr::Const(0)) + LinExpr::Const(size);
    a.start = lo;
    a.tag = addr.off.kind == IntVal::Kind::Exact ? Tag::Exact : addr.off.tag;
    a.at = at;
    if (a.hi.IsUnknown() || a.lo.IsUnknown()) {
        open("type_range_only");
        return;
    }
    std::set<std::string> syms;
    CollectSyms(a.have, syms);
    CollectSyms(a.lo, syms);
    CollectSyms(a.hi, syms);
    for (const LinExpr &g : s.guards) {
        std::set<std::string> gs;
        CollectSyms(g, gs);
        if (llvm::any_of(gs, [&](const std::string &x) { return syms.count(x) != 0; })) {
            a.guard.push_back(ShowLe0(g));
        }
    }
    // A-43 / A-44：末端上界 ≤ have 在守卫与类型范围下恒成立 → 安全，不生成 Req（SPEC-0016 §2.2）
    LinExpr need = a.hi - a.have;
    if (!need.IsUnknown() && EntailsLe0(need, s.guards, Env(s))) {
        if (trace != nullptr) {
            trace->push_back("safe " + a.at + " " + a.kind + " end " + a.hi.Show() + " <= have " + a.have.Show());
        }
        safe.push_back(std::move(a));
        return;
    }
    accesses.push_back(std::move(a));
}

void Analyzer::Step(const fir::Inst &inst, State &s)
{
    switch (inst.op) {
        case fir::InstOp::Set:
            SetLocal(s, inst.dst, Eval(inst.val, s));
            return;
        case fir::InstOp::Load: {
            PtrVal addr = EvalPtr(inst.addr, s);
            Access("R", addr, inst.size, inst.loc, s);
            SetLocal(s, inst.dst, LoadValue(addr, inst.size, inst.dst, s));
            return;
        }
        case fir::InstOp::Store: {
            PtrVal addr = EvalPtr(inst.addr, s);
            Access("W", addr, inst.size, inst.loc, s);
            if (addr.kind == BaseKind::Unknown) {
                s.cells.clear();  // A-12：经未知基址的写
                return;
            }
            // 失效同一基址上的格，常数偏移时再强更新
            for (auto it = s.cells.begin(); it != s.cells.end();) {
                it = std::get<0>(it->first) == addr.id ? s.cells.erase(it) : std::next(it);
            }
            if (addr.off.IsConst() && inst.val != fir::kNone) {
                s.cells[CellKey(addr.id, addr.off.value.Constant(), inst.size)] = Eval(inst.val, s);
            }
            return;
        }
        case fir::InstOp::Call:
            RecordCall(inst, s);
            s.cells.clear();  // 被调者可能经指针 / 全局写内存（效果在 S7d）
            if (inst.dst != fir::kNone) {
                Value v;
                v.isPtr = IsPtrLocal(inst.dst);
                v.p = UnknownPtr("call");
                v.i = IntVal::Top("call");
                s.locals[inst.dst] = v;
            }
            return;
        case fir::InstOp::Havoc: {
            Value v;
            v.isPtr = IsPtrLocal(inst.dst);
            v.p = UnknownPtr(inst.why);
            v.i = IntVal::Top(inst.why);
            s.locals[inst.dst] = v;
            return;
        }
        case fir::InstOp::Ret:
            return;
    }
}

State Analyzer::Entry() const
{
    State s;
    s.reached = true;
    for (const fir::Local &l : f.locals) {
        if (l.param < 0) {
            continue;
        }
        Value v;
        std::string p = "p" + std::to_string(l.param);
        if (IsPtrLocal(l.id)) {
            v.isPtr = true;
            v.p = Base(BaseKind::Param, "P" + std::to_string(l.param), "*" + p, LinExpr::Ext(p));
        } else {
            v.i = IntVal::Exact(LinExpr::Sym(p));
        }
        s.locals[l.id] = v;
    }
    return s;
}

// A-44 覆盖合并（SPEC-0014 §2.4）：同一 have / 对象 / 守卫，end 上界恒不大于另一条的并进去；差为 0 时写优先，再按出现顺序
std::vector<summary::Req> Analyzer::Merge() const
{
    auto group = [&](const RawAccess &a, const RawAccess &b) {
        return a.have == b.have && a.obj == b.obj && a.guard == b.guard;  // 守卫不同的不合并
    };
    auto beats = [&](size_t j, size_t i) {  // j 把 i 并掉？
        const RawAccess &a = accesses[i];
        const RawAccess &b = accesses[j];
        if (i == j || !group(a, b)) {
            return false;
        }
        LinExpr d = a.hi - b.hi;
        if (!d.IsConst() || d.Constant() > 0) {
            return false;
        }
        if (d.Constant() < 0) {
            return true;
        }
        if (a.kind != b.kind) {
            return b.kind == "W";
        }
        return j < i;
    };
    std::vector<summary::Req> out;
    std::vector<size_t> kept;
    for (size_t i = 0; i < accesses.size(); ++i) {
        bool dominated = false;
        for (size_t j = 0; j < accesses.size() && !dominated; ++j) {
            dominated = beats(j, i);
        }
        if (!dominated) {
            kept.push_back(i);
        }
    }
    for (size_t k : kept) {
        const RawAccess &a = accesses[k];
        summary::Req r = ToReq(a, out.size() + 1);
        std::vector<summary::Absorbed> absorbed;
        for (size_t i = 0; i < accesses.size(); ++i) {
            if (i != k && beats(k, i)) {
                absorbed.push_back({accesses[i].kind, accesses[i].lo.Show(), accesses[i].hi.Show(), accesses[i].at});
            }
        }
        if (!absorbed.empty()) {
            r.absorbed = absorbed;
        }
        out.push_back(std::move(r));
    }
    return out;
}

summary::Req Analyzer::ToReq(const RawAccess &a, size_t number) const
{
    summary::Req r;
    r.id = "R" + std::to_string(number);
    r.kind = a.kind;
    r.have = a.have.Show();
    r.obj = a.obj;
    r.endLo = a.lo.Show();
    r.endHi = a.hi.Show();
    if (a.start) {
        r.start = a.start->Show();
    }
    r.bound = TagName(a.tag).str();
    r.guard = a.guard;
    r.access = {f.name, f.key, a.at, r.endHi};
    r.path = {f.name + "@" + a.at};
    return r;
}

FunctionFacts Analyzer::Run()
{
    FunctionFacts facts;
    llvm::DenseMap<uint32_t, size_t> index;
    for (size_t i = 0; i < f.blocks.size(); ++i) {
        index[f.blocks[i].id] = i;
    }
    // 逆后序；有回边（DFS 栈上的后继）的函数本步不分析（SPEC-0014 §2.3）
    std::vector<int> mark(f.blocks.size(), 0);  // 0 未访问、1 在栈上、2 完成
    std::vector<size_t> post;
    bool loop = false;
    std::function<void(size_t)> dfs = [&](size_t b) {
        mark[b] = 1;
        for (const fir::Edge &e : f.blocks[b].succ) {
            auto it = index.find(e.to);
            if (it == index.end()) {
                continue;
            }
            if (mark[it->second] == 1) {
                loop = true;
            } else if (mark[it->second] == 0) {
                dfs(it->second);
            }
        }
        mark[b] = 2;
        post.push_back(b);
    };
    auto entry = index.find(f.entry);
    if (entry == index.end()) {
        facts.status = "skipped:no_body";
        return facts;
    }
    dfs(entry->second);
    if (loop) {
        facts.status = "partial:loop_pending";
        return facts;
    }
    std::vector<State> in(f.blocks.size());
    in[entry->second] = Entry();
    for (auto it = post.rbegin(); it != post.rend(); ++it) {
        State s = in[*it];
        if (!s.reached) {
            continue;
        }
        if (trace != nullptr) {
            trace->push_back(ShowState(f.blocks[*it], s));
        }
        for (const fir::Inst &inst : f.blocks[*it].insts) {
            Step(inst, s);
        }
        for (const fir::Edge &e : f.blocks[*it].succ) {
            auto t = index.find(e.to);
            if (t == index.end()) {
                continue;
            }
            if (e.assume == fir::kNone) {
                in[t->second] = Join(in[t->second], s);
                continue;
            }
            State taken = s;
            if (Assume(e.assume, false, taken)) {
                in[t->second] = Join(in[t->second], taken);
            }
        }
    }
    facts.reqs = Merge();
    for (size_t i = 0; i < safe.size(); ++i) {
        facts.safe.push_back(ToReq(safe[i], i + 1));
    }
    facts.calls = calls;
    facts.fields = fields;
    facts.opens = opens;
    return facts;
}

// 调用点记录（D-25，SPEC-0015 §2.1）：整数实参写表达式（区间写成局部名 + locals）；指针实参写 {obj, have, alias?}；
// 纯函数模型的指针实参为 null；不知道的值写成不是接口符号的名字（_unknown<k>），以后在调用点只会走向 entry_unproven
void Analyzer::RecordCall(const fir::Inst &inst, const State &s)
{
    if (inst.calleeExpr != fir::kNone) {
        return;  // 间接调用（fptr）留到 S9
    }
    summary::Call call;
    call.site = SiteOf(m, inst.loc);
    call.callee = KeyName(inst.callee).str();
    bool defined = llvm::any_of(m.funcs, [&](const fir::Function &g) { return g.key == inst.callee; });
    bool pure = !defined && IsPureModel(call.callee);
    if (pure) {
        call.model = std::string("pure");
    }
    for (size_t k = 0; k < inst.args.size(); ++k) {
        Value v = Eval(inst.args[k], s);
        summary::ArgValue arg;
        std::string unknown = "_unknown" + std::to_string(k);
        if (v.isPtr) {
            if (pure) {
                arg.kind = summary::ArgValue::Kind::Null;
            } else {
                arg.kind = summary::ArgValue::Kind::Ptr;
                arg.ptr = std::make_shared<summary::PtrArg>();
                llvm::Optional<LinExpr> hi = v.p.off.Upper();
                bool known = v.p.kind != BaseKind::Unknown && v.p.kind != BaseKind::Func && v.p.hasSize && hi;
                if (known) {
                    arg.ptr->obj = v.p.obj;
                    arg.ptr->have = (v.p.size - *hi).Show();  // 偏移是区间时取最小的剩余（多报不少报）
                    if (v.p.kind == BaseKind::Param && v.p.off.IsConst() && v.p.off.value.Constant() == 0) {
                        arg.ptr->alias = v.p.obj.substr(1);  // *p0 → p0
                    }
                } else {
                    arg.ptr->obj = "local:" + unknown;
                    arg.ptr->have = unknown;
                }
            }
        } else if (v.i.kind == IntVal::Kind::Exact) {
            arg.kind = summary::ArgValue::Kind::Expr;
            arg.expr = v.i.value.Show();
        } else {
            arg.kind = summary::ArgValue::Kind::Expr;
            llvm::Optional<LinExpr> lo = v.i.Lower();
            llvm::Optional<LinExpr> hi = v.i.Upper();
            if (v.i.kind == IntVal::Kind::Range && lo && hi) {
                const fir::Expr &x = f.exprs[inst.args[k]];
                const fir::Local *l = x.op == ExprOp::Local ? f.FindLocal(static_cast<uint32_t>(x.value)) : nullptr;
                std::string name = l != nullptr && !l->tmp ? l->name : "_arg" + std::to_string(k);
                if (!call.locals) {
                    call.locals = std::map<std::string, summary::Local>();
                }
                (*call.locals)[name] = {lo->Show(), hi->Show(),
                                        TagName(v.i.tag == Tag::None ? Tag::Branch : v.i.tag).str()};
                arg.expr = name;
            } else {
                arg.expr = unknown;
            }
        }
        call.args.push_back(std::move(arg));
    }
    if (!s.guards.empty()) {
        std::vector<summary::CallGuard> guards;
        for (const LinExpr &g : s.guards) {
            guards.push_back({ShowLe0(g), "check"});  // 调用点上的环境守卫（D-25）
        }
        call.guards = guards;
    }
    calls.push_back(std::move(call));
}

// ---- 条件边与守卫（SPEC-0016）----

// 接口符号的取值范围：形参、用到的字段、全局的类型范围，加上当前的守卫
SymEnv Analyzer::Env(const State &s) const
{
    SymEnv env;
    int64_t lo = 0;
    int64_t hi = 0;
    for (const fir::Local &l : f.locals) {
        if (l.param >= 0 && TypeRangeOf(fir::TypeText(m, l.type), lo, hi)) {
            env.SetTypeRange("p" + std::to_string(l.param), lo, hi);
        }
    }
    for (const auto &field : fields) {
        if (field.second.type && TypeRangeOf(*field.second.type, lo, hi)) {
            env.SetTypeRange(field.first, lo, hi);
        }
    }
    for (const fir::Global &g : m.globals) {
        if (g.type < m.types.size() && m.types[g.type].kind == fir::TypeKind::Int &&
            TypeRangeOf(fir::TypeText(m, g.type), lo, hi)) {
            env.SetTypeRange("g:" + g.name, lo, hi);
        }
    }
    for (const LinExpr &g : s.guards) {
        env.AddGuard(g, Tag::Check);
    }
    return env;
}

// 比较的一边算出来的值就是 C 的值：常数；或能证明落在它的 C 类型范围内（无符号加法 / 有符号运算可能溢出时不是——
// if (off + len > max) 这种检查本身可能被回绕绕过，不能当守卫，SPEC-0016 §2.1）
bool Analyzer::Faithful(ExprId e, const IntVal &v, const SymEnv &env) const
{
    if (v.IsConst()) {
        return true;
    }
    llvm::Optional<IntType> t = IntTypeOf(e);
    if (!t) {
        return false;
    }
    if (t->width >= 63) {
        return true;  // 64 位：线性式在 int64 里溢出时已成为未知
    }
    IntVal range = TypeRange(t->width, t->isSigned, Tag::Type);
    Limit lo;
    Limit hi;
    if (v.kind == IntVal::Kind::Exact) {
        lo = env.RangeOf(v.value, false);
        hi = env.RangeOf(v.value, true);
    } else if (v.kind == IntVal::Kind::Range && v.hasLo && v.hasHi) {
        lo = env.RangeOf(v.lo, false);
        hi = env.RangeOf(v.hi, true);
    }
    return lo.known && hi.known && lo.value >= range.lo.Constant() && hi.value <= range.hi.Constant();
}

void Analyzer::AddGuard(State &s, const LinExpr &le0)
{
    if (llvm::is_contained(s.guards, le0)) {
        return;
    }
    s.guards.push_back(le0);
    if (s.guards.size() > kMaxGuards) {
        s.guards.erase(s.guards.begin());  // 先进先出（A-14）
    }
}

// 条件 → 状态：比较、!x、条件的按位与（switch 的 default）；其余的值 v 当成 v != 0。返回 false = 这条边不可达
bool Analyzer::Assume(ExprId c, bool negate, State &s)
{
    if (c >= f.exprs.size()) {
        return true;
    }
    const fir::Expr &x = f.exprs[c];
    if (x.op == ExprOp::LNot) {
        return Assume(x.a, !negate, s);
    }
    if (x.op == ExprOp::Cmp) {
        static const fir::CmpOp flipped[] = {fir::CmpOp::Ne, fir::CmpOp::Eq, fir::CmpOp::Ge,
                                             fir::CmpOp::Gt, fir::CmpOp::Le, fir::CmpOp::Lt};
        fir::CmpOp op = static_cast<fir::CmpOp>(x.sub);
        return AssumeCmp(negate ? flipped[x.sub] : op, x.a, x.b, s);
    }
    if (IsCondExpr(f, c)) {  // 条件的按位与
        if (negate) {
            return true;  // ¬(A ∧ B) 是析取，不产生信息
        }
        return Assume(x.a, false, s) && Assume(x.b, false, s);
    }
    return AssumeCmp(negate ? fir::CmpOp::Eq : fir::CmpOp::Ne, c, fir::kNone, s);
}

// a op b（b 为 kNone 时是常数 0）。两边都是确切值 → 原子（常数为假 → 不可达）；一边是区间、一边是确切值 → 收窄区间
bool Analyzer::AssumeCmp(fir::CmpOp op, ExprId a, ExprId b, State &s)
{
    Value va = Eval(a, s);
    Value vb;
    vb.i = IntVal::Exact(LinExpr::Const(0));
    if (b != fir::kNone) {
        vb = Eval(b, s);
    }
    if (va.isPtr || vb.isPtr) {
        return true;  // 指针比较本步不产生信息
    }
    SymEnv env = Env(s);
    if (!Faithful(a, va.i, env) || (b != fir::kNone && !Faithful(b, vb.i, env))) {
        return true;
    }
    const IntVal &ia = va.i;
    const IntVal &ib = vb.i;
    if (ia.kind == IntVal::Kind::Exact && ib.kind == IntVal::Kind::Exact) {
        LinExpr d = ia.value - ib.value;  // a − b
        if (d.IsUnknown()) {
            return true;
        }
        LinExpr one = LinExpr::Const(1);
        std::vector<LinExpr> atoms;
        switch (op) {
            case fir::CmpOp::Lt:
                atoms = {d + one};
                break;
            case fir::CmpOp::Le:
                atoms = {d};
                break;
            case fir::CmpOp::Gt:
                atoms = {one - d};
                break;
            case fir::CmpOp::Ge:
                atoms = {d.Scale(-1)};
                break;
            case fir::CmpOp::Eq:
                atoms = {d, d.Scale(-1)};
                break;
            case fir::CmpOp::Ne: {
                if (d.IsConst()) {
                    return d.Constant() != 0;
                }
                // != 不是线性约束：只在 a − b 的界正好是 0 时推出严格不等式（u32 的 n != 0 → n >= 1）
                Limit lo = env.RangeOf(d, false);
                Limit hi = env.RangeOf(d, true);
                if (lo.known && hi.known && lo.value == 0 && hi.value == 0) {
                    return false;
                }
                if (lo.known && lo.value == 0) {
                    atoms = {one - d};
                } else if (hi.known && hi.value == 0) {
                    atoms = {d + one};
                }
                break;
            }
        }
        for (const LinExpr &atom : atoms) {
            if (atom.IsUnknown()) {
                continue;
            }
            if (atom.IsConst()) {
                if (atom.Constant() > 0) {
                    return false;
                }
                continue;
            }
            AddGuard(s, atom);
        }
        return true;
    }
    static const fir::CmpOp mirrored[] = {fir::CmpOp::Eq, fir::CmpOp::Ne, fir::CmpOp::Gt,
                                          fir::CmpOp::Ge, fir::CmpOp::Lt, fir::CmpOp::Le};
    if (ia.kind == IntVal::Kind::Range && ib.kind == IntVal::Kind::Exact) {
        return Narrow(a, op, ib.value, s);
    }
    if (ib.kind == IntVal::Kind::Range && ia.kind == IntVal::Kind::Exact && b != fir::kNone) {
        return Narrow(b, mirrored[static_cast<int>(op)], ia.value, s);
    }
    return true;
}

// e op c，e 的值是区间：收窄（e 是局部、或保值的类型转换下的局部时写回）；区间为空 → 不可达
bool Analyzer::Narrow(ExprId e, fir::CmpOp op, const LinExpr &c, State &s)
{
    IntVal v = EvalInt(e, s);
    llvm::Optional<LinExpr> lo = v.Lower();
    llvm::Optional<LinExpr> hi = v.Upper();
    llvm::Optional<LinExpr> newLo;
    llvm::Optional<LinExpr> newHi;
    LinExpr one = LinExpr::Const(1);
    auto same = [](const llvm::Optional<LinExpr> &x, const LinExpr &y) {
        return x && (*x - y).IsConst() && (*x - y).Constant() == 0;
    };
    switch (op) {
        case fir::CmpOp::Lt:
            newHi = c - one;
            break;
        case fir::CmpOp::Le:
            newHi = c;
            break;
        case fir::CmpOp::Gt:
            newLo = c + one;
            break;
        case fir::CmpOp::Ge:
            newLo = c;
            break;
        case fir::CmpOp::Eq:
            newLo = c;
            newHi = c;
            break;
        case fir::CmpOp::Ne:
            if (same(lo, c)) {
                newLo = c + one;
            } else if (same(hi, c)) {
                newHi = c - one;
            }
            break;
    }
    if (!newLo && !newHi) {
        return true;
    }
    // 与原来的界取更紧的；两者不可比时留原来的（都是对的界）
    auto tighter = [](const llvm::Optional<LinExpr> &old, const llvm::Optional<LinExpr> &cand, bool upper) {
        if (!cand || cand->IsUnknown()) {
            return old;
        }
        if (!old) {
            return cand;
        }
        LinExpr d = *cand - *old;
        if (!d.IsConst()) {
            return old;
        }
        return (upper ? d.Constant() < 0 : d.Constant() > 0) ? cand : old;
    };
    llvm::Optional<LinExpr> l = tighter(lo, newLo, false);
    llvm::Optional<LinExpr> h = tighter(hi, newHi, true);
    if (l && h) {
        LinExpr width = *h - *l;
        if (width.IsConst() && width.Constant() < 0) {
            return false;
        }
    }
    // 写回局部：跳过保值的类型转换（转换的值与里面的值相同）
    ExprId cur = e;
    while (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Cast && EvalInt(f.exprs[cur].a, s) == v) {
        cur = f.exprs[cur].a;
    }
    if (cur >= f.exprs.size() || f.exprs[cur].op != ExprOp::Local) {
        return true;
    }
    Value out;
    if (l && h && same(l, *h)) {
        out.i = IntVal::Exact(*l);
    } else {
        // 标签说的是上界的来源（F-12 只看上界，同 replay.py）：上界被代码里的检查收窄 → check；只收窄了下界 → 原标签不变
        bool hiNarrowed = h && (!hi || !(*h == *hi));
        Tag tag = !hiNarrowed ? v.tag : (IsCodeTag(v.tag) ? WorseTag(v.tag, Tag::Check) : Tag::Check);
        out.i = IntVal::Range(l, h, tag);
    }
    s.locals[static_cast<uint32_t>(f.exprs[cur].value)] = out;
    return true;
}

std::string Analyzer::ShowState(const fir::Block &b, const State &s) const
{
    std::string out = "b" + std::to_string(b.id) + ":";
    for (const auto &entry : s.locals) {
        const fir::Local *l = f.FindLocal(entry.first);
        if (l == nullptr || l->tmp) {
            continue;
        }
        const Value &v = entry.second;
        std::string text = v.isPtr
                               ? (v.p.kind == BaseKind::Unknown ? "?(" + v.p.why + ")" : v.p.obj + "+" + v.p.off.Show())
                               : v.i.Show();
        out += " " + l->name + "=" + text;
    }
    for (const auto &cell : s.cells) {
        const Value &v = cell.second;
        out += " [" + std::get<0>(cell.first) + "+" + std::to_string(std::get<1>(cell.first)) + ":" +
               std::to_string(std::get<2>(cell.first)) + "]=" + (v.isPtr ? v.p.obj : v.i.Show());
    }
    if (!s.guards.empty()) {
        out += " guards:";
        for (size_t i = 0; i < s.guards.size(); ++i) {
            out += (i == 0 ? " " : ", ") + ShowLe0(s.guards[i]);
        }
    }
    return out;
}

}  // namespace

bool IsPureModel(llvm::StringRef name)
{
    // 日志不接收任何会流进内存访问的东西（cases/common.h）；WP5 的模型表接上后由 config 提供（I8 / I9）
    static const char *const pure[] = {"LOG_Print"};
    return llvm::is_contained(pure, name);
}

FunctionFacts AnalyzeFunction(const fir::Module &m, const fir::Function &f, std::vector<std::string> *stateTrace)
{
    Analyzer a(m, f);
    a.trace = stateTrace;
    return a.Run();
}

summary::Summary BuildSummary(const fir::Module &m, const fir::Function &f, const FunctionFacts &facts)
{
    summary::Summary s;
    s.fn = f.name;
    s.key = f.key;
    s.loc = SiteOf(m, f.loc);
    std::vector<const fir::Local *> params;
    for (const fir::Local &l : f.locals) {
        if (l.param >= 0) {
            params.push_back(&l);
        }
    }
    std::sort(params.begin(), params.end(),
              [](const fir::Local *a, const fir::Local *b) { return a->param < b->param; });
    for (const fir::Local *l : params) {
        s.params.push_back({l->name, fir::TypeText(m, l->type)});
    }
    if (!facts.fields.empty()) {
        s.fields = facts.fields;
    }
    s.status = facts.status;
    s.reqs = facts.reqs;
    s.calls = facts.calls;
    s.opens = facts.opens;
    return s;
}

}  // namespace interp
}  // namespace oobsum
