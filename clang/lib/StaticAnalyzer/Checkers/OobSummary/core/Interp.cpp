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

// 比较符号界的环境（SPEC-0023）：类型范围与守卫；没有就只认常数差
struct Comparer {
    const SymEnv *env = nullptr;
    const std::vector<LinExpr> *guards = nullptr;
};

// x <= y 恒成立：差是常数，或在类型范围与守卫下可证
bool ProvablyLe(const LinExpr &x, const LinExpr &y, const Comparer *cmp)
{
    LinExpr d = x - y;
    if (d.IsUnknown()) {
        return false;
    }
    if (d.IsConst()) {
        return d.Constant() <= 0;
    }
    static const std::vector<LinExpr> none;
    return cmp != nullptr && cmp->env != nullptr &&
           EntailsLe0(d, cmp->guards != nullptr ? *cmp->guards : none, *cmp->env);
}

// 两个值合并（A-15 的子集）
llvm::Optional<LinExpr> MinOpt(const llvm::Optional<LinExpr> &a, const llvm::Optional<LinExpr> &b,
                               const Comparer *cmp = nullptr)
{
    if (!a || !b) {
        return llvm::None;
    }
    if (ProvablyLe(*a, *b, cmp)) {
        return *a;
    }
    if (ProvablyLe(*b, *a, cmp)) {
        return *b;
    }
    return llvm::None;
}

llvm::Optional<LinExpr> MaxOpt(const llvm::Optional<LinExpr> &a, const llvm::Optional<LinExpr> &b,
                               const Comparer *cmp = nullptr)
{
    if (!a || !b) {
        return llvm::None;
    }
    if (ProvablyLe(*b, *a, cmp)) {
        return *a;
    }
    if (ProvablyLe(*a, *b, cmp)) {
        return *b;
    }
    return llvm::None;
}

IntVal JoinInt(const IntVal &a, const IntVal &b, const Comparer *cmp = nullptr)
{
    if (a == b) {
        return a;
    }
    if (a.kind == IntVal::Kind::Top || b.kind == IntVal::Kind::Top) {
        return IntVal::Top("imprecise_join");
    }
    llvm::Optional<LinExpr> lo = MinOpt(a.Lower(), b.Lower(), cmp);
    llvm::Optional<LinExpr> hi = MaxOpt(a.Upper(), b.Upper(), cmp);
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

Value JoinValue(const Value &a, const Value &b, const Comparer *cmp = nullptr)
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
        out.i = JoinInt(a.i, b.i, cmp);
        return out;
    }
    if (a.p.kind == BaseKind::Unknown || b.p.kind == BaseKind::Unknown || a.p.id != b.p.id) {
        out.p = UnknownPtr("base_join");
        return out;
    }
    out.p = a.p;
    out.p.off = JoinInt(a.p.off, b.p.off, cmp);
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
const int kMaxHeadUpdates = 20;  // A-33：每个循环头至多更新 20 次
const int kNarrowPasses = 2;     // 下降的遍数（SPEC-0017 §2.1）

bool operator==(const State &a, const State &b)
{
    return a.reached == b.reached && a.locals == b.locals && a.cells == b.cells && a.guards == b.guards;
}

// envOf：给定汇合后成立的守卫（两边的交集），建比较符号界的环境（SPEC-0023）；空 = 只认常数差
State Join(const State &a, const State &b, const std::function<SymEnv(const std::vector<LinExpr> &)> &envOf = nullptr)
{
    if (!a.reached) {
        return b;
    }
    if (!b.reached) {
        return a;
    }
    State out;
    out.reached = true;
    for (const LinExpr &g : a.guards) {
        if (llvm::is_contained(b.guards, g)) {
            out.guards.push_back(g);  // A-15：守卫取交集
        }
    }
    SymEnv env;
    Comparer cmp;
    if (envOf) {
        env = envOf(out.guards);
        cmp.env = &env;
        cmp.guards = &out.guards;
    }
    for (const auto &entry : a.locals) {
        auto it = b.locals.find(entry.first);
        out.locals[entry.first] = it == b.locals.end() ? Value() : JoinValue(entry.second, it->second, &cmp);
    }
    for (const auto &entry : a.cells) {
        auto it = b.cells.find(entry.first);
        if (it != b.cells.end() && it->second == entry.second) {
            out.cells.insert(entry);  // 两边都有且相同才保留
        }
    }
    return out;
}

// widening（SPEC-0017 §2.1）：b = a ⊔ 新的；下界变小 / 不可比 → 去掉，上界变大 / 不可比 → 去掉并标 widen
IntVal WidenInt(const IntVal &a, const IntVal &b)
{
    if (a == b) {
        return a;
    }
    if (a.kind == IntVal::Kind::Top || b.kind == IntVal::Kind::Top) {
        return b.kind == IntVal::Kind::Top ? b : IntVal::Top("widen");
    }
    llvm::Optional<LinExpr> al = a.Lower();
    llvm::Optional<LinExpr> bl = b.Lower();
    llvm::Optional<LinExpr> ah = a.Upper();
    llvm::Optional<LinExpr> bh = b.Upper();
    auto diff = [](const llvm::Optional<LinExpr> &x, const llvm::Optional<LinExpr> &y, int64_t &out) {
        if (!x || !y) {
            return false;
        }
        LinExpr d = *x - *y;
        if (!d.IsConst()) {
            return false;
        }
        out = d.Constant();
        return true;
    };
    int64_t d = 0;
    bool keepLo = diff(bl, al, d) && d >= 0;
    bool keepHi = diff(bh, ah, d) && d <= 0;
    Tag ta = a.kind == IntVal::Kind::Exact ? Tag::Branch : a.tag;
    Tag tb = b.kind == IntVal::Kind::Exact ? Tag::Branch : b.tag;
    Tag tag = keepHi ? WorseTag(ta, tb) : Tag::Widen;
    return IntVal::Range(keepLo ? al : llvm::None, keepHi ? ah : llvm::None, tag);
}

Value WidenValue(const Value &a, const Value &b)
{
    if (a == b || a.isPtr != b.isPtr) {
        return b;
    }
    Value out = b;
    if (!b.isPtr) {
        out.i = WidenInt(a.i, b.i);
        return out;
    }
    if (b.p.kind == BaseKind::Unknown) {
        return b;
    }
    if (a.p.kind == BaseKind::Unknown || a.p.id != b.p.id) {
        out.p = UnknownPtr("base_join");
        return out;
    }
    out.p.off = WidenInt(a.p.off, b.p.off);
    return out;
}

// 循环头的入口：next = old ⊔ 新的（格与守卫已经是交集，只会变少）；局部逐个 widening
State WidenState(const State &old, const State &next)
{
    State out = next;
    for (auto &entry : out.locals) {
        auto it = old.locals.find(entry.first);
        if (it != old.locals.end()) {
            entry.second = WidenValue(it->second, entry.second);
        }
    }
    return out;
}

// l op x（l 的值是区间）推出的接口符号原子（SPEC-0017 §2.3）：l < x → lo < x；l > x → hi > x …
void AtomsOfRange(const IntVal &l, fir::CmpOp op, const LinExpr &x, std::vector<LinExpr> &atoms)
{
    llvm::Optional<LinExpr> lo = l.Lower();
    llvm::Optional<LinExpr> hi = l.Upper();
    LinExpr one = LinExpr::Const(1);
    switch (op) {
        case fir::CmpOp::Lt:
            if (lo) {
                atoms.push_back(*lo - x + one);
            }
            break;
        case fir::CmpOp::Le:
            if (lo) {
                atoms.push_back(*lo - x);
            }
            break;
        case fir::CmpOp::Gt:
            if (hi) {
                atoms.push_back(x - *hi + one);
            }
            break;
        case fir::CmpOp::Ge:
            if (hi) {
                atoms.push_back(x - *hi);
            }
            break;
        case fir::CmpOp::Eq:
            if (lo) {
                atoms.push_back(*lo - x);
            }
            if (hi) {
                atoms.push_back(x - *hi);
            }
            break;
        case fir::CmpOp::Ne:
            break;
    }
}

// 线性式里 div / mod 的内层都在 [tmin, tmax] 里（内层回绕时除出来的不是数学值，SPEC-0017 §2.4）
bool InnerFaithful(const LinExpr &e, int64_t tmin, int64_t tmax, const SymEnv &env)
{
    for (const auto &term : e.Terms()) {
        const Atom &atom = term.first;
        if ((atom.kind == Atom::Kind::Div || atom.kind == Atom::Kind::Mod) && atom.a) {
            Limit lo = env.RangeOf(*atom.a, false);
            Limit hi = env.RangeOf(*atom.a, true);
            if (!lo.known || !hi.known || lo.value < tmin || hi.value > tmax) {
                return false;
            }
        }
        if ((atom.a && !InnerFaithful(*atom.a, tmin, tmax, env)) ||
            (atom.b && !InnerFaithful(*atom.b, tmin, tmax, env))) {
            return false;
        }
    }
    return true;
}

bool FaithfulAs(const IntVal &v, const IntType &type, const SymEnv &env);

// 实参名 p<k> → k；不是返回 −1
int ParamIndexOf(const std::string &name)
{
    if (name.size() < 2 || name[0] != 'p') {
        return -1;
    }
    int k = 0;
    for (size_t i = 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        k = k * 10 + (name[i] - '0');
    }
    return k;
}

// 值就是一个回绕形 wrapW(E)（无符号减法可能为负时的写法，SPEC-0014 §2.2）
bool SingleWrap(const LinExpr &v, LinExpr &inner, unsigned &bits)
{
    if (v.IsUnknown() || v.Constant() != 0 || v.Terms().size() != 1) {
        return false;
    }
    const auto &term = *v.Terms().begin();
    if (term.second != 1 || term.first.kind != Atom::Kind::Wrap || !term.first.a) {
        return false;
    }
    inner = *term.first.a;
    bits = static_cast<unsigned>(term.first.n);
    return true;
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
    Analyzer(const fir::Module &m, const fir::Function &f, const models::ModelTable &models)
        : m(m), f(f), models(models)
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
    void Access(const char *kind, const PtrVal &addr, ExprId addrExpr, const IntVal &bytes, const fir::Loc &loc,
                const State &s, const std::vector<LinExpr> &forced = {});
    // ---- 模型（SPEC-0018）----
    void ApplyAccessModel(const fir::Inst &inst, const models::AccessModel &model, const State &s);
    Value AllocResult(const fir::Inst &inst, const models::AllocModel &model, const State &s);
    std::vector<State> FlowOut(size_t block, State s);

    // ---- 条件边与守卫（SPEC-0016）----
    SymEnv Env(const State &s) const;
    bool Faithful(ExprId e, const IntVal &v, const SymEnv &env) const;
    bool UpperSound(ExprId e, const State &s, const SymEnv &env);
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
    const models::ModelTable &models;
    std::vector<RawAccess> accesses;
    std::vector<RawAccess> safe;
    std::vector<summary::Open> opens;
    std::vector<summary::Call> calls;
    mutable std::map<std::string, summary::FieldInfo> fields;
    mutable std::map<std::string, std::string> globalTypes;  // 读到过的全局标量：g:名 → 类型（Env 用）
    std::vector<bool> heads;                                 // 循环头（回边的目标），按块下标
    bool recording = false;     // 只有最后一遍记访问、调用点、账本（SPEC-0017 §2.1）
    bool edgeFromHead = false;  // 当前 Assume 的边从循环头出发：收窄出来的上界标 loop
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
    // 无符号的加 / 乘 / 左移得到区间、上界不能证明 ≤ 类型最大值：可能回绕，真实值在 [0, 上界] 里（SPEC-0017 §2.4）。
    // 确切值仍按数学值（SPEC-0014 §2.2 的约定），由比较处的 Faithful 把关
    bool ring = op == fir::BinOp::Add || op == fir::BinOp::Mul || op == fir::BinOp::Shl;
    if (ring && !x.isSigned && x.width < 63 && r.kind == IntVal::Kind::Range) {
        int64_t tmax = (int64_t(1) << x.width) - 1;
        Limit hi = r.hasHi ? Env(s).RangeOf(r.hi, true) : Limit();
        if (!hi.known || hi.value > tmax) {
            r = IntVal::Range(LinExpr::Const(0), r.Upper(), r.tag);
        }
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
                v.p.hasParent = true;  // 外层对象也要查（SPEC-0022）
                v.p.parentId = p.id;
                v.p.parentObj = p.obj;
                v.p.parentSize = p.size;
                v.p.parentOff = x.value;
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
                globalTypes["g:" + g.name] = fir::TypeText(m, g.type);
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

// forced：模型给出的守卫（这次访问发生的条件），总挂在 Req 上，被其余守卫蕴含的不挂（SPEC-0020 §2.2）
void Analyzer::Access(const char *kind, const PtrVal &addr, ExprId addrExpr, const IntVal &bytes, const fir::Loc &loc,
                      const State &s, const std::vector<LinExpr> &forced)
{
    if (!recording) {
        return;
    }
    // 成员数组：外层对象的边界也要满足（off(f) + 下标 + 字节数 <= ext(pK)，F-02；SPEC-0022）——只查成员会把
    // "按大结构体访问小缓冲区"（LV7-03）证明成安全
    if (addr.hasParent) {
        PtrVal parent;
        parent.kind = BaseKind::Param;
        parent.id = addr.parentId;
        parent.obj = addr.parentObj;
        parent.hasSize = true;
        parent.size = addr.parentSize;
        parent.off = AddVals(IntVal::Exact(LinExpr::Const(addr.parentOff)), addr.off);
        Access(kind, parent, fir::kNone, bytes, loc, s, forced);
    }
    std::string at = SiteOf(m, loc);
    auto open = [&](const char *reason) {
        bool seen = llvm::any_of(opens, [&](const summary::Open &o) { return o.reason == reason && o.at == at; });
        if (!seen) {  // 按（原因, 位置）去重：同一行的读和写只记一条
            summary::Open o;
            o.reason = reason;
            o.at = at;
            opens.push_back(o);
        }
    };
    if (bytes.IsConst() && bytes.value.Constant() == 0) {
        return;  // 0 字节：不访问
    }
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
        // 偏移的上界被 widening 去掉、地址就是一个指针局部（*p++）→ 指针步进（A-45、SPEC-0017 §2.5）
        bool stepping = addr.off.kind == IntVal::Kind::Range && addr.off.tag == Tag::Widen &&
                        addrExpr < f.exprs.size() && f.exprs[addrExpr].op == ExprOp::Local;
        open(stepping ? "pointer_stepping" : "type_range_only");
        return;
    }
    // 字节数（模型给出的可以是区间或符号，SPEC-0018 §2.2）：没有上界 → 账本；没有下界按 0
    llvm::Optional<LinExpr> bytesHi = bytes.Upper();
    if (!bytesHi) {
        open("type_range_only");
        return;
    }
    llvm::Optional<LinExpr> bytesLo = bytes.Lower();
    llvm::Optional<LinExpr> lo = addr.off.Lower();
    RawAccess a;
    a.kind = kind;
    a.have = addr.size;
    a.obj = addr.obj;
    a.hi = *hi + *bytesHi;
    a.lo = (lo ? *lo : LinExpr::Const(0)) + (bytesLo ? *bytesLo : LinExpr::Const(0));
    a.start = lo;
    Tag offTag = addr.off.kind == IntVal::Kind::Exact ? Tag::Exact : addr.off.tag;
    Tag bytesTag = bytes.kind == IntVal::Kind::Exact ? Tag::Exact : bytes.tag;
    a.tag = WorseTag(offTag, bytesTag);
    a.at = at;
    if (a.hi.IsUnknown() || a.lo.IsUnknown()) {
        open("type_range_only");
        return;
    }
    std::set<std::string> syms;  // 与 have / end / start 有公共符号的守卫（SPEC-0016 §2.2、SPEC-0018 §2.2）
    CollectSyms(a.have, syms);
    CollectSyms(a.lo, syms);
    CollectSyms(a.hi, syms);
    if (a.start) {
        CollectSyms(*a.start, syms);
    }
    std::vector<LinExpr> chosen;
    for (const LinExpr &g : s.guards) {
        std::set<std::string> gs;
        CollectSyms(g, gs);
        if (llvm::any_of(gs, [&](const std::string &x) { return syms.count(x) != 0; })) {
            chosen.push_back(g);
        }
    }
    for (const LinExpr &g : forced) {
        // 环境只用类型范围与已挑中的守卫（Env(s) 里已经有 g 自己，会把它判成"已蕴含"）
        State bare;
        bare.reached = true;
        bare.guards = chosen;
        if (!llvm::is_contained(chosen, g) && !EntailsLe0(g, chosen, Env(bare))) {
            chosen.push_back(g);
        }
    }
    for (const LinExpr &g : chosen) {
        a.guard.push_back(ShowLe0(g));
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
            Access("R", addr, inst.addr, IntVal::Exact(LinExpr::Const(inst.size)), inst.loc, s);
            SetLocal(s, inst.dst, LoadValue(addr, inst.size, inst.dst, s));
            return;
        }
        case fir::InstOp::Store: {
            PtrVal addr = EvalPtr(inst.addr, s);
            Access("W", addr, inst.addr, IntVal::Exact(LinExpr::Const(inst.size)), inst.loc, s);
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
        case fir::InstOp::Call: {
            RecordCall(inst, s);
            llvm::StringRef name = KeyName(inst.callee);
            models::Kind kind = inst.calleeExpr == fir::kNone ? models.KindOf(name) : models::Kind::None;
            if (kind == models::Kind::Access) {
                ApplyAccessModel(inst, *models.Access(name), s);
            }
            Value result;
            result.isPtr = inst.dst != fir::kNone && IsPtrLocal(inst.dst);
            result.p = UnknownPtr("call");
            result.i = IntVal::Top("call");
            if (kind == models::Kind::Alloc) {
                result = AllocResult(inst, *models.Alloc(name), s);
            }
            if (kind != models::Kind::Pure && kind != models::Kind::Free) {
                s.cells.clear();  // 被调者可能经指针 / 全局写内存（效果在 S7d-3）；纯函数、释放函数不改被跟踪的内存
            }
            if (inst.dst != fir::kNone) {
                SetLocal(s, inst.dst, result);
            }
            return;
        }
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

// 一个块：执行指令，再沿每条出边 Assume（不可达的边给出未到达的状态）
std::vector<State> Analyzer::FlowOut(size_t block, State s)
{
    for (const fir::Inst &inst : f.blocks[block].insts) {
        Step(inst, s);
    }
    const std::vector<fir::Edge> &succ = f.blocks[block].succ;
    std::vector<State> out(succ.size());
    edgeFromHead = heads[block];
    for (size_t i = 0; i < succ.size(); ++i) {
        if (succ[i].assume == fir::kNone) {
            out[i] = s;
            continue;
        }
        State taken = s;
        if (Assume(succ[i].assume, false, taken)) {
            out[i] = std::move(taken);
        }
    }
    edgeFromHead = false;
    return out;
}

// SPEC-0017 §2.1：上升（循环头 widening）→ 下降 2 遍 → 用最终的入口状态走一遍、记录
FunctionFacts Analyzer::Run()
{
    FunctionFacts facts;
    size_t n = f.blocks.size();
    llvm::DenseMap<uint32_t, size_t> index;
    for (size_t i = 0; i < n; ++i) {
        index[f.blocks[i].id] = i;
    }
    auto entry = index.find(f.entry);
    if (entry == index.end()) {
        facts.status = "skipped:no_body";
        return facts;
    }
    // DFS：后序；指向栈上块的边是回边，它的目标是循环头。后继倒着走，逆后序里后继 0（条件为真 / 循环体）排在前面：
    // 先把循环算稳再传给出口，Dump=state 也按源码顺序
    std::vector<int> mark(n, 0);  // 0 未访问、1 在栈上、2 完成
    std::vector<size_t> post;
    heads.assign(n, false);
    std::function<void(size_t)> dfs = [&](size_t b) {
        mark[b] = 1;
        const std::vector<fir::Edge> &succ = f.blocks[b].succ;
        for (auto e = succ.rbegin(); e != succ.rend(); ++e) {
            auto it = index.find(e->to);
            if (it == index.end()) {
                continue;
            }
            if (mark[it->second] == 1) {
                heads[it->second] = true;
            } else if (mark[it->second] == 0) {
                dfs(it->second);
            }
        }
        mark[b] = 2;
        post.push_back(b);
    };
    dfs(entry->second);
    std::vector<size_t> rpo(post.rbegin(), post.rend());
    std::vector<size_t> order(n, n);
    for (size_t k = 0; k < rpo.size(); ++k) {
        order[rpo[k]] = k;
    }
    // 边：(起点, 第几条出边, 终点)，只留两端都可达的
    struct EdgeRef {
        size_t from;
        size_t succ;
        size_t to;
    };
    std::vector<EdgeRef> edges;
    std::vector<std::vector<size_t>> incoming(n);
    std::vector<std::vector<size_t>> outgoing(n);
    for (size_t b : rpo) {
        for (size_t i = 0; i < f.blocks[b].succ.size(); ++i) {
            auto it = index.find(f.blocks[b].succ[i].to);
            if (it != index.end() && order[it->second] < n) {
                incoming[it->second].push_back(edges.size());
                outgoing[b].push_back(edges.size());
                edges.push_back({b, i, it->second});
            }
        }
    }

    // 汇合时比较符号界的环境：类型范围 + 两边都成立的守卫（SPEC-0023）
    auto envOf = [&](const std::vector<LinExpr> &guards) {
        State t;
        t.reached = true;
        t.guards = guards;
        return Env(t);
    };
    // 上升：工作表按逆后序编号
    std::vector<State> in(n);
    in[entry->second] = Entry();
    std::vector<int> updates(n, 0);
    std::set<size_t> work = {order[entry->second]};
    while (!work.empty()) {
        size_t b = rpo[*work.begin()];
        work.erase(work.begin());
        if (!in[b].reached) {
            continue;
        }
        std::vector<State> out = FlowOut(b, in[b]);
        for (size_t e : outgoing[b]) {
            const State &o = out[edges[e].succ];
            size_t t = edges[e].to;
            if (!o.reached) {
                continue;
            }
            State next = Join(in[t], o, envOf);
            if (heads[t] && in[t].reached) {
                next = WidenState(in[t], next);
            }
            if (next == in[t]) {
                continue;
            }
            if (heads[t] && ++updates[t] > kMaxHeadUpdates) {
                facts.status = "partial:iter_limit";  // A-33；S8c 按"函数没分析完"报 SUSPECT
                return facts;
            }
            in[t] = std::move(next);
            work.insert(order[t]);
        }
    }

    // 下降：每块的入口 = 各入边上一次的贡献之并，不再 widening
    std::vector<State> contrib(edges.size());
    for (size_t b : rpo) {
        if (in[b].reached) {
            std::vector<State> out = FlowOut(b, in[b]);
            for (size_t e : outgoing[b]) {
                contrib[e] = out[edges[e].succ];
            }
        }
    }
    for (int pass = 0; pass < kNarrowPasses; ++pass) {
        for (size_t b : rpo) {
            State s = b == entry->second ? Entry() : State();
            for (size_t e : incoming[b]) {
                s = Join(s, contrib[e], envOf);
            }
            in[b] = s;
            std::vector<State> out = s.reached ? FlowOut(b, s) : std::vector<State>(f.blocks[b].succ.size());
            for (size_t e : outgoing[b]) {
                contrib[e] = out[edges[e].succ];
            }
        }
    }

    // 记录：最终的入口状态上走一遍
    recording = true;
    for (size_t b : rpo) {
        if (!in[b].reached) {
            continue;
        }
        if (trace != nullptr) {
            trace->push_back(ShowState(f.blocks[b], in[b]) + (heads[b] ? " (head)" : ""));
        }
        State s = in[b];
        for (const fir::Inst &inst : f.blocks[b].insts) {
            Step(inst, s);
        }
    }
    recording = false;
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
    if (!recording || inst.calleeExpr != fir::kNone) {
        return;  // 间接调用（fptr）留到 S9
    }
    summary::Call call;
    call.site = SiteOf(m, inst.loc);
    call.callee = KeyName(inst.callee).str();
    // 模型表优先（I-44，SPEC-0018 §2.1）
    models::Kind kind = models.KindOf(call.callee);
    bool pure = kind == models::Kind::Pure;
    if (kind != models::Kind::None) {
        call.model = models::KindName(kind).str();
    }
    for (size_t k = 0; k < inst.args.size(); ++k) {
        Value v = Eval(inst.args[k], s);
        summary::ArgValue arg;
        std::string unknown = "_unknown" + std::to_string(k);
        if (v.isPtr) {
            if (pure || v.p.kind == BaseKind::Func) {
                arg.kind = summary::ArgValue::Kind::Null;  // 纯函数模型的指针、函数指针（SPEC-0018 §2.4）
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

// 接口符号的取值范围：形参、用到的字段、读到过的全局的类型范围，加上当前的守卫
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
    for (const auto &g : globalTypes) {
        if (TypeRangeOf(g.second, lo, hi)) {
            env.SetTypeRange(g.first, lo, hi);
        }
    }
    for (const LinExpr &g : s.guards) {
        env.AddGuard(g, Tag::Check);
    }
    return env;
}

// 比较的一边算出来的值就是 C 的值（SPEC-0016 §2.1、SPEC-0017 §2.4）：常数；确切值落在 C 类型范围内、div / mod 的内层也在；
// 区间的上下界都在类型范围内，或整个类型范围都在区间里；有符号的区间缺的界视为在类型范围内（有符号溢出是未定义行为）。
// if (off + len > max) 里的 off + len 可能回绕，不忠实
bool Analyzer::Faithful(ExprId e, const IntVal &v, const SymEnv &env) const
{
    if (v.IsConst()) {
        return true;
    }
    llvm::Optional<IntType> t = IntTypeOf(e);
    return t && FaithfulAs(v, *t, env);
}

bool FaithfulAs(const IntVal &v, const IntType &type, const SymEnv &env)
{
    const IntType *t = &type;
    if (v.IsConst()) {
        return true;
    }
    if (t->width >= 63) {
        return true;  // 64 位：线性式在 int64 里溢出时已成为未知
    }
    IntVal range = TypeRange(t->width, t->isSigned, Tag::Type);
    int64_t tmin = range.lo.Constant();
    int64_t tmax = range.hi.Constant();
    // inner：确切值要求 div / mod 的内层也不回绕；区间的界是收窄时按忠实 / 可当上界的规则造出来的可靠界，只查落在类型范围内（SPEC-0023 §2）
    auto within = [&](const LinExpr &x, bool inner) {
        Limit lo = env.RangeOf(x, false);
        Limit hi = env.RangeOf(x, true);
        return lo.known && hi.known && lo.value >= tmin && hi.value <= tmax &&
               (!inner || InnerFaithful(x, tmin, tmax, env));
    };
    if (v.kind == IntVal::Kind::Exact) {
        return within(v.value, true);
    }
    if (v.kind != IntVal::Kind::Range) {
        return false;
    }
    Limit loTop = v.hasLo ? env.RangeOf(v.lo, true) : Limit();
    Limit hiBottom = v.hasHi ? env.RangeOf(v.hi, false) : Limit();
    bool coversLo = !v.hasLo || (loTop.known && loTop.value <= tmin);
    bool coversHi = !v.hasHi || (hiBottom.known && hiBottom.value >= tmax);
    if (coversLo && coversHi) {
        return true;
    }
    bool loOk = v.hasLo ? within(v.lo, false) : t->isSigned;
    bool hiOk = v.hasHi ? within(v.hi, false) : t->isSigned;
    return loOk && hiOk;
}

// 只当"大的一边"（上界）用时可用（SPEC-0017 §2.4）：无符号的加、乘常数、左移常数、除常数、右移常数、保值的无符号扩展，
// 叶子是忠实的无符号局部或非负常数——回绕只会让真实值变小（真实值 = 数学值 mod 2^W ≤ 数学值）
bool Analyzer::UpperSound(ExprId e, const State &s, const SymEnv &env)
{
    if (e >= f.exprs.size()) {
        return false;
    }
    const fir::Expr &x = f.exprs[e];
    auto constant = [&](ExprId k, int64_t &out) {
        IntVal c = EvalInt(k, s);
        if (!c.IsConst()) {
            return false;
        }
        out = c.value.Constant();
        return true;
    };
    int64_t c = 0;
    switch (x.op) {
        case ExprOp::Const:
            return x.value >= 0;
        case ExprOp::Local: {
            llvm::Optional<IntType> t = IntTypeOf(e);
            return t && !t->isSigned && Faithful(e, EvalInt(e, s), env);
        }
        case ExprOp::Bin:
            if (x.width == 0 || x.isSigned) {
                return false;
            }
            switch (static_cast<fir::BinOp>(x.sub)) {
                case fir::BinOp::Add:
                    return UpperSound(x.a, s, env) && UpperSound(x.b, s, env);
                case fir::BinOp::Mul:
                    return (constant(x.b, c) && c >= 0 && UpperSound(x.a, s, env)) ||
                           (constant(x.a, c) && c >= 0 && UpperSound(x.b, s, env));
                case fir::BinOp::Div:
                    return constant(x.b, c) && c > 0 && UpperSound(x.a, s, env);
                case fir::BinOp::Shl:
                case fir::BinOp::Shr:
                    return constant(x.b, c) && c >= 0 && c < 62 && UpperSound(x.a, s, env);
                default:
                    return false;
            }
        case ExprOp::Cast: {
            llvm::Optional<IntType> src = IntTypeOf(x.a);
            return !x.isSigned && src && !src->isSigned && x.width >= src->width && UpperSound(x.a, s, env);
        }
        default:
            return false;
    }
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

// a op b（b 为 kNone 时是常数 0）。两边都是确切值 → 原子（常数为假 → 不可达）；一边是区间、一边是确切值 → 收窄区间并推出原子
// （SPEC-0017 §2.3）；在类型范围与已有守卫下已经成立的原子不加
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
    // "大的一边"：a < b、a <= b 的 b；a > b、a >= b 的 a（SPEC-0017 §2.4）
    bool aLarge = op == fir::CmpOp::Gt || op == fir::CmpOp::Ge;
    bool bLarge = op == fir::CmpOp::Lt || op == fir::CmpOp::Le;
    auto usable = [&](ExprId e, const IntVal &v, bool large) {
        if (e == fir::kNone) {
            return true;
        }
        return Faithful(e, v, env) || (large && v.kind == IntVal::Kind::Exact && UpperSound(e, s, env));
    };
    if (!usable(a, va.i, aLarge) || !usable(b, vb.i, bLarge)) {
        return true;
    }
    static const fir::CmpOp mirrored[] = {fir::CmpOp::Eq, fir::CmpOp::Ne, fir::CmpOp::Gt,
                                          fir::CmpOp::Ge, fir::CmpOp::Lt, fir::CmpOp::Le};
    const IntVal &ia = va.i;
    const IntVal &ib = vb.i;
    LinExpr one = LinExpr::Const(1);
    std::vector<LinExpr> atoms;
    if (ia.kind == IntVal::Kind::Exact && ib.kind == IntVal::Kind::Exact) {
        LinExpr d = ia.value - ib.value;  // a − b
        if (d.IsUnknown()) {
            return true;
        }
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
    } else if (ia.kind == IntVal::Kind::Range && ib.kind == IntVal::Kind::Exact) {
        if (!Narrow(a, op, ib.value, s)) {
            return false;
        }
        AtomsOfRange(ia, op, ib.value, atoms);
    } else if (ib.kind == IntVal::Kind::Range && ia.kind == IntVal::Kind::Exact && b != fir::kNone) {
        fir::CmpOp m = mirrored[static_cast<int>(op)];
        if (!Narrow(b, m, ia.value, s)) {
            return false;
        }
        AtomsOfRange(ib, m, ia.value, atoms);
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
        if (EntailsLe0(atom, s.guards, env)) {
            continue;  // 已经成立，没有信息
        }
        AddGuard(s, atom);
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
    // 与原来的界取更紧的：差不是常数时用这条边上的状态（类型范围与守卫）比（SPEC-0023）；比不出留原来的（都是对的界）
    SymEnv env = Env(s);
    Comparer cmp{&env, &s.guards};
    auto tighter = [&](const llvm::Optional<LinExpr> &old, const llvm::Optional<LinExpr> &cand, bool upper) {
        if (!cand || cand->IsUnknown()) {
            return old;
        }
        if (!old) {
            return cand;
        }
        if (upper ? ProvablyLe(*cand, *old, &cmp) : ProvablyLe(*old, *cand, &cmp)) {
            return cand;
        }
        return old;
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
        // 标签说的是上界的来源（F-12 只看上界，同 replay.py）：上界被代码里的检查收窄 → check，循环头上的条件 → loop
        // （SPEC-0017 §2.2）；只收窄了下界 → 原标签不变
        bool hiNarrowed = h && (!hi || !(*h == *hi));
        Tag by = edgeFromHead ? Tag::Loop : Tag::Check;
        Tag tag = !hiNarrowed ? v.tag : (IsCodeTag(v.tag) ? WorseTag(v.tag, by) : by);
        out.i = IntVal::Range(l, h, tag);
    }
    s.locals[static_cast<uint32_t>(f.exprs[cur].value)] = out;
    return true;
}

// ---- 模型（SPEC-0018）----

const int64_t kSecurecMemMax = 0x7fffffff;  // SECUREC_MEM_MAX_LEN 的默认值（F-03）

// 模型的式子（实参 p0、p1… 的线性式或乘积，I-41）代入实参的值
IntVal EvalModelExpr(const LinExpr &e, const std::vector<IntVal> &args)
{
    if (e.IsUnknown()) {
        return IntVal::Top("model_expr");
    }
    auto arg = [&](const Atom &atom, IntVal &out) {
        int k = atom.kind == Atom::Kind::Sym ? ParamIndexOf(atom.name) : -1;
        if (k < 0 || static_cast<size_t>(k) >= args.size()) {
            return false;
        }
        out = args[k];
        return true;
    };
    auto single = [&](const std::shared_ptr<const LinExpr> &x, IntVal &out) {
        return x && x->Constant() == 0 && x->Terms().size() == 1 && x->Terms().begin()->second == 1 &&
               arg(x->Terms().begin()->first, out);
    };
    IntVal acc = IntVal::Exact(LinExpr::Const(e.Constant()));
    for (const auto &term : e.Terms()) {
        IntVal v;
        IntVal a;
        IntVal b;
        if (arg(term.first, v)) {
        } else if (term.first.kind == Atom::Kind::Mul && single(term.first.a, a) && single(term.first.b, b)) {
            v = MulVals(a, b);
        } else {
            return IntVal::Top("model_expr");
        }
        acc = AddVals(acc, ScaleVal(v, term.second));
    }
    return acc;
}

// 访问类函数在调用点（SPEC-0018 §2.2）：securec 的 destMax 规则 → 各条件照常做访问检查（when 变成这条访问的守卫）
void Analyzer::ApplyAccessModel(const fir::Inst &inst, const models::AccessModel &model, const State &s)
{
    if (!recording) {
        return;
    }
    SymEnv env = Env(s);
    // 一段：这段的状态（带模型给出的守卫）、实参的值、实参是否忠实、模型给出的守卫
    struct Segment {
        State t;
        std::vector<IntVal> ints;
        std::vector<bool> faithful;
        std::vector<LinExpr> forced;
    };
    std::vector<Value> values;
    Segment base{s, {}, {}, {}};
    for (ExprId arg : inst.args) {
        Value v = Eval(arg, s);
        values.push_back(v);
        base.ints.push_back(v.isPtr ? IntVal::Top("ptr_as_int") : v.i);
        base.faithful.push_back(!v.isPtr && Faithful(arg, v.i, env));
    }
    // 给一段加模型守卫 atom（L ≤ 0）：不可能成立 → false；已经成立 → 不加
    auto guard = [&](Segment &seg, const LinExpr &atom) {
        if (atom.IsUnknown()) {
            return true;
        }
        if (atom.IsConst()) {
            return atom.Constant() <= 0;
        }
        SymEnv e = Env(seg.t);
        if (EntailsLe0(LinExpr::Const(1) - atom, seg.t.guards, e)) {
            return false;  // 能证明 atom >= 1
        }
        if (!EntailsLe0(atom, seg.t.guards, e)) {
            AddGuard(seg.t, atom);
            seg.forced.push_back(atom);
        }
        return true;
    };
    std::vector<Segment> segments;
    int dmIndex = model.destMax;
    if (dmIndex >= 0 && static_cast<size_t>(dmIndex) < base.ints.size()) {
        const IntVal dm = base.ints[dmIndex];
        if (dm.IsConst() && (dm.value.Constant() == 0 || dm.value.Constant() > kSecurecMemMax)) {
            return;  // securec 直接返回：不写、不读
        }
        LinExpr inner;
        unsigned bits = 0;
        if (dm.kind == IntVal::Kind::Exact && SingleWrap(dm.value, inner, bits) && bits < 63) {
            // destMax = wrapW(E)：0 < 值 <= 0x7fffffff 才写（F-03，SPEC-0020 §2.1）
            // 段 A：E >= 0，destMax = E（LV2-03 TN：128 − ulOff → 守卫 p1 <= 128）
            Segment segA = base;
            if (guard(segA, inner.Scale(-1))) {
                segA.ints[dmIndex] = IntVal::Exact(inner);
                segA.faithful[dmIndex] = FaithfulAs(segA.ints[dmIndex], IntType{bits, false}, Env(segA.t));
                segments.push_back(segA);
            }
            // 段 B：E <= 0x7fffffff − 2^W（且 E <= −1），回绕后的值仍不超过上限，照样写，destMax = E + 2^W
            int64_t modulus = int64_t(1) << bits;
            Segment segB = base;
            if (guard(segB, inner - LinExpr::Const(kSecurecMemMax - modulus)) &&
                guard(segB, inner + LinExpr::Const(1))) {
                segB.ints[dmIndex] = IntVal::Exact(inner + LinExpr::Const(modulus));
                segB.faithful[dmIndex] = FaithfulAs(segB.ints[dmIndex], IntType{bits, false}, Env(segB.t));
                segments.push_back(segB);
            }
        } else {
            segments.push_back(base);
        }
    } else {
        segments.push_back(base);
    }
    for (const Segment &seg : segments) {
        SymEnv envT = Env(seg.t);
        for (const models::AccessCond &cond : model.conds) {
            if (cond.base < 0 || static_cast<size_t>(cond.base) >= values.size()) {
                continue;
            }
            LinExpr bytesExpr;
            std::string error;
            if (!ParseLin(cond.bytes, bytesExpr, error)) {
                continue;
            }
            const Value &baseValue = values[cond.base];
            PtrVal addr = baseValue.isPtr ? baseValue.p : UnknownPtr("int_as_ptr");
            IntVal bytes = EvalModelExpr(bytesExpr, seg.ints);
            State u = seg.t;
            std::vector<LinExpr> forced = seg.forced;
            std::vector<LinExpr> when;
            if (!cond.when.empty() && ParseCond(cond.when, when, error)) {
                bool never = false;
                for (const LinExpr &g : when) {
                    // 用到的实参都忠实时才变成守卫；否则照常访问（多报不少报）
                    std::set<std::string> syms;
                    CollectSyms(g, syms);
                    bool ok = llvm::all_of(syms, [&](const std::string &x) {
                        int k = ParamIndexOf(x);
                        return k >= 0 && static_cast<size_t>(k) < seg.faithful.size() && seg.faithful[k];
                    });
                    IntVal v = EvalModelExpr(g, seg.ints);
                    if (!ok || v.kind != IntVal::Kind::Exact) {
                        continue;
                    }
                    if (v.value.IsConst()) {
                        never = never || v.value.Constant() > 0;
                        continue;
                    }
                    if (!EntailsLe0(v.value, u.guards, envT)) {
                        AddGuard(u, v.value);
                        forced.push_back(v.value);
                    }
                }
                if (never) {
                    continue;  // count > destMax：securec 不读源
                }
            }
            Access(cond.kind == "W" ? "W" : "R", addr, fir::kNone, bytes, inst.loc, u, forced);
        }
    }
}

// 分配器的返回值（SPEC-0018 §2.3）：新的堆对象 heap@<调用点>，大小取 size 代入实参的下界
Value Analyzer::AllocResult(const fir::Inst &inst, const models::AllocModel &model, const State &s)
{
    std::vector<IntVal> ints;
    for (ExprId arg : inst.args) {
        ints.push_back(EvalInt(arg, s));
    }
    LinExpr sizeExpr;
    std::string error;
    IntVal size = ParseLin(model.size, sizeExpr, error) ? EvalModelExpr(sizeExpr, ints) : IntVal::Top("model_expr");
    llvm::Optional<LinExpr> lo = size.Lower();
    std::string site = SiteOf(m, inst.loc);
    Value v;
    v.isPtr = true;
    v.p = Base(BaseKind::Heap, "H" + site, "heap@" + site, lo && !lo->IsUnknown() ? lo : llvm::Optional<LinExpr>());
    return v;
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

FunctionFacts AnalyzeFunction(const fir::Module &m, const fir::Function &f, std::vector<std::string> *stateTrace,
                              const models::ModelTable &models)
{
    Analyzer a(m, f, models);
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
        summary::Param p{l->name, fir::TypeText(m, l->type), llvm::None};
        // 指向完整类型的指针：所指类型的字节数（SPEC-0019 §2.2，入口记账 typed_field 用）
        int64_t size = 0;
        if (l->type < m.types.size() && m.types[l->type].kind == fir::TypeKind::Ptr &&
            TypeSize(m, m.types[l->type].to, size)) {
            p.size = size;
        }
        s.params.push_back(p);
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
