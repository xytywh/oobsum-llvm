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
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
                   tag == o.tag && cases == o.cases && stride == o.stride && (stride < 2 || anchor == o.anchor);
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
        case Kind::Range: {
            std::string out = "[" + (hasLo ? lo.Show() : std::string("-inf")) + ", " +
                              (hasHi ? hi.Show() : std::string("+inf")) + "]" +
                              (tag == Tag::None ? "" : ":" + TagName(tag).str());
            if (stride >= 2) {  // SPEC-0084
                out += "%" + std::to_string(stride) +
                       (anchor.IsConst() && anchor.Constant() == 0 ? std::string() : "~" + anchor.Show());
            }
            for (size_t i = 0; i < cases.size(); ++i) {  // {0: p0 >= 17; 1: p0 <= 16}
                out += (i == 0 ? "{" : "; ") + std::to_string(cases[i].value) + ":";
                for (size_t k = 0; k < cases[i].guards.size(); ++k) {
                    out += (k == 0 ? " " : ", ") + ShowLe0(cases[i].guards[k]);
                }
                out += i + 1 == cases.size() ? "}" : "";
            }
            return out;
        }
        case Kind::Top:
            return "?(" + why + ")";
    }
    return "?";
}

bool PtrVal::operator==(const PtrVal &o) const
{
    return kind == o.kind && id == o.id && off == o.off && why == o.why && subs == o.subs;
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

// ---- 同余（SPEC-0084）----

const int64_t kMaxStride = int64_t(1) << 30;

int64_t Gcd(int64_t a, int64_t b)
{
    if (a == INT64_MIN || b == INT64_MIN) {
        return 1;
    }
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int64_t FloorDivInt(int64_t a, int64_t s)
{
    int64_t q = a / s;
    return (a % s != 0 && ((a < 0) != (s < 0))) ? q - 1 : q;
}

int64_t PosMod(int64_t a, int64_t s)
{
    int64_t r = a % s;
    return r < 0 ? r + s : r;
}

// 锚点规范化：系数与常数都按步长取正余数（x 是整数，c·x ≡ (c mod s)·x），同余类相同的写法相同
LinExpr NormAnchor(const LinExpr &a, int64_t s)
{
    LinExpr out = LinExpr::Const(PosMod(a.Constant(), s));
    for (const auto &term : a.Terms()) {
        int64_t c = PosMod(term.second, s);
        if (c != 0) {
            out = out + LinExpr::OfAtom(term.first).Scale(c);
        }
    }
    return out;
}

// v 的同余：确切值 → 锚点 = 值、步长 0（与任何步长相容）；区间 → 记着的；其余没有
bool CongOf(const IntVal &v, LinExpr &anchor, int64_t &stride)
{
    if (v.kind == IntVal::Kind::Exact && !v.value.IsUnknown()) {
        anchor = v.value;
        stride = 0;
        return true;
    }
    if (v.kind == IntVal::Kind::Range && v.stride >= 2) {
        anchor = v.anchor;
        stride = v.stride;
        return true;
    }
    return false;
}

// 只给区间记；步长小于 2、太大或锚点不知道 → 清掉
void SetCong(IntVal &v, const LinExpr &anchor, int64_t stride)
{
    if (v.kind != IntVal::Kind::Range || stride < 2 || stride > kMaxStride || anchor.IsUnknown()) {
        v.stride = 0;
        v.anchor = LinExpr();
        return;
    }
    v.stride = stride;
    v.anchor = NormAnchor(anchor, stride);
}

// 差 d 的常数与各系数的 gcd 并进 g
int64_t GcdWith(int64_t g, const LinExpr &d)
{
    g = Gcd(g, d.Constant());
    for (const auto &term : d.Terms()) {
        g = Gcd(g, term.second);
    }
    return g;
}

// a ⊔ b 的同余写进 out：步长 = gcd(sa, sb, 锚点之差的常数与各系数)
void JoinCong(const IntVal &a, const IntVal &b, IntVal &out)
{
    LinExpr ra;
    LinExpr rb;
    int64_t sa = 0;
    int64_t sb = 0;
    if (!CongOf(a, ra, sa) || !CongOf(b, rb, sb)) {
        SetCong(out, LinExpr(), 0);
        return;
    }
    LinExpr d = ra - rb;
    SetCong(out, ra, d.IsUnknown() ? 0 : GcdWith(Gcd(sa, sb), d));
}

// 上界取到同余类里（向下）：anchor + s·div(h − anchor, s)；差的各系数都能被 s 整除时直接减常数余数
llvm::Optional<LinExpr> RoundDown(const LinExpr &h, const LinExpr &anchor, int64_t s)
{
    LinExpr d = h - anchor;
    if (d.IsUnknown()) {
        return llvm::None;
    }
    if (llvm::all_of(d.Terms(), [&](const std::pair<const Atom, int64_t> &t) { return t.second % s == 0; })) {
        return h - LinExpr::Const(PosMod(d.Constant(), s));
    }
    LinExpr q = LinExpr::Div(d, s);
    if (q.IsUnknown()) {
        return llvm::None;
    }
    LinExpr out = anchor + q.Scale(s);
    return out.IsUnknown() ? llvm::Optional<LinExpr>() : out;
}

// 下界取到同余类里（向上）：只做差的各系数都能被 s 整除的情形
llvm::Optional<LinExpr> RoundUp(const LinExpr &l, const LinExpr &anchor, int64_t s)
{
    LinExpr d = l - anchor;
    if (d.IsUnknown() ||
        !llvm::all_of(d.Terms(), [&](const std::pair<const Atom, int64_t> &t) { return t.second % s == 0; })) {
        return llvm::None;
    }
    int64_t r = PosMod(d.Constant(), s);
    return r == 0 ? l : l + LinExpr::Const(s - r);
}

IntVal AddVals(const IntVal &a, const IntVal &b)
{
    if (a.kind == IntVal::Kind::Exact && b.kind == IntVal::Kind::Exact) {
        return IntVal::Exact(a.value + b.value);
    }
    if (a.kind == IntVal::Kind::Top || b.kind == IntVal::Kind::Top) {
        return IntVal::Top(a.kind == IntVal::Kind::Top ? a.why : b.why);
    }
    IntVal r = IntVal::Range(SumOpt(a.Lower(), b.Lower()), SumOpt(a.Upper(), b.Upper()), WorseTag(TagOf(a), TagOf(b)));
    // 同余：步长 gcd(sa, sb)，锚点相加（SPEC-0084）
    LinExpr ra;
    LinExpr rb;
    int64_t sa = 0;
    int64_t sb = 0;
    if (CongOf(a, ra, sa) && CongOf(b, rb, sb)) {
        SetCong(r, ra + rb, Gcd(sa, sb));
    }
    return r;
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
    IntVal r = c >= 0 ? IntVal::Range(scale(lo), scale(hi), a.tag) : IntVal::Range(scale(hi), scale(lo), a.tag);
    // 同余：有 → 步长 × |c|、锚点 × c；没有 → 步长 |c|、锚点 0（SPEC-0084）
    int64_t mag = c < 0 ? -c : c;
    LinExpr ra;
    int64_t sa = 0;
    if (c != INT64_MIN && mag <= kMaxStride) {
        if (CongOf(a, ra, sa) && sa >= 2) {
            SetCong(r, ra.Scale(c), sa <= kMaxStride / (mag == 0 ? 1 : mag) ? sa * mag : 0);
        } else {
            SetCong(r, LinExpr::Const(0), mag);
        }
    }
    return r;
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
    // 单个位 m = 2^k、x 确切：x & m = m·mod(div(x, m), 2)（向下取整、非负余数，补码的负数也成立；标志位判断才推得出守卫，SPEC-0097）
    bool singleBit = m > 0 && (m & (m - 1)) == 0;
    if (singleBit && a.kind == IntVal::Kind::Exact && !a.value.IsUnknown() && m < (int64_t(1) << 62)) {
        LinExpr bit = LinExpr::Mod(LinExpr::Div(a.value, m), 2);
        if (!bit.IsUnknown()) {
            return IntVal::Exact(bit.Scale(m));
        }
    }
    return IntVal::Range(LinExpr::Const(0), LinExpr::Const(m), Tag::Mask);
}

// 比较符号界的环境（SPEC-0023）：类型范围与守卫；没有就只认常数差
struct Comparer {
    const SymEnv *env = nullptr;
    const std::vector<LinExpr> *guards = nullptr;
    // 两边各自路上的守卫与类型范围（SPEC-0042）：共同环境下比不出来时，各自求常数界
    const SymEnv *sideA = nullptr;
    const SymEnv *sideB = nullptr;
    const std::vector<LinExpr> *guardsA = nullptr;  // 两边各自路上的守卫（SPEC-0043）
    const std::vector<LinExpr> *guardsB = nullptr;
    std::string self = std::string();  // 正在合并的局部若是形参：它的入口符号 pK（SPEC-0043）
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

// v 在它那条路上的常数界（SPEC-0042）：端点在那条路的守卫与类型范围下求；标签取端点的来源（常数 → branch）
llvm::Optional<Limit> SideLimit(const IntVal &v, bool upper, const SymEnv *env)
{
    llvm::Optional<LinExpr> e = upper ? v.Upper() : v.Lower();
    if (!e || e->IsUnknown() || env == nullptr) {
        return llvm::None;
    }
    Limit l = env->RangeOf(*e, upper);
    if (!l.known) {
        return llvm::None;
    }
    Tag own = v.kind == IntVal::Kind::Exact ? Tag::Branch : v.tag;
    l.tag = l.tag == Tag::None ? own : WorseTag(own, l.tag);
    return l;
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
    // 共同环境下比不出来的一端（钳位写法）：① 各自在自己路上的守卫下求常数界、取 min / max（SPEC-0042）；
    // ② 符号候选：一边的界在另一边自己路上的守卫下也成立就取它（if (ulLen > ulDstLen) ulLen = ulDstLen;，SPEC-0043）。
    // 上界只在来源是代码给出的界时才用（类型范围 / widen / data 照旧丢掉，记账方式不变）
    bool sides = cmp != nullptr && cmp->sideA != nullptr && cmp->sideB != nullptr;
    llvm::Optional<Tag> sideTag;
    if (!lo && sides) {
        llvm::Optional<Limit> la = SideLimit(a, false, cmp->sideA);
        llvm::Optional<Limit> lb = SideLimit(b, false, cmp->sideB);
        if (la && lb) {
            lo = LinExpr::Const(std::min(la->value, lb->value));
        }
    }
    if (!hi && sides) {
        llvm::Optional<Limit> ha = SideLimit(a, true, cmp->sideA);
        llvm::Optional<Limit> hb = SideLimit(b, true, cmp->sideB);
        if (ha && hb && IsCodeTag(WorseTag(ha->tag, hb->tag))) {
            hi = LinExpr::Const(std::max(ha->value, hb->value));
            sideTag = WorseTag(ha->tag, hb->tag);
        }
    }
    if (sides && (!lo || !hi)) {
        Comparer onA;
        Comparer onB;
        onA.env = cmp->sideA;
        onA.guards = cmp->guardsA;
        onB.env = cmp->sideB;
        onB.guards = cmp->guardsB;
        // 两个候选都成立（合并后其实是 min / max）：不取变量自己的原值（形参的入口符号），取钳位的目标
        auto pick = [&](const llvm::Optional<LinExpr> &x, const llvm::Optional<LinExpr> &y, bool upper) {
            llvm::Optional<LinExpr> out;
            if (!x || !y) {
                return out;
            }
            // upper：y 在 a 那条路上不小于 x → 取 y；x 在 b 那条路上不小于 y → 取 x（下界对称）
            bool takeY = upper ? ProvablyLe(*x, *y, &onA) : ProvablyLe(*y, *x, &onA);
            bool takeX = upper ? ProvablyLe(*y, *x, &onB) : ProvablyLe(*x, *y, &onB);
            bool selfY = !cmp->self.empty() && *y == LinExpr::Sym(cmp->self);
            if (takeY && takeX) {
                out = selfY ? x : y;
            } else if (takeY) {
                out = y;
            } else if (takeX) {
                out = x;
            }
            return out;
        };
        Tag both = WorseTag(a.kind == IntVal::Kind::Exact ? Tag::Branch : a.tag,
                            b.kind == IntVal::Kind::Exact ? Tag::Branch : b.tag);
        if (!hi && IsCodeTag(both)) {
            hi = pick(a.Upper(), b.Upper(), true);
            if (hi) {
                sideTag = both;
            }
        }
        if (!lo) {
            lo = pick(a.Lower(), b.Lower(), false);
        }
    }
    if (!lo && !hi) {
        return IntVal::Top("imprecise_join");
    }
    // 两条路上的确切值合成区间，来源是分支（D-22 的 branch）
    Tag tag = WorseTag(a.kind == IntVal::Kind::Exact ? Tag::Branch : a.tag,
                       b.kind == IntVal::Kind::Exact ? Tag::Branch : b.tag);
    IntVal out = IntVal::Range(lo, hi, sideTag ? *sideTag : tag);
    JoinCong(a, b, out);  // SPEC-0084
    return out;
}

// 未知值的原因 → 账本原因（A-08、D-27，SPEC-0060）：引擎不认识的构造；认识、只是值不知道的返回空
llvm::StringRef ConstructReason(llvm::StringRef why)
{
    if (why == "struct_by_value" || why == "struct_rvalue") {
        return "struct_by_value";
    }
    if (why == "inline_asm") {
        return "inline_asm";
    }
    bool className = !why.empty() && why.front() >= 'A' && why.front() <= 'Z';  // Clang 的类名（StmtExpr…）
    if (className || why.startswith("cast:") || why.startswith("unary:") || why.startswith("binary:") ||
        why == "decl_ref" || why == "member" || why == "lvalue_as_rvalue" || why == "unknown_var" ||
        why == "wide_string") {
        return "unsupported_construct";
    }
    return "";
}

// 账本原因：值是不支持的构造给出的未知值时换成对应原因，否则用 fallback
std::string OpenReason(const IntVal &v, const char *fallback)
{
    llvm::StringRef r = v.kind == IntVal::Kind::Top ? ConstructReason(v.why) : llvm::StringRef();
    return r.empty() ? fallback : r.str();
}

PtrVal UnknownPtr(llvm::StringRef why)
{
    PtrVal p;
    p.kind = BaseKind::Unknown;
    p.why = why.str();
    p.off = IntVal::Top(why);
    return p;
}

IntVal WidenInt(const IntVal &a, const IntVal &b);

constexpr size_t kMaxSubs = 4;  // 一个指针至多带几个内层窗口（SPEC-0104）

// 从这个指针起最内层子对象还剩多少字节（取最小的，SPEC-0104 §2.3 3b）；没有窗口、窗口到基址末尾、偏移没有上界 → None
// 从这个指针往前到最内层子对象起点有多少字节（取最小的，SPEC-0107 §2.2 第 3 步）；没有窗口、窗口从基址起点开始、偏移没有下界 → None
llvm::Optional<std::string> SubBack(const PtrVal &p)
{
    if (p.subs.empty() || p.subs.back().head) {
        return llvm::None;
    }
    llvm::Optional<LinExpr> wlo = p.subs.back().off.Lower();
    return wlo && !wlo->IsUnknown() ? llvm::Optional<std::string>(wlo->Show()) : llvm::None;
}

llvm::Optional<std::string> SubRest(const PtrVal &p)
{
    if (p.subs.empty() || p.subs.back().tail) {
        return llvm::None;
    }
    llvm::Optional<LinExpr> whi = p.subs.back().off.Upper();
    LinExpr rest = whi ? LinExpr::Const(p.subs.back().size) - *whi : LinExpr::Unknown();
    return rest.IsUnknown() ? llvm::None : llvm::Optional<std::string>(rest.Show());
}

// 偏移 d 同时加到每个内层窗口上（SPEC-0104 §2.2）
void ShiftSubs(PtrVal &p, const IntVal &d)
{
    for (SubWindow &w : p.subs) {
        w.off = AddVals(w.off, d);
    }
}

// 内层窗口的汇合 / 加宽（SPEC-0104 §2.2；SPEC-0107 §2.2 第 4 步）：大小（与 head / tail）相同的两两合并；对不上的两边都留下——
// 每个窗口对应一条路径，访问越出它就是那条路上越出了子对象（检出优先）。原来对不上就全丢、只查外层
std::vector<SubWindow> JoinSubs(const std::vector<SubWindow> &a, const std::vector<SubWindow> &b, const Comparer *cmp,
                                bool widen)
{
    auto same = [](const SubWindow &x, const SubWindow &y) {
        return x.size == y.size && x.head == y.head && x.tail == y.tail;
    };
    std::vector<SubWindow> out;
    std::vector<bool> used(b.size(), false);
    for (const SubWindow &x : a) {
        size_t j = 0;
        while (j < b.size() && (used[j] || !same(x, b[j]))) {
            ++j;
        }
        SubWindow w = x;
        if (j < b.size()) {
            used[j] = true;
            w.off = widen ? WidenInt(x.off, b[j].off) : JoinInt(x.off, b[j].off, cmp);
        }
        out.push_back(w);
    }
    for (size_t j = 0; j < b.size(); ++j) {
        if (!used[j]) {
            out.push_back(b[j]);
        }
    }
    while (out.size() > kMaxSubs) {
        out.erase(out.begin());  // 丢最外层的
    }
    return out;
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
    out.p.subs = JoinSubs(a.p.subs, b.p.subs, cmp, false);
    return out;
}

// 内存格（A-12）：(基址身份, 常数偏移, 字节数) → 值
using CellKey = std::tuple<std::string, int64_t, int64_t>;

struct State {
    bool reached = false;
    std::map<uint32_t, Value> locals;
    std::map<CellKey, Value> cells;
    std::vector<LinExpr> guards;  // A-14：只含接口符号的 L ≤ 0，至多 8 个，先进先出（SPEC-0016）
    // 本函数写过哪些内存（SPEC-0026：别名实参的字段改过没有）
    std::set<CellKey> written;         // 常数偏移写过的 (基址, 偏移, 大小)
    std::set<std::string> writtenAll;  // 偏移不定写过、或把指针交给了被调者的基址
    bool clobbered = false;  // 经未知指针写过 / 把未知指针交给了被调者：任何基址都可能被改
    bool called = false;  // 调用过可能写全局的函数（不在模型表里的被调者）：全局标量不再是入口值（SPEC-0041）
    bool externAll = false;  // 经 char / void 写过外部内存（CharAlias）：任何外部对象都可能被改（SPEC-0103）
    std::set<std::string> charRoots;  // CharAlias=verify：本路径上经 char / void 写过的外部根（SPEC-0106）
    // AssumeNoWrap：局部线性关系 L ≤ 0（含局部符号 $l<id> / $o<id>，至多 8 条，先进先出；SPEC-0108）
    std::vector<LinExpr> rels;
};

const size_t kMaxGuards = 8;
const size_t kMaxCases = 4;  // 一个值至多带几个情形（SPEC-0025 §2.1）
const size_t kMaxRels = 8;   // 局部线性关系至多几条（SPEC-0108）

// 局部符号（SPEC-0108）：$l<id> 是整数局部此刻的值，$o<id> 是指针局部此刻的偏移
LinExpr LocalSym(uint32_t id, bool pointer)
{
    return LinExpr::Sym(std::string(pointer ? "$o" : "$l") + std::to_string(id));
}

bool HasLocalSym(const LinExpr &e)
{
    return llvm::any_of(e.Terms(), [](const std::pair<const Atom, int64_t> &t) {
        return t.first.kind == Atom::Kind::Sym && llvm::StringRef(t.first.name).startswith("$");
    });
}

// 局部 id 被赋值：含它的局部关系去掉（SPEC-0108）
void DropRels(State &s, uint32_t id)
{
    if (s.rels.empty()) {
        return;
    }
    std::string l = "$l" + std::to_string(id);
    std::string o = "$o" + std::to_string(id);
    s.rels.erase(std::remove_if(s.rels.begin(), s.rels.end(),
                                [&](const LinExpr &r) {
                                    return llvm::any_of(r.Terms(), [&](const std::pair<const Atom, int64_t> &t) {
                                        return t.first.kind == Atom::Kind::Sym &&
                                               (t.first.name == l || t.first.name == o);
                                    });
                                }),
                 s.rels.end());
}

// 一边的情形：常数 → {(c, 这条路上的守卫)}；已带情形 → 每个情形再加上这条路上的守卫；其余没有
bool CasesOf(const IntVal &v, const std::vector<LinExpr> &pathGuards, std::vector<IntCase> &out)
{
    if (v.IsConst()) {
        out = {{v.value.Constant(), pathGuards, {}}};
        return true;
    }
    if (v.kind != IntVal::Kind::Range || v.cases.empty()) {
        return false;
    }
    out = v.cases;
    for (IntCase &c : out) {
        for (const LinExpr &g : pathGuards) {
            if (!llvm::is_contained(c.guards, g) && c.guards.size() < kMaxGuards) {
                c.guards.push_back(g);
            }
        }
    }
    return true;
}

// 两边的情形合在一起：同一个常数的守卫取交集
std::vector<IntCase> MergeCases(const std::vector<IntCase> &a, const std::vector<IntCase> &b)
{
    std::vector<IntCase> out = a;
    for (const IntCase &cb : b) {
        auto it = std::find_if(out.begin(), out.end(), [&](const IntCase &c) { return c.value == cb.value; });
        if (it == out.end()) {
            out.push_back(cb);
            continue;
        }
        std::vector<LinExpr> common;
        for (const LinExpr &g : it->guards) {
            if (llvm::is_contained(cb.guards, g)) {
                common.push_back(g);
            }
        }
        it->guards = common;
        if (!(it->writes == cb.writes)) {
            it->writes.clear();  // 两边写入不同：丢掉（变弱，仍可靠，SPEC-0079）
        }
    }
    std::sort(out.begin(), out.end(), [](const IntCase &x, const IntCase &y) { return x.value < y.value; });
    return out;
}
const int kMaxHeadUpdates = 20;  // A-33：每个循环头至多更新 20 次
const int kNarrowPasses = 2;     // 下降的遍数（SPEC-0017 §2.1）

bool operator==(const State &a, const State &b)
{
    return a.reached == b.reached && a.locals == b.locals && a.cells == b.cells && a.guards == b.guards &&
           a.written == b.written && a.writtenAll == b.writtenAll && a.clobbered == b.clobbered &&
           a.called == b.called && a.externAll == b.externAll && a.charRoots == b.charRoots && a.rels == b.rels;
}

// envOf：给定汇合后成立的守卫（两边的交集），建比较符号界的环境（SPEC-0023）；空 = 只认常数差
// selfOf：局部号 → 它若是形参，入口符号 pK（合并时不把变量自己的原值当钳位目标，SPEC-0043）
State Join(const State &a, const State &b, const std::function<SymEnv(const std::vector<LinExpr> &)> &envOf = nullptr,
           const std::function<std::string(uint32_t)> &selfOf = nullptr)
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
    out.written = a.written;  // 写过的：取并集
    out.written.insert(b.written.begin(), b.written.end());
    out.writtenAll = a.writtenAll;
    out.writtenAll.insert(b.writtenAll.begin(), b.writtenAll.end());
    out.clobbered = a.clobbered || b.clobbered;
    out.called = a.called || b.called;
    out.externAll = a.externAll || b.externAll;
    out.charRoots = a.charRoots;  // SPEC-0106：取并集
    out.charRoots.insert(b.charRoots.begin(), b.charRoots.end());
    for (const LinExpr &r : a.rels) {
        if (llvm::is_contained(b.rels, r)) {
            out.rels.push_back(r);  // 局部关系：取两边都有的（SPEC-0108）
        }
    }
    SymEnv env;
    SymEnv envA;
    SymEnv envB;
    Comparer cmp;
    if (envOf) {
        env = envOf(out.guards);
        cmp.env = &env;
        cmp.guards = &out.guards;
        envA = envOf(a.guards);
        envB = envOf(b.guards);
        cmp.sideA = &envA;
        cmp.sideB = &envB;
        cmp.guardsA = &a.guards;
        cmp.guardsB = &b.guards;
    }
    for (const auto &entry : a.locals) {
        auto it = b.locals.find(entry.first);
        if (it == b.locals.end()) {
            out.locals[entry.first] = Value();
            continue;
        }
        cmp.self = selfOf ? selfOf(entry.first) : std::string();
        Value j = JoinValue(entry.second, it->second, &cmp);
        // 两边是不同的常数（或已带情形）：记下各自路径上的守卫（SPEC-0025 §2.1）
        std::vector<IntCase> ca;
        std::vector<IntCase> cb;
        if (!j.isPtr && j.i.kind == IntVal::Kind::Range && !entry.second.isPtr && !it->second.isPtr &&
            CasesOf(entry.second.i, a.guards, ca) && CasesOf(it->second.i, b.guards, cb)) {
            std::vector<IntCase> merged = MergeCases(ca, cb);
            if (merged.size() <= kMaxCases) {
                j.i.cases = merged;
            }
        }
        out.locals[entry.first] = j;
    }
    for (const auto &entry : a.cells) {
        auto it = b.cells.find(entry.first);
        if (it == b.cells.end()) {
            continue;  // 只有一边有：丢掉（读的时候按没记过的规则）
        }
        if (it->second == entry.second) {
            out.cells.insert(entry);
            continue;
        }
        // 两边是不同的整数：汇合成两头都有界的区间时保留（SPEC-0069）；循环头上 WidenState 会丢掉变了的格
        if (entry.second.isPtr || it->second.isPtr) {
            continue;
        }
        Value j = JoinValue(entry.second, it->second, &cmp);
        llvm::Optional<LinExpr> lo = j.i.Lower();
        llvm::Optional<LinExpr> hi = j.i.Upper();
        if (!j.isPtr && j.i.kind == IntVal::Kind::Range && lo && hi && !lo->IsUnknown() && !hi->IsUnknown()) {
            j.i.cases.clear();
            out.cells[entry.first] = j;
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
    IntVal out = IntVal::Range(keepLo ? al : llvm::None, keepHi ? ah : llvm::None, tag);
    JoinCong(a, b, out);  // SPEC-0084
    return out;
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
    out.p.subs = JoinSubs(a.p.subs, b.p.subs, nullptr, true);
    return out;
}

// 循环头的入口：next = old ⊔ 新的（格与守卫已经是交集，只会变少）；局部逐个 widening
// only：这个循环头上要 widen 的局部（本循环里被赋值的，SPEC-0046）；空指针 = 全部
State WidenState(const State &old, const State &next, const std::set<uint32_t> *only = nullptr)
{
    State out = next;
    for (auto &entry : out.locals) {
        if (only != nullptr && only->count(entry.first) == 0) {
            continue;  // 本循环里不变：取汇合值，随外层收敛（外层循环头会 widen 它）
        }
        auto it = old.locals.find(entry.first);
        if (it != old.locals.end()) {
            entry.second = WidenValue(it->second, entry.second);
        }
    }
    // 内存格：循环里变了的丢掉（同 SPEC-0069 之前的汇合规则，保证收敛）
    for (auto it = out.cells.begin(); it != out.cells.end();) {
        auto before = old.cells.find(it->first);
        it = before == old.cells.end() || before->second == it->second ? std::next(it) : out.cells.erase(it);
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
            case Atom::Kind::BExt:
                out.insert("bext:" + atom.name);
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
// AssumeNoWrap 的访问（SPEC-0108 §2.2）：偏移与字节数的符号式（含局部符号）
struct SymAccess {
    LinExpr off;
    LinExpr bytes;
    bool upto = false;  // 模型的"至多 bytes 字节"（实际 1 … bytes，SPEC-0034）
};

struct RawAccess {
    std::string kind;
    std::string dir;  // "back"：负偏移（SPEC-0039）
    LinExpr have;
    std::string obj;
    LinExpr lo;
    LinExpr hi;
    llvm::Optional<LinExpr> start;
    Tag tag = Tag::Exact;
    std::string at;
    std::vector<std::string> guard;  // 与 have / end 有公共符号的守卫（ShowLe0）
    int64_t effDepth = 0;  // 堆对象经过了几层 ret_new（本函数里直接访问封装分配器返回的对象，SPEC-0113）
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
                const State &s, const std::vector<LinExpr> &forced = {}, bool checkBack = true,
                const SymAccess *sym = nullptr);
    // AssumeNoWrap（SPEC-0108 §2.2）：含局部符号的式子 e 的上 / 下界（只含接口符号，用局部关系与区间消去局部符号）
    llvm::Optional<std::pair<LinExpr, Tag>> Bound(const LinExpr &e, bool upper, const State &s);
    llvm::Optional<std::pair<LinExpr, Tag>> AtomBound(llvm::StringRef name, bool upper, const State &s) const;
    llvm::Optional<SymAccess> SymAt(ExprId addrExpr, const PtrVal &addr, const LinExpr &bytes, const State &s);
    void CheckSubs(const char *kind, const PtrVal &addr, const IntVal &bytes, const std::string &at, const State &s,
                   const std::vector<LinExpr> &forced);
    void BackAccess(const char *kind, const PtrVal &addr, ExprId addrExpr, const std::string &at, const State &s,
                    const std::vector<LinExpr> &forced, const SymAccess *sym = nullptr);
    void Record(RawAccess a, const State &s, const std::vector<LinExpr> &forced);
    void AddOpen(const std::string &reason, const std::string &at);
    // mod-ref（SPEC-0045）：记下可能写的位置；调用点按被调者的 mods 只让写到的位置失效
    void AddMod(const PtrVal &addr, llvm::Optional<int64_t> size);
    void AddModText(const std::string &mod);
    void ApplyMods(const fir::Inst &inst, const std::vector<std::string> &calleeMods, State &s);
    // 形参所指结构体的字段进函数后改过没有（SPEC-0026 / SPEC-0040）；形参别名（同所指类型的结构体形参，SPEC-0040 §2.2）
    bool Changed(const State &s, const std::string &id, int64_t off, int64_t size) const;
    void MarkWritten(State &s, const std::string &id) const;
    void AliasWritten(State &s, const std::string &id) const;
    std::string RootOf(const std::string &id) const;           // SPEC-0106
    void NoteAssume(const State &s, const std::string &root);  // SPEC-0106
    struct ExternalView {
        enum class Kind { None, Param, ParamPtr, Global };
        Kind kind = Kind::None;
        int param = -1;
        std::string key;        // 类型键
        std::string parentKey;  // 成员数组：外层的类型键
    };
    ExternalView ViewOf(const std::string &id) const;
    std::string TypeKey(fir::TypeId t) const;
    fir::TypeId ParamPointee(int k) const;
    // 类型键 → 全局基址 id（SPEC-0091）
    const std::vector<std::string> &GlobalsWithKey(const std::string &key) const
    {
        static const std::vector<std::string> none;
        if (!keyIndexed) {
            keyIndexed = true;
            for (const fir::Global &g : m.globals) {
                globalsByKey[TypeKey(g.type)].push_back("G" + g.key);
            }
        }
        auto it = key.empty() ? globalsByKey.end() : globalsByKey.find(key);
        return it != globalsByKey.end() ? it->second : none;
    }
    mutable bool keyIndexed = false;
    mutable std::map<std::string, std::vector<std::string>> globalsByKey;
    llvm::Optional<uint32_t> PointeeRecord(int param) const;
    // 形参所指的类型（别名判断用）：结构体按记录号，整数按宽度与符号；别的 → None（SPEC-0040 §2.2、SPEC-0071）
    // ---- 模型（SPEC-0018）----
    void ApplyAccessModel(const fir::Inst &inst, const models::AccessModel &model, const State &s);
    Value HeapResult(const fir::Inst &inst, const std::string &size, int64_t effDepth, const State &s);
    void RecordReturn(const fir::Inst &inst, const State &s);
    std::vector<summary::Eff> Effects() const;
    llvm::Optional<summary::Eff> ReturnRange() const;
    // 返回实参指针（或它所指结构体的指针字段在入口时的值）加同一个偏移（SPEC-0070）
    llvm::Optional<summary::Eff> ReturnPointer() const;
    Value RetPtrResult(const fir::Inst &inst, const summary::Eff &e, const summary::Summary &callee, const State &s);
    // 写效应 field 在调用点要写的格与新值（调用前的状态，SPEC-0073 §2.2）
    // conditional 非空时：带 when: ret == c 的写效应放进它（返回常数 c、格、值），SPEC-0079
    std::vector<std::pair<CellKey, Value>> FieldWrites(
        const fir::Inst &inst, const summary::Summary &callee, const State &s,
        std::vector<std::pair<int64_t, std::pair<CellKey, Value>>> *conditional = nullptr);
    // 调用刚结束时某一格的值（情形里的写回要求筛选时还相同，SPEC-0079）
    static std::string CellText(const State &s, const CellKey &key)
    {
        auto it = s.cells.find(key);
        if (it == s.cells.end()) {
            return "";
        }
        return it->second.isPtr ? "ptr" : "int:" + it->second.i.Show();
    }
    // 被调者的式子换成调用方的：形参换成可信的确切实参，全局按调用点的 gvals；换不了 → None（SPEC-0054 / 0064 / 0070）
    // 字段符号 p<k>->f 按被调者 fields 表从第 k 个实参所指对象读调用前的值；没换掉的被调者接口符号算换不了（SPEC-0072）
    llvm::Optional<LinExpr> CallerExpr(const fir::Inst &inst, const LinExpr &calleeExpr, const State &s,
                                       const summary::Summary *callee);
    std::vector<summary::Eff> CondEffects() const;
    IntVal CondResult(const fir::Inst &inst, const std::vector<summary::Eff> &effs, const State &s,
                      const summary::Summary *callee = nullptr);
    bool FilterCases(ExprId e, const IntVal &v, fir::CmpOp op, int64_t k, State &s);
    std::vector<State> FlowOut(size_t block, State s);
    // 表达式里出现的局部（Local 叶子，SPEC-0075）
    void CollectLocals(ExprId e, std::set<uint32_t> &out) const
    {
        if (e >= f.exprs.size()) {
            return;
        }
        const fir::Expr &x = f.exprs[e];
        if (x.op == ExprOp::Local) {
            out.insert(static_cast<uint32_t>(x.value));
        }
        CollectLocals(x.a, out);
        CollectLocals(x.b, out);
    }

    // ---- 条件边与守卫（SPEC-0016）----
    SymEnv Env(const State &s) const;
    bool Faithful(ExprId e, const IntVal &v, const SymEnv &env) const;
    bool UpperSound(ExprId e, const State &s, const SymEnv &env);
    bool Assume(ExprId c, bool negate, State &s);
    bool AssumeCmp(fir::CmpOp op, ExprId a, ExprId b, State &s);
    bool Narrow(ExprId e, fir::CmpOp op, const LinExpr &c, State &s);
    // 按比较收窄一个区间（Narrow 与 NarrowPtr 共用，SPEC-0081）：收成空 → false；changed：有没有收窄
    // coarse：收窄出的上界不当代码给出的界（标 widen，SPEC-0083）
    bool NarrowRange(const IntVal &v, fir::CmpOp op, const LinExpr &c, const State &s, IntVal &out, bool &changed,
                     bool coarse = false);
    bool NarrowPtr(ExprId e, fir::CmpOp op, const LinExpr &c, State &s, bool coarse);
    uint32_t WrittenLocal(ExprId e, const State &s);
    static void AddGuard(State &s, const LinExpr &le0);
    void NonEmptyGuard(const IntVal &v, State &s);
    bool AssumeNull(fir::CmpOp op, const Value &va, const Value &vb, State &s);
    void SetLocal(State &s, uint32_t dst, Value v) const
    {
        bool ptr = IsPtrLocal(dst);
        if (ptr && !v.isPtr) {
            Value p;
            p.isPtr = true;
            p.p = UnknownPtr(v.i.IsConst() && v.i.value.Constant() == 0 ? "null" : "int_as_ptr");
            if (v.i.kind == IntVal::Kind::Top && !ConstructReason(v.i.why).empty()) {
                p.p = UnknownPtr(v.i.why);  // 不支持的构造给出的指针：原因留着（账本原因，SPEC-0060）
            }
            v = p;
        } else if (!ptr && v.isPtr) {
            Value i;
            i.i = IntVal::Top("ptr_as_int");
            v = i;
        }
        DropRels(s, dst);  // 赋值：含它的局部关系失效（SPEC-0108）
        s.locals[dst] = v;
    }
    // AssumeNoWrap 的符号式（SPEC-0108 §2.1）：整数式 / 指针（基址身份, 偏移）；含局部符号，没有符号式 → None
    llvm::Optional<LinExpr> SymOf(ExprId e, const State &s, int depth = 0);
    llvm::Optional<std::pair<std::string, LinExpr>> SymPtr(ExprId e, const State &s, int depth = 0);
    void NoteRel(fir::CmpOp op, ExprId a, ExprId b, State &s);

    State Entry() const;
    std::vector<summary::Req> Merge() const;
    summary::Req ToReq(const RawAccess &a, size_t number) const;
    void RecordCall(const fir::Inst &inst, const State &s, bool alt = false);
    // 调用点上已经不是入口值的整数全局（调用点记录的 gvals，SPEC-0064）
    // locals 非空时：代码给出的区间值写成调用点局部 _gv_<名字>，区间写进 locals（SPEC-0077）；否则区间写 "?"
    std::map<std::string, std::string> GlobalsAt(const State &s,
                                                 std::map<std::string, summary::Local> *locals = nullptr) const;
    // 没有摘要可用的被调者：保守失效（清格、指针实参整块、可能写全局，SPEC-0026 / 0041 / 0045）
    void Clobber(const fir::Inst &inst, models::Kind kind, llvm::StringRef name, State &s);
    // 形参结构体的成员数组与外层对象是同一块内存（SPEC-0090）：写 addr 的 [off, off + size)（off 为 None：偏移不定）时，
    // 另一种写法上重叠的格失效
    void InvalidateFamily(State &s, const PtrVal &addr, llvm::Optional<int64_t> off, int64_t size) const;
    // 对象 p 所在的整块内存的格全部失效：同一基址、它的成员数组（id.成员）、成员数组的外层对象及其成员（SPEC-0089）
    void EraseObject(State &s, const PtrVal &p) const
    {
        auto family = [](llvm::StringRef base, const std::string &id) {
            return base == id || base.startswith(id + ".");
        };
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            llvm::StringRef base = std::get<0>(it->first);
            bool same = family(base, p.id) || (p.hasParent && family(base, p.parentId));
            it = same ? s.cells.erase(it) : std::next(it);
        }
        if (p.hasParent) {
            s.writtenAll.insert(p.parentId);
        }
    }
    // 结构体整体赋值按字段拷值（SPEC-0088）：做了返回 true；取不到类型 / 基址 / 偏移返回 false（调用方照旧 Clobber）
    bool CopyStruct(const fir::Inst &inst, State &s);
    // 局部函数指针（S9.1，06- A-60 ①，SPEC-0058）：只被赋过函数地址常量的局部 → 可能指向的函数
    void ResolveFnLocals();
    std::vector<std::string> TargetsOf(ExprId e) const;
    bool AltCall(const fir::Inst &inst, const std::vector<std::string> &targets, State &s);
    ExprId StripCasts(ExprId e) const
    {
        while (e < f.exprs.size() && f.exprs[e].op == ExprOp::Cast) {
            e = f.exprs[e].a;
        }
        return e;
    }
    // locals 非空时：区间值的整数字段写成调用点局部 _fld<k>_<字段>，区间写进 locals（SPEC-0069）
    std::map<std::string, summary::ArgValue> ArgFields(const PtrVal &p, const State &s, size_t k = 0,
                                                       std::map<std::string, summary::Local> *locals = nullptr) const;
    std::string ShowState(const fir::Block &b, const State &s) const;

public:
    std::vector<std::string> *trace = nullptr;
    Options options;  // SPEC-0103
    const std::map<std::string, summary::Summary> *callees = nullptr;
    const std::map<std::string, int64_t> *globalSizes = nullptr;  // 全局键 → 定义处的大小（SPEC-0032）

private:
    const fir::Module &m;
    const fir::Function &f;
    const models::ModelTable &models;
    std::vector<RawAccess> accesses;
    std::vector<RawAccess> safe;
    std::vector<summary::Open> opens;
    std::set<std::string> mods;                             // 可能写的位置（记录那一遍，SPEC-0045）
    std::set<std::pair<std::string, std::string>> assumes;  // CharAlias=verify 的不别名假设（记录那一遍，SPEC-0106）
    std::vector<summary::Call> calls;
    std::map<uint32_t, std::set<std::string>> fnTargets;  // 局部函数指针可能指向的函数键（SPEC-0058）
    mutable std::map<std::string, summary::FieldInfo> fields;
    mutable std::map<std::string, std::string> globalTypes;  // 读到过的全局标量：g:名 → 类型（Env 用）
    std::vector<std::pair<Value, std::vector<LinExpr>>>
        returns;  // 记录那一遍里每个 ret 的值与当时的守卫（SPEC-0024、0025）
    std::vector<std::map<std::pair<int, int64_t>, Value>>
        outs;  // 每个 ret 时指针形参 K 所指对象里偏移 off 的指针格的值（出参，SPEC-0053 / 0061）
    // 写效应 field（SPEC-0073）：每个 ret 时形参所指对象各整数字段的状态
    struct FieldAt {
        enum Kind { Written, Unchanged, Unknown } kind = Unknown;
        LinExpr value;
    };
    struct FieldSlot {
        std::string name;  // pK->f（伪字段 pK->_0）；全局：g:名字（SPEC-0077）
        std::string id;    // 格的基址：P<k> / G<键>
        int param = 0;     // 全局：-1
        int64_t off = 0;
        int64_t size = 0;
        fir::TypeId type = fir::kNone;
    };
    std::vector<FieldSlot> FieldSlots() const;
    mutable llvm::Optional<std::vector<FieldSlot>> slotsCache;  // FieldSlots 只算一次（SPEC-0080）
    // 格的基址 G<键> → 全局（SPEC-0080：GlobalsAt 只看状态里动过的全局）
    // 同名的取最后一个（同原来按表顺序找、后面的覆盖前面的）
    const fir::Global *GlobalOf(const std::string &id) const
    {
        IndexGlobals();
        auto it = globalById.find(id);
        return it != globalById.end() ? it->second : nullptr;
    }
    // g:名字（显示名）→ 同名的全局（函数内 static 可能与全局同名，都要）：只给老格式的 mods 兜底，保守（SPEC-0086）
    const std::vector<const fir::Global *> &GlobalsNamed(const std::string &name) const
    {
        static const std::vector<const fir::Global *> none;
        IndexGlobals();
        auto it = globalsByName.find(name);
        return it != globalsByName.end() ? it->second : none;
    }
    // 接口符号（GlobalSymbol）→ 全局：精确（SPEC-0086）
    const std::vector<const fir::Global *> &GlobalsBySym(const std::string &sym) const
    {
        static const std::vector<const fir::Global *> none;
        IndexGlobals();
        auto it = globalsBySym.find(sym);
        return it != globalsBySym.end() ? it->second : none;
    }
    void IndexGlobals() const
    {
        if (indexed) {
            return;
        }
        indexed = true;
        for (const fir::Global &g : m.globals) {
            globalById["G" + g.key] = &g;
            globalsByName["g:" + g.name].push_back(&g);
            globalsBySym[GlobalSymbol(g.key, g.name)].push_back(&g);
        }
    }
    mutable bool indexed = false;
    mutable std::map<std::string, const fir::Global *> globalById;
    mutable std::map<std::string, std::vector<const fir::Global *>> globalsByName;
    mutable std::map<std::string, std::vector<const fir::Global *>> globalsBySym;
    std::vector<std::map<std::string, FieldAt>> fieldOuts;
    std::vector<llvm::Optional<int64_t>> outRets;  // 与 fieldOuts 对齐：这条路径返回的常数（SPEC-0079）
    std::vector<bool> heads;                       // 循环头（回边的目标），按块下标
    bool recording = false;     // 只有最后一遍记访问、调用点、账本（SPEC-0017 §2.1）
    bool edgeFromHead = false;  // 当前 Assume 的边从循环头出发：收窄出来的上界标 loop
    bool resolving = false;  // 正在把解析出的间接调用当直接调用（调用点记录标 ind，SPEC-0058）
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
            int64_t stride = r.stride;
            LinExpr anchor = r.anchor;
            r = IntVal::Range(LinExpr::Const(0), r.Upper(), r.tag);
            // 回绕减 2^W：步长是 2 的幂且不超过 2^W 时余数不变（SPEC-0084）
            if (stride >= 2 && (stride & (stride - 1)) == 0 && stride <= tmax) {
                SetCong(r, anchor, stride);
            }
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
            const fir::Global *g = GlobalOf("G" + x.text);  // 按键查表（SPEC-0080）
            if (g == nullptr) {
                v.p = UnknownPtr("unknown_global");
                return v;
            }
            int64_t size = 0;
            bool known = TypeSize(m, g->type, size);
            if (!known && globalSizes != nullptr) {
                // 本 TU 里类型不完整（extern UINT32 g_x[];）：取定义所在 TU 的大小（D-34，SPEC-0032）
                auto it = globalSizes->find(g->key);
                if (it != globalSizes->end()) {
                    size = it->second;
                    known = true;
                }
            }
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
                IntVal d = EvalInt(x.b, s);
                p.off = AddVals(p.off, d);
                ShiftSubs(p, d);
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
                v.p.subs = p.subs;  // 外面的窗口照带（SPEC-0104）
                ShiftSubs(v.p, IntVal::Exact(LinExpr::Const(x.value)));
                return v;
            }
            if (p.kind != BaseKind::Unknown) {
                p.off = AddVals(p.off, IntVal::Exact(LinExpr::Const(x.value)));
                ShiftSubs(p, IntVal::Exact(LinExpr::Const(x.value)));
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
        case ExprOp::SubObj: {
            // 子对象的起点（SPEC-0104 §2.2）：推一个内层窗口
            v = Eval(x.a, s);
            if (!v.isPtr || v.p.kind == BaseKind::Unknown || v.p.kind == BaseKind::Func || x.size <= 0 ||
                options.subobject == SubMode::Off) {
                return v;  // Subobject=off：只查整个对象（SPEC-0105）
            }
            SubWindow w;
            w.size = x.size;
            if (v.p.off.IsConst() && v.p.hasSize && v.p.size.IsConst()) {
                int64_t start = v.p.off.value.Constant();
                w.head = start == 0;
                w.tail = start + x.size == v.p.size.Constant();
            }
            if (w.head && w.tail) {
                return v;  // 就是整个基址（例如形参所指结构体的成员数组 p->aucBuf，SPEC-0022 已经以它为界）
            }
            v.p.subs.push_back(w);
            if (v.p.subs.size() > kMaxSubs) {
                v.p.subs.erase(v.p.subs.begin());  // 丢最外层的
            }
            return v;
        }
    }
    v.i = IntVal::Top("expr");
    return v;
}

// load 的值：命中格；形参所指结构体的字段 → pi->字段（指针字段 → *pi->字段 的基址）；全局标量 → g:名（A-28）；其余 data
bool Analyzer::Changed(const State &s, const std::string &id, int64_t off, int64_t size) const
{
    // 经 char / void 写过外部内存（CharAlias）：形参所指、指针字段所指、全局都可能被改（SPEC-0103）
    if (s.externAll && (llvm::StringRef(id).startswith("P") || llvm::StringRef(id).startswith("G"))) {
        return true;
    }
    return s.clobbered || s.writtenAll.count(id) != 0 || llvm::any_of(s.written, [&](const CellKey &w) {
               return std::get<0>(w) == id && std::get<1>(w) < off + size && off < std::get<1>(w) + std::get<2>(w);
           });
}

// 第 param 个形参所指的结构体（记录号）；不是指向结构体的指针 → None
llvm::Optional<uint32_t> Analyzer::PointeeRecord(int param) const
{
    for (const fir::Local &l : f.locals) {
        if (l.param != param || l.type >= m.types.size() || m.types[l.type].kind != fir::TypeKind::Ptr) {
            continue;
        }
        fir::TypeId pointee = m.types[l.type].to;
        if (pointee < m.types.size() && m.types[pointee].kind == fir::TypeKind::Rec) {
            return m.types[pointee].record;
        }
    }
    return llvm::None;
}

// 基址 id 整个可能被改：记进 writtenAll，同类型的形参别名也算（SPEC-0040 §2.2）
void Analyzer::MarkWritten(State &s, const std::string &id) const
{
    s.writtenAll.insert(id);
    AliasWritten(s, id);
}

// 外部内存的类型键（SPEC-0091）：数组取元素类型，其余同 TypeText（rec#号、u8、u32、void…）
std::string Analyzer::TypeKey(fir::TypeId t) const
{
    while (t < m.types.size() && m.types[t].kind == fir::TypeKind::Arr) {
        t = m.types[t].to;
    }
    return t < m.types.size() ? fir::TypeText(m, t) : std::string();
}

// 第 k 个形参所指的类型；不是指针 → kNone
fir::TypeId Analyzer::ParamPointee(int k) const
{
    for (const fir::Local &l : f.locals) {
        if (l.param == k && l.type < m.types.size() && m.types[l.type].kind == fir::TypeKind::Ptr) {
            return m.types[l.type].to;
        }
    }
    return fir::kNone;
}

// 基址 id 是哪一种外部内存、类型键是什么（SPEC-0091）：形参所指 P<k>（成员数组 P<k>.<m> 另带外层的键）、
// 形参结构体里指针字段所指 PP<k>.<f>、全局 G…；局部、堆、字符串不是
Analyzer::ExternalView Analyzer::ViewOf(const std::string &id) const
{
    ExternalView v;
    llvm::StringRef text = id;
    auto fieldOf = [&](int k, llvm::StringRef name) -> const fir::Field * {
        fir::TypeId pointee = ParamPointee(k);
        if (pointee >= m.types.size() || m.types[pointee].kind != fir::TypeKind::Rec) {
            return nullptr;
        }
        const fir::Record *r = m.FindRecord(m.types[pointee].record);
        for (const fir::Field &field : r != nullptr ? r->fields : std::vector<fir::Field>()) {
            if (field.name == name) {
                return &field;
            }
        }
        return nullptr;
    };
    if (text.consume_front("PP")) {
        unsigned k = 0;
        if (text.consumeInteger(10, k) || !text.consume_front(".")) {
            return v;
        }
        const fir::Field *field = fieldOf(static_cast<int>(k), text);
        if (field != nullptr && field->type < m.types.size() && m.types[field->type].kind == fir::TypeKind::Ptr) {
            v.kind = ExternalView::Kind::ParamPtr;
            v.param = static_cast<int>(k);
            v.key = TypeKey(m.types[field->type].to);
        }
        return v;
    }
    if (text.size() >= 2 && text[0] == 'P' && std::isdigit(static_cast<unsigned char>(text[1]))) {
        text = text.drop_front();
        unsigned k = 0;
        if (text.consumeInteger(10, k)) {
            return v;
        }
        v.kind = ExternalView::Kind::Param;
        v.param = static_cast<int>(k);
        std::string outer = ParamPointee(v.param) != fir::kNone ? TypeKey(ParamPointee(v.param)) : std::string();
        if (text.consume_front(".")) {
            const fir::Field *field = fieldOf(v.param, text);
            v.key = field != nullptr ? TypeKey(field->type) : std::string();
            v.parentKey = outer;
        } else {
            v.key = outer;
        }
        return v;
    }
    if (text.startswith("G")) {
        if (const fir::Global *g = GlobalOf(id)) {
            v.kind = ExternalView::Kind::Global;
            v.key = TypeKey(g->type);
        }
    }
    return v;
}

// 外部内存的根（接口说法，SPEC-0106）：P<k>、P<k>.m → p<k>；PP<k>.f → p<k>->f；全局 → g:名；其余（局部、堆、字符串）空
std::string Analyzer::RootOf(const std::string &id) const
{
    llvm::StringRef text = id;
    unsigned k = 0;
    if (text.consume_front("PP")) {
        if (text.consumeInteger(10, k) || !text.consume_front(".")) {
            return "";
        }
        return "p" + std::to_string(k) + "->" + text.str();
    }
    if (text.size() >= 2 && text[0] == 'P' && std::isdigit(static_cast<unsigned char>(text[1]))) {
        text = text.drop_front();
        return text.consumeInteger(10, k) ? "" : "p" + std::to_string(k);
    }
    if (text.startswith("G")) {
        if (const fir::Global *g = GlobalOf(id)) {
            return "g:" + g->name;
        }
    }
    return "";
}

// CharAlias=verify（SPEC-0106）：本路径上经 char 写过外部根 X，之后用到外部根 Y（读、交给被调者）→ 假设 X 与 Y 不重叠
void Analyzer::NoteAssume(const State &s, const std::string &root)
{
    if (!recording || !options.charVerify || root.empty()) {
        return;
    }
    for (const std::string &x : s.charRoots) {
        if (x != root) {
            assumes.insert(x < root ? std::make_pair(x, root) : std::make_pair(root, x));
        }
    }
}

// 经外部内存 id 写：可能是同一块的其它外部内存——格失效、记进 writtenAll（SPEC-0091，取代 SPEC-0040 §2.2）。
// 指针视角（形参所指、指针字段所指）：类型键相同，或它是 char / void；全局对象：类型键相同（经全局写时不算别的全局）。
// char / void 的写不影响别的类型（SPEC-0040 的取舍）；同一形参自己的成员 / 外层由 InvalidateFamily 按范围处理
void Analyzer::AliasWritten(State &s, const std::string &id) const
{
    ExternalView x = ViewOf(id);
    if (x.kind == ExternalView::Kind::None) {
        return;
    }
    auto charLike = [](const std::string &k) { return k == "u8" || k == "i8" || k == "void"; };
    // CharAlias=verify（SPEC-0106）：记下写过的根，之后照 false 处理（不让别的外部内存失效）
    if (options.charVerify && charLike(x.key)) {
        std::string root = RootOf(id);
        if (!root.empty()) {
            s.charRoots.insert(root);
        }
    }
    // CharAlias（SPEC-0103，默认打开）：经 char / void 写外部内存 → 任何外部对象都可能被改：外部内存的格全部失效（写的那个对象自己除外，
    // 它的成员 / 外层由 InvalidateFamily 按范围处理），之后读字段、全局标量都不是入口值
    if (options.charAlias && charLike(x.key)) {
        s.externAll = true;
        std::string root = x.kind == ExternalView::Kind::Param ? "P" + std::to_string(x.param) : id;
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            llvm::StringRef base = std::get<0>(it->first);
            bool own = base == root || base.startswith(root + ".") || base == id;
            bool external = base.startswith("P") || base.startswith("G");
            it = external && !own ? s.cells.erase(it) : std::next(it);
        }
        return;
    }
    auto same = [&](const ExternalView &y) {
        auto eq = [](const std::string &a, const std::string &b) { return !a.empty() && a == b; };
        return eq(y.key, x.key) || eq(y.key, x.parentKey) || eq(y.parentKey, x.key) || eq(y.parentKey, x.parentKey);
    };
    auto hit = [&](const ExternalView &y) {
        if (y.kind == ExternalView::Kind::None) {
            return false;
        }
        if (y.kind == ExternalView::Kind::Global) {
            return x.kind != ExternalView::Kind::Global && same(y);
        }
        return same(y) || charLike(y.key);
    };
    auto ownFamily = [&](llvm::StringRef other) {
        if (x.kind != ExternalView::Kind::Param) {
            return other == id;
        }
        std::string root = "P" + std::to_string(x.param);
        return other == root || other.startswith(root + ".");
    };
    // 候选：所有指针形参、格里出现的外部基址（成员数组、指针字段所指、全局）、类型键相同的全局（之后读全局标量要知道改过）
    std::set<std::string> candidates;
    for (const fir::Local &l : f.locals) {
        if (l.param >= 0 && ParamPointee(l.param) != fir::kNone) {
            candidates.insert("P" + std::to_string(l.param));
        }
    }
    for (const auto &cell : s.cells) {
        llvm::StringRef base = std::get<0>(cell.first);
        if (base.startswith("P") || base.startswith("G")) {
            candidates.insert(base.str());
        }
    }
    if (x.kind != ExternalView::Kind::Global) {
        for (const std::string &key : {x.key, x.parentKey}) {
            for (const std::string &g : GlobalsWithKey(key)) {
                candidates.insert(g);
            }
        }
    }
    for (const std::string &other : candidates) {
        if (ownFamily(other) || !hit(ViewOf(other))) {
            continue;
        }
        bool param = other.size() >= 2 && other[0] == 'P' && std::isdigit(static_cast<unsigned char>(other[1])) &&
                     other.find('.') == std::string::npos;
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            llvm::StringRef base = std::get<0>(it->first);
            bool drop = base == other || (param && base.startswith(other + "."));
            it = drop ? s.cells.erase(it) : std::next(it);
        }
        s.writtenAll.insert(other);
    }
}

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
    if (addr.kind == BaseKind::Param && off && !Changed(s, addr.id, *off, size)) {
        // 形参的类型 → 所指的记录 → 这个偏移处的字段。进函数后改过的不是入口值，读成 data（SPEC-0040 §2.1）
        int param = std::atoi(addr.obj.c_str() + 2);  // "*p0"
        for (const fir::Local &l : f.locals) {
            if (l.param != param || l.type >= m.types.size() || m.types[l.type].kind != fir::TypeKind::Ptr) {
                continue;
            }
            fir::TypeId pointee = m.types[l.type].to;
            // 指向（宽于 char 的）整数的形参：*pK 是伪字段 pK->_0（经指针传进来的长度，SPEC-0071）；字节缓冲区不算
            int64_t scalarSize = 0;
            if (pointee < m.types.size() && m.types[pointee].kind == fir::TypeKind::Int && m.types[pointee].width > 8 &&
                *off == 0 && !v.isPtr && TypeSize(m, pointee, scalarSize) && scalarSize == size) {
                std::string name = "p" + std::to_string(param) + "->_0";
                summary::FieldInfo info;
                info.off = 0;
                info.size = scalarSize;
                info.type = fir::TypeText(m, pointee);
                fields[name] = info;
                v.i = IntVal::Exact(LinExpr::Sym(name));
                return v;
            }
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
    // 全局标量：调用过可能写全局的函数、或它可能被改过之后，不是入口值，读成 data（SPEC-0041）
    if (addr.kind == BaseKind::Global && off && *off == 0 && !v.isPtr && !s.called && !Changed(s, addr.id, 0, size)) {
        if (const fir::Global *found = GlobalOf(addr.id)) {
            const fir::Global &g = *found;
            int64_t gsize = 0;
            if (g.type < m.types.size() && m.types[g.type].kind == fir::TypeKind::Int && TypeSize(m, g.type, gsize) &&
                gsize == size) {
                std::string sym = GlobalSymbol(g.key, g.name);  // 带身份（SPEC-0086）
                globalTypes[sym] = fir::TypeText(m, g.type);
                v.i = IntVal::Exact(LinExpr::Sym(sym));
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
// 基址前面还有多少字节（SPEC-0039 §2.1）：形参 / 形参指针字段 → bext(…)；局部、全局、堆、字符串、成员数组 → 0（偏移从对象 / 成员起点算）
llvm::Optional<LinExpr> BackOf(const PtrVal &p)
{
    switch (p.kind) {
        case BaseKind::Param:
        case BaseKind::ParamPtr:
            return LinExpr::BExt(
                llvm::StringRef(p.obj).drop_front());  // *p0 → bext(p0)；*p0->pucBuf → bext(p0->pucBuf)
        case BaseKind::ParamField:
        case BaseKind::Global:
        case BaseKind::Local:
        case BaseKind::Str:
        case BaseKind::Heap:
            return LinExpr::Const(0);
        case BaseKind::Unknown:
        case BaseKind::Func:
            return llvm::None;
    }
    return llvm::None;
}

// 指针前面还有多少字节（调用点实参的 back，SPEC-0039 §2.2）：基址前面的字节数 + 偏移下界；不知道就不写
llvm::Optional<std::string> BackText(const PtrVal &p)
{
    llvm::Optional<LinExpr> base = BackOf(p);
    llvm::Optional<LinExpr> lo = p.off.Lower();
    if (!base || !lo || lo->IsUnknown()) {
        return llvm::None;
    }
    return (*base + *lo).Show();
}

void Analyzer::AddOpen(const std::string &reason, const std::string &at)
{
    bool seen = llvm::any_of(opens, [&](const summary::Open &o) { return o.reason == reason && o.at == at; });
    if (!seen) {  // 按（原因, 位置）去重：同一行的读和写只记一条
        summary::Open o;
        o.reason = reason;
        o.at = at;
        opens.push_back(o);
    }
}

void Analyzer::AddModText(const std::string &mod)
{
    if (recording) {
        mods.insert(mod);
    }
}

// 经 addr 写 size 字节（None = 不定 / 整块）：换成摘要的说法（SPEC-0045 §2.2）；局部、堆、字符串调用方看不到，不记
void Analyzer::AddMod(const PtrVal &addr, llvm::Optional<int64_t> size)
{
    llvm::Optional<int64_t> off;
    if (addr.off.IsConst()) {
        off = addr.off.value.Constant();
    }
    switch (addr.kind) {
        case BaseKind::Unknown:
            AddModText("*");
            return;
        case BaseKind::Param: {
            std::string p = addr.obj.substr(1);  // *p0 → p0
            AddModText(off && size ? p + "+" + std::to_string(*off) + ":" + std::to_string(*size) : p);
            return;
        }
        case BaseKind::ParamField: {
            std::string p = llvm::StringRef(addr.parentObj).drop_front().str();
            if (off && size) {
                AddModText(p + "+" + std::to_string(addr.parentOff + *off) + ":" + std::to_string(*size));
            } else if (addr.size.IsConst()) {
                AddModText(p + "+" + std::to_string(addr.parentOff) + ":" + std::to_string(addr.size.Constant()));
            } else {
                AddModText(p);
            }
            return;
        }
        case BaseKind::ParamPtr:
            AddModText(addr.obj);  // *p0->pucBuf
            return;
        case BaseKind::Global:
            if (const fir::Global *g = GlobalOf(addr.id)) {
                AddModText(GlobalSymbol(g->key, g->name));  // 带身份的符号（SPEC-0086）
            } else {
                AddModText(addr.obj);  // g:名字
            }
            return;
        case BaseKind::Local:
        case BaseKind::Heap:
        case BaseKind::Str:
        case BaseKind::Func:
            return;
    }
}

// 被调者有摘要：它的每条 mods 换成调用方的说法照做，只让写到的位置失效，并记进本函数的 mods（SPEC-0045 §2.3）
void Analyzer::ApplyMods(const fir::Inst &inst, const std::vector<std::string> &calleeMods, State &s)
{
    auto eraseBase = [&](const std::string &id, llvm::Optional<std::pair<int64_t, int64_t>> range) {
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            bool same = std::get<0>(it->first) == id;
            int64_t o = std::get<1>(it->first);
            int64_t sz = std::get<2>(it->first);
            bool overlap = !range || (o < range->first + range->second && range->first < o + sz);
            it = same && overlap ? s.cells.erase(it) : std::next(it);
        }
    };
    auto everything = [&]() {
        s.clobbered = true;
        s.called = true;
        s.cells.clear();
        AddModText("*");
    };
    for (const std::string &mod : calleeMods) {
        llvm::StringRef text = mod;
        if (text == "*") {
            everything();
            continue;
        }
        if (text == "g:*") {
            s.called = true;
            for (auto it = s.cells.begin(); it != s.cells.end();) {
                it = llvm::StringRef(std::get<0>(it->first)).startswith("G") ? s.cells.erase(it) : std::next(it);
            }
            AddModText("g:*");
            continue;
        }
        if (text.startswith("g:")) {
            // 按符号查；查不到（别的 TU 的 static、本模块没声明的全局、老格式）→ 记进 writtenAll，后面调用点上当"改过"，
            // 再按显示名兜底多失效一些（SPEC-0086）
            const std::vector<const fir::Global *> &exact = GlobalsBySym(mod);
            if (exact.empty()) {
                s.writtenAll.insert(mod);
            }
            for (const fir::Global *gp : !exact.empty() ? exact : GlobalsNamed(mod)) {  // 查表（SPEC-0080）
                const fir::Global &g = *gp;
                {
                    std::string id = "G" + g.key;
                    s.writtenAll.insert(id);
                    eraseBase(id, llvm::None);
                }
            }
            AddModText(mod);
            continue;
        }
        bool viaField = text.consume_front("*");  // *pK->f
        if (!text.consume_front("p")) {
            everything();  // 认不出的写法：保守
            continue;
        }
        unsigned k = 0;
        if (text.consumeInteger(10, k) || k >= inst.args.size()) {
            everything();
            continue;
        }
        Value v = Eval(inst.args[k], s);
        if (viaField) {
            // 指针字段所指的内存：实参是本函数的形参 pJ（偏移 0）时换成 *pJ->f；否则不处理（数据缓冲区，SPEC-0040 的取舍）
            if (v.isPtr && v.p.kind == BaseKind::Param && v.p.off.IsConst() && v.p.off.value.Constant() == 0) {
                AddModText("*" + v.p.obj.substr(1) + text.str());
            }
            continue;
        }
        if (!v.isPtr || v.p.kind == BaseKind::Unknown) {
            everything();
            continue;
        }
        if (v.p.kind == BaseKind::Func) {
            continue;
        }
        int64_t off = 0;
        int64_t size = 0;
        bool range = text.consume_front("+") && !text.consumeInteger(10, off) && text.consume_front(":") &&
                     !text.consumeInteger(10, size) && text.empty();
        if (range && v.p.off.IsConst()) {
            int64_t at = v.p.off.value.Constant() + off;
            s.written.insert(CellKey(v.p.id, at, size));
            eraseBase(v.p.id, std::make_pair(at, size));
            InvalidateFamily(s, v.p, at, size);  // 成员数组 / 外层（SPEC-0090、0091）
            AliasWritten(s, v.p.id);
            PtrVal q = v.p;
            q.off = IntVal::Exact(LinExpr::Const(at));
            AddMod(q, size);
        } else {
            MarkWritten(s, v.p.id);
            eraseBase(v.p.id, llvm::None);
            InvalidateFamily(s, v.p, llvm::None, 0);  // 成员数组 / 外层（SPEC-0090、0091）
            AddMod(v.p, llvm::None);
        }
    }
}

void Analyzer::Access(const char *kind, const PtrVal &addr, ExprId addrExpr, const IntVal &bytes, const fir::Loc &loc,
                      const State &s, const std::vector<LinExpr> &forced, bool checkBack, const SymAccess *sym)
{
    if (!recording) {
        return;
    }
    // 成员数组：外层对象的边界也要满足（off(f) + 下标 + 字节数 <= ext(pK)，F-02；SPEC-0022）——只查成员会把
    // "按大结构体访问小缓冲区"（LV7-03）证明成安全。外层只查正向：成员起点就是下界，比外层更严（SPEC-0039 §2.1）
    if (addr.hasParent) {
        PtrVal parent;
        parent.kind = BaseKind::Param;
        parent.id = addr.parentId;
        parent.obj = addr.parentObj;
        parent.hasSize = true;
        parent.size = addr.parentSize;
        parent.off = AddVals(IntVal::Exact(LinExpr::Const(addr.parentOff)), addr.off);
        Access(kind, parent, fir::kNone, bytes, loc, s, forced, false);
    }
    std::string at = SiteOf(m, loc);
    auto open = [&](const char *reason) { AddOpen(reason, at); };
    if (bytes.IsConst() && bytes.value.Constant() == 0) {
        return;  // 0 字节：不访问
    }
    if (addr.kind == BaseKind::Unknown || addr.kind == BaseKind::Func) {
        llvm::StringRef r = ConstructReason(addr.why);
        AddOpen(r.empty() ? "untracked_pointer" : r.str(), at);  // 不支持的构造给出的指针（SPEC-0060）
        return;
    }
    if (!addr.subs.empty()) {
        CheckSubs(kind, addr, bytes, at, s, forced);  // 成员数组、多维数组的一行（SPEC-0104）
    }
    if (checkBack) {
        BackAccess(kind, addr, addrExpr, at, s, forced, sym);  // 负偏移（SPEC-0039）
    }
    if (!addr.hasSize) {
        open("alloc_size_unknown");
        return;
    }
    // AssumeNoWrap（SPEC-0108 §2.2）：末端 = 偏移 + 字节数的符号式，同类项抵消（A-41），再用局部关系与区间消去局部符号
    llvm::Optional<std::pair<LinExpr, Tag>> symHi;
    llvm::Optional<std::pair<LinExpr, Tag>> symLo;
    if (options.assumeNoWrap && sym != nullptr) {
        symHi = Bound(sym->off + sym->bytes, true, s);
        symLo = Bound(sym->off + (sym->upto ? LinExpr::Const(1) : sym->bytes), false, s);
    }
    llvm::Optional<LinExpr> hi = addr.off.Upper();
    if (!hi && !symHi) {
        // 偏移的上界被 widening 去掉、地址就是一个指针局部（*p++）→ 指针步进（A-45、SPEC-0017 §2.5）
        bool stepping = addr.off.kind == IntVal::Kind::Range && addr.off.tag == Tag::Widen &&
                        addrExpr < f.exprs.size() && f.exprs[addrExpr].op == ExprOp::Local;
        AddOpen(OpenReason(addr.off, stepping ? "pointer_stepping" : "type_range_only"), at);
        return;
    }
    // 字节数（模型给出的可以是区间或符号，SPEC-0018 §2.2）：没有上界 → 账本；没有下界按 0
    llvm::Optional<LinExpr> bytesHi = bytes.Upper();
    if (!bytesHi && !symHi) {
        AddOpen(OpenReason(bytes, "type_range_only"), at);
        return;
    }
    llvm::Optional<LinExpr> bytesLo = bytes.Lower();
    llvm::Optional<LinExpr> lo = addr.off.Lower();
    RawAccess a;
    a.kind = kind;
    a.have = addr.size;
    a.obj = addr.obj;
    a.hi = hi && bytesHi ? *hi + *bytesHi : LinExpr::Unknown();
    a.lo = (lo ? *lo : LinExpr::Const(0)) + (bytesLo ? *bytesLo : LinExpr::Const(0));
    a.start = lo;
    Tag offTag = addr.off.kind == IntVal::Kind::Exact ? Tag::Exact : addr.off.tag;
    Tag bytesTag = bytes.kind == IntVal::Kind::Exact ? Tag::Exact : bytes.tag;
    a.tag = WorseTag(offTag, bytesTag);
    a.at = at;
    a.effDepth = addr.kind == BaseKind::Heap ? addr.effDepth : 0;  // SPEC-0113
    // 符号式给出的界更紧（或原来没有）就用它
    SymEnv env = Env(s);
    if (symHi && (a.hi.IsUnknown() || (symHi->first != a.hi && EntailsLe0(symHi->first - a.hi, s.guards, env)))) {
        a.hi = symHi->first;
        a.tag = symHi->second;
    }
    if (symLo && (a.lo.IsUnknown() || (symLo->first != a.lo && EntailsLe0(a.lo - symLo->first, s.guards, env)))) {
        a.lo = symLo->first;
    }
    if (a.hi.IsUnknown() || a.lo.IsUnknown()) {
        open("type_range_only");
        return;
    }
    Record(std::move(a), s, forced);
}

// 局部符号此刻的界（SPEC-0108 §2.2）：局部的区间；没有就用类型范围（无符号的下界 0 是确切的事实，标 exact）
llvm::Optional<std::pair<LinExpr, Tag>> Analyzer::AtomBound(llvm::StringRef name, bool upper, const State &s) const
{
    bool ptr = name.startswith("$o");
    unsigned id = 0;
    if ((!ptr && !name.startswith("$l")) || name.drop_front(2).getAsInteger(10, id)) {
        return llvm::None;
    }
    auto it = s.locals.find(id);
    if (it != s.locals.end() && it->second.isPtr != ptr) {
        return llvm::None;
    }
    IntVal v = it == s.locals.end() ? IntVal::Top("uninit") : (ptr ? it->second.p.off : it->second.i);
    llvm::Optional<LinExpr> b = upper ? v.Upper() : v.Lower();
    const fir::Type *t = ptr ? nullptr : LocalType(id);
    bool unsignedInt = t != nullptr && t->kind == fir::TypeKind::Int && !t->isSigned;
    if (b && !b->IsUnknown()) {
        Tag tag = v.kind == IntVal::Kind::Exact ? Tag::Exact : v.tag;
        if (!upper && unsignedInt && b->IsConst() && b->Constant() == 0) {
            tag = Tag::Exact;
        }
        return std::make_pair(*b, tag);
    }
    if (t == nullptr || t->kind != fir::TypeKind::Int || t->width == 0 || t->width > 62) {
        return llvm::None;
    }
    int64_t max = t->isSigned ? (int64_t(1) << (t->width - 1)) - 1 : (int64_t(1) << t->width) - 1;
    int64_t min = t->isSigned ? -(int64_t(1) << (t->width - 1)) : 0;
    return std::make_pair(LinExpr::Const(upper ? max : min), upper || t->isSigned ? Tag::Type : Tag::Exact);
}

llvm::Optional<std::pair<LinExpr, Tag>> Analyzer::Bound(const LinExpr &e, bool upper, const State &s)
{
    if (e.IsUnknown()) {
        return llvm::None;
    }
    // ① 用局部关系 R ≤ 0 消去局部符号：上界 e − k·R、下界 e + k·R（k > 0，使这个局部符号的系数变成 0；至多两轮）
    std::vector<std::pair<LinExpr, Tag>> cands{{e, Tag::Exact}};
    for (int round = 0; round < 2; ++round) {
        size_t n = cands.size();
        for (size_t i = 0; i < n && cands.size() < 32; ++i) {
            LinExpr d = cands[i].first;
            Tag tag = cands[i].second;
            for (const auto &term : d.Terms()) {
                const Atom &x = term.first;
                int64_t c = term.second;
                if (x.kind != Atom::Kind::Sym || !llvm::StringRef(x.name).startswith("$") ||
                    (upper ? c <= 0 : c >= 0)) {
                    continue;  // 系数另一侧的用区间
                }
                for (const LinExpr &r : s.rels) {
                    int64_t cr = r.Coeff(x);
                    int64_t k = upper ? c : -c;
                    if (cr <= 0 || k % cr != 0) {
                        continue;
                    }
                    k /= cr;
                    LinExpr next = upper ? d - r.Scale(k) : d + r.Scale(k);
                    if (!next.IsUnknown()) {
                        cands.push_back({next, WorseTag(tag, Tag::Check)});
                    }
                }
            }
        }
    }
    // ② 剩下的局部符号按区间换掉（系数为正取上界、为负取下界；求下界时反过来）；取最紧的一个
    SymEnv env = Env(s);
    llvm::Optional<std::pair<LinExpr, Tag>> best;
    for (const auto &cand : cands) {
        LinExpr out = cand.first;
        Tag tag = cand.second;
        bool ok = true;
        for (const auto &term : cand.first.Terms()) {
            const Atom &x = term.first;
            if (x.kind != Atom::Kind::Sym || !llvm::StringRef(x.name).startswith("$")) {
                continue;
            }
            llvm::Optional<std::pair<LinExpr, Tag>> b = AtomBound(x.name, (term.second > 0) == upper, s);
            if (!b) {
                ok = false;
                break;
            }
            out = out + (b->first - LinExpr::OfAtom(x)).Scale(term.second);
            tag = WorseTag(tag, b->second);
        }
        if (!ok || out.IsUnknown() || HasLocalSym(out)) {
            continue;
        }
        bool better = !best || (upper ? EntailsLe0(out - best->first, s.guards, env) && out != best->first
                                      : EntailsLe0(best->first - out, s.guards, env) && out != best->first);
        if (better) {
            best = std::make_pair(out, tag);
        }
    }
    return best;
}

// load / store 的符号偏移（同一基址身份才用，SPEC-0108 §2.2）
llvm::Optional<SymAccess> Analyzer::SymAt(ExprId addrExpr, const PtrVal &addr, const LinExpr &bytes, const State &s)
{
    if (!options.assumeNoWrap) {
        return llvm::None;
    }
    llvm::Optional<std::pair<std::string, LinExpr>> p = SymPtr(addrExpr, s);
    if (!p || p->first != addr.id) {
        return llvm::None;
    }
    return SymAccess{p->second, bytes};
}

// 内层窗口（SPEC-0104 §2.2，用户决定 D22）：访问还要落在每个子对象（成员数组、多维数组的一行）里。证明不了 → 账本
// subobject_overflow（SUSPECT）；越没越出整个对象由外层的检查照常判。窗口的偏移没有界而外层有 → 也记（可靠）；
// 都没有 → 外层自己记账，不重复记。上界证明不了、式子里有接口符号（下标是形参…）→ 记内层 Req（dir "sub"）往调用方传（SPEC-0104 §2.3 3a）
void Analyzer::CheckSubs(const char *kind, const PtrVal &addr, const IntVal &bytes, const std::string &at,
                         const State &s, const std::vector<LinExpr> &forced)
{
    llvm::Optional<LinExpr> bytesHi = bytes.Upper();
    if (!bytesHi || bytesHi->IsUnknown()) {
        return;  // 字节数没有上界：外层记账
    }
    llvm::Optional<LinExpr> outerHi = addr.off.Upper();
    llvm::Optional<LinExpr> outerLo = addr.off.Lower();
    bool outerHasHi = outerHi && !outerHi->IsUnknown();
    bool outerHasLo = outerLo && !outerLo->IsUnknown();
    std::vector<RawAccess> up;
    for (const SubWindow &w : addr.subs) {
        bool bad = false;
        if (!w.tail) {
            llvm::Optional<LinExpr> hi = w.off.Upper();
            if (!hi || hi->IsUnknown()) {
                bad = outerHasHi;
            } else {
                LinExpr need = *hi + *bytesHi - LinExpr::Const(w.size);
                bad = need.IsUnknown() || !EntailsLe0(need, s.guards, Env(s));
                std::set<std::string> syms;
                if (bad && !need.IsUnknown()) {
                    CollectSyms(need, syms);
                }
                llvm::Optional<LinExpr> wlo = w.off.Lower();
                llvm::Optional<LinExpr> bytesLo = bytes.Lower();
                LinExpr lo = (wlo ? *wlo : LinExpr::Const(0)) + (bytesLo ? *bytesLo : LinExpr::Const(0));
                // Subobject=label：本函数里就定了的也记内层 Req，由入口 / 调用点按常规判（SPEC-0105）
                bool asReq = !syms.empty() || (bad && options.subobject == SubMode::Label);
                if (asReq && !need.IsUnknown() && !lo.IsUnknown()) {
                    // 结论取决于调用方给的值：记内层 Req，调用点再判（违反只判 SUSPECT，apply::Classify）
                    RawAccess a;
                    a.kind = kind;
                    a.dir = "sub";
                    a.have = LinExpr::Const(w.size);
                    a.obj = addr.obj;
                    a.hi = *hi + *bytesHi;
                    a.lo = lo;
                    a.start = wlo;
                    Tag offTag = w.off.kind == IntVal::Kind::Exact ? Tag::Exact : w.off.tag;
                    a.tag = WorseTag(offTag, bytes.kind == IntVal::Kind::Exact ? Tag::Exact : bytes.tag);
                    a.at = at;
                    up.push_back(std::move(a));
                    bad = false;
                }
            }
        }
        if (!bad && !w.head) {
            llvm::Optional<LinExpr> lo = w.off.Lower();
            if (!lo || lo->IsUnknown()) {
                bad = outerHasLo;
            } else {
                LinExpr under = LinExpr::Const(0) - *lo;  // 越到子对象前面的字节数
                bad = !EntailsLe0(under, s.guards, Env(s));
                std::set<std::string> syms;
                if (bad) {
                    CollectSyms(under, syms);
                }
                // 负偏移的内层 Req（dir subback，SPEC-0107 §2.2 第 3 步）：子对象前面没有字节可越（have 0），越过的字节数 −lo
                if (!syms.empty() || (bad && options.subobject == SubMode::Label)) {
                    llvm::Optional<LinExpr> hi = w.off.Upper();
                    RawAccess b;
                    b.kind = kind;
                    b.dir = "subback";
                    b.have = LinExpr::Const(0);
                    b.obj = addr.obj;
                    b.hi = under;
                    b.lo = hi && !hi->IsUnknown() ? LinExpr::Const(0) - *hi : LinExpr::Const(0);
                    b.tag = w.off.kind == IntVal::Kind::Exact ? Tag::Exact : w.off.tag;
                    b.at = at;
                    up.push_back(std::move(b));
                    bad = false;
                }
            }
        }
        if (bad) {
            if (trace != nullptr) {
                trace->push_back("subobject " + at + " " + kind + " off " + w.off.Show() + " bytes " + bytes.Show() +
                                 " of " + std::to_string(w.size));
            }
            AddOpen("subobject_overflow", at);
            return;
        }
    }
    for (RawAccess &a : up) {
        Record(std::move(a), s, forced);
    }
}

// 负偏移（06- §3.1，SPEC-0039 §2.1）：偏移下界不能证明 ≥ 0 时，记一条反向 Req：越到基址前面的字节数 −lo ≤ bext(基址)
void Analyzer::BackAccess(const char *kind, const PtrVal &addr, ExprId addrExpr, const std::string &at, const State &s,
                          const std::vector<LinExpr> &forced, const SymAccess *sym)
{
    llvm::Optional<LinExpr> have = BackOf(addr);
    if (!have) {
        return;
    }
    llvm::Optional<LinExpr> lo = addr.off.Lower();
    if ((!lo || lo->IsUnknown()) && options.assumeNoWrap && sym != nullptr) {
        // AssumeNoWrap：偏移的下界也用符号式求（SPEC-0108 §2.2）
        llvm::Optional<std::pair<LinExpr, Tag>> b = Bound(sym->off, false, s);
        if (b) {
            lo = b->first;
        }
    }
    if (!lo || lo->IsUnknown()) {
        bool stepping = addr.off.kind == IntVal::Kind::Range && addr.off.tag == Tag::Widen &&
                        addrExpr < f.exprs.size() && f.exprs[addrExpr].op == ExprOp::Local;
        AddOpen(OpenReason(addr.off, stepping ? "pointer_stepping" : "type_range_only"), at);
        return;
    }
    LinExpr under = LinExpr::Const(0) - *lo;
    if (EntailsLe0(under, s.guards, Env(s))) {
        return;  // 偏移下界 ≥ 0（常数、无符号类型、守卫）：没有负偏移，不记
    }
    llvm::Optional<LinExpr> hi = addr.off.Upper();
    RawAccess b;
    b.kind = kind;
    b.dir = "back";
    b.have = *have;
    b.obj = addr.obj;
    b.hi = under;
    b.lo = hi && !hi->IsUnknown() ? LinExpr::Const(0) - *hi : LinExpr::Const(0);
    b.tag = addr.off.kind == IntVal::Kind::Exact ? Tag::Exact : addr.off.tag;
    b.at = at;
    Record(std::move(b), s, forced);
}

// 挑守卫、在本函数里证明安全，否则记成 Req（正向与反向共用）
void Analyzer::Record(RawAccess a, const State &s, const std::vector<LinExpr> &forced)
{
    std::set<std::string> syms;  // 与 have / end / start 有公共符号的守卫（SPEC-0016 §2.2、SPEC-0018 §2.2）
    CollectSyms(a.have, syms);
    CollectSyms(a.lo, syms);
    CollectSyms(a.hi, syms);
    if (a.start) {
        CollectSyms(*a.start, syms);
    }
    // 全部守卫都带上（SPEC-0096）：只关于别的形参的路径条件（if (c) …）也要带，调用方传的值让它为假时访问走不到。
    // 守卫只含接口符号（A-14），带上是可靠的；与 have / end 有公共符号的排在前面
    std::vector<LinExpr> chosen;
    std::vector<LinExpr> others;
    for (const LinExpr &g : s.guards) {
        std::set<std::string> gs;
        CollectSyms(g, gs);
        bool related = llvm::any_of(gs, [&](const std::string &x) { return syms.count(x) != 0; });
        (related ? chosen : others).push_back(g);
    }
    chosen.insert(chosen.end(), others.begin(), others.end());
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
            trace->push_back("safe " + a.at + " " + a.kind + (a.dir.empty() ? "" : " " + a.dir) + " end " +
                             a.hi.Show() + " <= have " + a.have.Show());
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
            llvm::Optional<SymAccess> sym = SymAt(inst.addr, addr, LinExpr::Const(inst.size), s);  // SPEC-0108
            Access("R", addr, inst.addr, IntVal::Exact(LinExpr::Const(inst.size)), inst.loc, s, {}, true,
                   sym ? &*sym : nullptr);
            NoteAssume(s, RootOf(addr.id));  // CharAlias=verify（SPEC-0106）
            SetLocal(s, inst.dst, LoadValue(addr, inst.size, inst.dst, s));
            return;
        }
        case fir::InstOp::Store: {
            PtrVal addr = EvalPtr(inst.addr, s);
            llvm::Optional<SymAccess> sym = SymAt(inst.addr, addr, LinExpr::Const(inst.size), s);  // SPEC-0108
            Access("W", addr, inst.addr, IntVal::Exact(LinExpr::Const(inst.size)), inst.loc, s, {}, true,
                   sym ? &*sym : nullptr);
            AddMod(addr, inst.size);  // SPEC-0045
            if (addr.kind == BaseKind::Unknown) {
                s.cells.clear();  // A-12：经未知基址的写
                s.clobbered = true;
                return;
            }
            if (addr.off.IsConst()) {
                s.written.insert(CellKey(addr.id, addr.off.value.Constant(), inst.size));
                AliasWritten(s, addr.id);  // 同类型的形参可能就是这个对象（SPEC-0040 §2.2）
            } else {
                MarkWritten(s, addr.id);
            }
            // 失效同一基址上的格：常数偏移时只失效与 [off, off + size) 重叠的（SPEC-0026），再强更新；偏移不定时整个基址
            bool exact = addr.off.IsConst();
            int64_t off = exact ? addr.off.value.Constant() : 0;
            for (auto it = s.cells.begin(); it != s.cells.end();) {
                bool same = std::get<0>(it->first) == addr.id;
                int64_t o = std::get<1>(it->first);
                int64_t sz = std::get<2>(it->first);
                bool overlap = !exact || (o < off + inst.size && off < o + sz);
                it = same && overlap ? s.cells.erase(it) : std::next(it);
            }
            InvalidateFamily(s, addr, exact ? llvm::Optional<int64_t>(off) : llvm::None, inst.size);  // SPEC-0090
            if (addr.off.IsConst() && inst.val != fir::kNone) {
                s.cells[CellKey(addr.id, addr.off.value.Constant(), inst.size)] = Eval(inst.val, s);
            }
            return;
        }
        case fir::InstOp::Call: {
            if (inst.calleeExpr != fir::kNone) {
                std::vector<std::string> targets = TargetsOf(inst.calleeExpr);
                if (targets.size() == 1) {
                    fir::Inst direct = inst;  // 只可能是这一个函数：当直接调用（SPEC-0058 §2.2）
                    direct.callee = targets[0];
                    direct.calleeExpr = fir::kNone;
                    resolving = true;
                    Step(direct, s);
                    resolving = false;
                    return;
                }
                if (targets.size() > 1 && AltCall(inst, targets, s)) {
                    return;
                }
            }
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
                result = HeapResult(inst, models.Alloc(name)->size, 0, s);
            }
            const summary::Summary *calleeSummary = nullptr;  // 本 TU / solve 时已算出摘要的被调者
            if (kind == models::Kind::None && inst.calleeExpr == fir::kNone && callees != nullptr) {
                // 代入它的 ret_new（SPEC-0024 §2.2）
                auto callee = callees->find(inst.callee);  // 按函数键（SPEC-0027）；找不到再按名字
                if (callee == callees->end()) {
                    callee = callees->find(name.str());
                }
                if (callee != callees->end()) {
                    calleeSummary = &callee->second;
                    for (const summary::Eff &e : callee->second.effs) {
                        if (e.kind == "ret_new" && e.size) {
                            result = HeapResult(inst, *e.size, e.effDepth.getValueOr(1), s);
                            break;
                        }
                        if (e.kind == "ret_int" && !result.isPtr) {
                            result.i = CondResult(inst, callee->second.effs, s, &callee->second);  // SPEC-0025 §2.3
                            break;
                        }
                        if (e.kind == "ret_ptr" && result.isPtr && e.target && (e.value || e.range)) {
                            result = RetPtrResult(inst, e, callee->second, s);  // 调用前的状态（SPEC-0070 §2.2）
                            break;
                        }
                    }
                }
            }
            // 写效应 field：按调用前的状态算新值（SPEC-0073 §2.2），失效之后再写进格
            std::vector<std::pair<CellKey, Value>> fieldWrites;
            std::vector<std::pair<int64_t, std::pair<CellKey, Value>>> caseWrites;
            if (calleeSummary != nullptr) {
                fieldWrites = FieldWrites(inst, *calleeSummary, s, &caseWrites);
            }
            if (calleeSummary != nullptr) {
                ApplyMods(inst, calleeSummary->mods, s);  // 只让被调者可能写的位置失效（mod-ref，SPEC-0045）
                for (const auto &w : fieldWrites) {
                    s.cells[w.first] = w.second;
                }
                // 只在某个返回值下成立的写效应：挂到调用结果的情形上，筛出这个返回值时写回（SPEC-0079）
                for (const auto &cw : caseWrites) {
                    auto c = std::find_if(result.i.cases.begin(), result.i.cases.end(),
                                          [&](const IntCase &x) { return x.value == cw.first; });
                    if (result.isPtr || c == result.i.cases.end()) {
                        continue;
                    }
                    const CellKey &key = cw.second.first;
                    CaseWrite w;
                    w.id = std::get<0>(key);
                    w.off = std::get<1>(key);
                    w.size = std::get<2>(key);
                    w.value = cw.second.second.i.value;
                    w.atCall = CellText(s, key);
                    c->writes.push_back(w);
                }
                // 出参写回的新对象：实参所指的格强更新成 heap@调用点（SPEC-0053）
                for (const summary::Eff &e : calleeSummary->effs) {
                    llvm::StringRef target = e.target ? llvm::StringRef(*e.target) : llvm::StringRef();
                    // pK 或 pK->f（字段的偏移从被调者的 fields 表取，SPEC-0061 §2.2）
                    int64_t fieldOff = 0;
                    if (target.contains("->")) {
                        auto info = calleeSummary->fields ? calleeSummary->fields->find(target.str())
                                                          : std::map<std::string, summary::FieldInfo>::const_iterator();
                        if (!calleeSummary->fields || info == calleeSummary->fields->end()) {
                            continue;
                        }
                        fieldOff = info->second.off;
                        target = target.split("->").first;
                    }
                    unsigned k = 0;
                    if (e.kind != "out_new" || !e.size || !target.consume_front("p") || target.getAsInteger(10, k) ||
                        k >= inst.args.size()) {
                        continue;
                    }
                    Value arg = Eval(inst.args[k], s);
                    if (!arg.isPtr || arg.p.kind == BaseKind::Unknown || arg.p.kind == BaseKind::Func ||
                        !arg.p.off.IsConst()) {
                        continue;
                    }
                    s.cells[CellKey(arg.p.id, arg.p.off.value.Constant() + fieldOff, m.target.ptr)] =
                        HeapResult(inst, *e.size, e.effDepth.getValueOr(1), s);
                }
            } else if (kind != models::Kind::Pure && kind != models::Kind::Free) {
                if (name != "__oobsum_copy" || !CopyStruct(inst, s)) {
                    Clobber(inst, kind, name, s);
                }
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
            DropRels(s, inst.dst);  // SPEC-0108
            s.locals[inst.dst] = v;
            return;
        }
        case fir::InstOp::Ret:
            RecordReturn(inst, s);
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
        return a.have == b.have && a.obj == b.obj && a.guard == b.guard && a.dir == b.dir;  // 守卫 / 方向不同的不合并
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
    r.dir = a.dir;
    r.effDepth = a.effDepth;  // SPEC-0113
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
    const std::vector<fir::Inst> &insts = f.blocks[block].insts;
    for (const fir::Inst &inst : insts) {
        Step(inst, s);
    }
    // 可重算的定义（SPEC-0075）：Y = E，Y 之后没再赋值，E 里的局部不是 Y、在这之后也没再赋值 → 出口处 Y 就是 E 的值
    std::map<uint32_t, size_t> lastDef;
    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].dst != fir::kNone) {
            lastDef[insts[i].dst] = i;
        }
    }
    std::vector<std::pair<uint32_t, ExprId>> redefs;
    for (size_t i = 0; i < insts.size(); ++i) {
        const fir::Inst &inst = insts[i];
        if (inst.op != fir::InstOp::Set || inst.dst == fir::kNone || lastDef[inst.dst] != i) {
            continue;
        }
        std::set<uint32_t> used;
        CollectLocals(inst.val, used);
        bool ok = !used.empty() && used.count(inst.dst) == 0;
        for (uint32_t x : used) {
            auto d = lastDef.find(x);
            ok = ok && (d == lastDef.end() || d->second < i);
        }
        if (ok) {
            redefs.push_back({inst.dst, inst.val});
        }
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
            // 收窄了 E 里的局部（或边上新加了守卫：确切值收窄不了时只加守卫）、Y 自己没被这次收窄改过 →
            // Y 按收窄后的值重算（while (n--) 的 n = t − 1，SPEC-0075）
            bool guarded = taken.guards.size() != s.guards.size();
            for (const auto &r : redefs) {
                std::set<uint32_t> used;
                CollectLocals(r.second, used);
                bool narrowed = guarded || llvm::any_of(used, [&](uint32_t x) {
                                    auto a = s.locals.find(x);
                                    auto b = taken.locals.find(x);
                                    return a != s.locals.end() && b != taken.locals.end() && !(a->second == b->second);
                                });
                auto before = s.locals.find(r.first);
                auto now = taken.locals.find(r.first);
                bool self = before != s.locals.end() && now != taken.locals.end() && before->second == now->second;
                if (narrowed && self) {
                    SetLocal(taken, r.first, Eval(r.second, taken));
                }
            }
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
    // lower 没降级函数体的（块数超限、建不出 CFG）：不分析，引擎在声明处报 SUSPECT（A-09，SPEC-0037）
    for (const char *flag : {"cfg_too_large", "no_cfg"}) {
        if (llvm::is_contained(f.flags, flag)) {
            facts.status = std::string("skipped:") + flag;
            return facts;
        }
    }
    ResolveFnLocals();  // SPEC-0058
    // DFS：后序；指向栈上块的边是回边，它的目标是循环头。后继倒着走，逆后序里后继 0（条件为真 / 循环体）排在前面：
    // 先把循环算稳再传给出口，Dump=state 也按源码顺序
    std::vector<int> mark(n, 0);  // 0 未访问、1 在栈上、2 完成
    std::vector<size_t> post;
    heads.assign(n, false);
    std::vector<std::pair<size_t, size_t>> backEdges;  // (起点, 循环头)
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
                backEdges.emplace_back(b, it->second);
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

    // 每个循环头：自然循环体（回边起点不经过头能反向走到的块）里被赋值的局部，只对它们 widen（SPEC-0046）
    std::vector<std::vector<size_t>> preds(n);
    for (size_t b = 0; b < n; ++b) {
        for (const fir::Edge &e : f.blocks[b].succ) {
            auto it = index.find(e.to);
            if (it != index.end()) {
                preds[it->second].push_back(b);
            }
        }
    }
    std::vector<std::set<uint32_t>> loopVars(n);
    std::vector<std::set<size_t>> loopBlocks(n);  // 每个循环头的自然循环体（含头，SPEC-0085）
    for (const auto &be : backEdges) {
        std::vector<bool> inBody(n, false);
        inBody[be.second] = true;
        std::vector<size_t> stack{be.first};
        while (!stack.empty()) {
            size_t x = stack.back();
            stack.pop_back();
            if (inBody[x]) {
                continue;
            }
            inBody[x] = true;
            for (size_t p : preds[x]) {
                stack.push_back(p);
            }
        }
        for (size_t x = 0; x < n; ++x) {
            if (!inBody[x]) {
                continue;
            }
            loopBlocks[be.second].insert(x);
            for (const fir::Inst &inst : f.blocks[x].insts) {
                if (inst.dst != fir::kNone) {
                    loopVars[be.second].insert(inst.dst);
                }
            }
        }
    }

    // 循环上 x != c / x == c 的阈值候选（SPEC-0082）：自然循环体里每一块（含头，SPEC-0085）出边上的比较；x 是本循环里被赋值的局部
    // （隔着类型转换也行），那一块的指令不赋 x、也不赋 c 里用到的局部
    struct NeBound {
        uint32_t local;
        ExprId side;   // 比较里 x 那一边
        ExprId other;  // c
    };
    std::vector<std::vector<NeBound>> neBounds(n);
    for (size_t t = 0; t < n; ++t) {
        if (!heads[t]) {
            continue;
        }
        for (size_t blk : loopBlocks[t]) {
            std::set<uint32_t> defined;
            for (const fir::Inst &inst : f.blocks[blk].insts) {
                if (inst.dst != fir::kNone) {
                    defined.insert(inst.dst);
                }
            }
            for (const fir::Edge &e : f.blocks[blk].succ) {
                ExprId c = e.assume;
                while (c < f.exprs.size() && f.exprs[c].op == ExprOp::LNot) {
                    c = f.exprs[c].a;
                }
                if (c >= f.exprs.size() || f.exprs[c].op != ExprOp::Cmp) {
                    continue;
                }
                fir::CmpOp op = static_cast<fir::CmpOp>(f.exprs[c].sub);
                if (op != fir::CmpOp::Ne && op != fir::CmpOp::Eq) {
                    continue;
                }
                for (int k = 0; k < 2; ++k) {
                    ExprId side = k == 0 ? f.exprs[c].a : f.exprs[c].b;
                    ExprId other = k == 0 ? f.exprs[c].b : f.exprs[c].a;
                    ExprId cur = side;
                    while (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Cast) {
                        cur = f.exprs[cur].a;
                    }
                    if (other == fir::kNone || cur >= f.exprs.size() || f.exprs[cur].op != ExprOp::Local) {
                        continue;
                    }
                    uint32_t x = static_cast<uint32_t>(f.exprs[cur].value);
                    std::set<uint32_t> used;
                    CollectLocals(other, used);
                    if (loopVars[t].count(x) != 0 && defined.count(x) == 0 &&
                        llvm::none_of(used, [&](uint32_t u) { return defined.count(u) != 0; })) {
                        neBounds[t].push_back({x, side, other});
                    }
                }
            }
        }
    }
    // widen 去掉了 x 的上（下）界：旧值与这条入边的贡献里 c 是同一个确切值、x 的界在各自的状态下都能证明 ≤ c（≥ c）→ 界取 c。
    // 只是候选的不变式：下一圈回来证明不了就照常 widen 掉（SPEC-0082）
    auto neThreshold = [&](size_t t, const State &old, const State &o, State &next) {
        for (const NeBound &nb : neBounds[t]) {
            auto it = next.locals.find(nb.local);
            auto lo = old.locals.find(nb.local);
            auto ln = o.locals.find(nb.local);
            if (it == next.locals.end() || lo == old.locals.end() || ln == o.locals.end()) {
                continue;
            }
            const Value &xo = lo->second;
            const Value &xn = ln->second;
            if (!(Eval(nb.side, old) == xo) || !(Eval(nb.side, o) == xn)) {
                continue;  // 类型转换改了值
            }
            Value co = Eval(nb.other, old);
            Value cn = Eval(nb.other, o);
            Value &v = it->second;
            if (!(co == cn) || co.isPtr != v.isPtr || xo.isPtr != v.isPtr || xn.isPtr != v.isPtr) {
                continue;
            }
            auto sameBase = [&](const Value &a) { return a.p.kind == v.p.kind && a.p.id == v.p.id; };
            if (v.isPtr && (v.p.kind == BaseKind::Unknown || !sameBase(xo) || !sameBase(xn) || !sameBase(co))) {
                continue;
            }
            const IntVal &c = v.isPtr ? co.p.off : co.i;
            const IntVal &io = v.isPtr ? xo.p.off : xo.i;
            const IntVal &in2 = v.isPtr ? xn.p.off : xn.i;
            IntVal &w = v.isPtr ? v.p.off : v.i;
            if (c.kind != IntVal::Kind::Exact || c.value.IsUnknown() || w.kind != IntVal::Kind::Range) {
                continue;
            }
            SymEnv envOld = Env(old);
            SymEnv envNew = Env(o);
            Comparer cmpOld{&envOld, &old.guards};
            Comparer cmpNew{&envNew, &o.guards};
            llvm::Optional<LinExpr> ho = io.Upper();
            llvm::Optional<LinExpr> hn = in2.Upper();
            if (!w.hasHi && ho && hn && ProvablyLe(*ho, c.value, &cmpOld) && ProvablyLe(*hn, c.value, &cmpNew)) {
                w.hasHi = true;
                w.hi = c.value;
            }
            llvm::Optional<LinExpr> lo2 = io.Lower();
            llvm::Optional<LinExpr> ln2 = in2.Lower();
            if (!w.hasLo && lo2 && ln2 && ProvablyLe(c.value, *lo2, &cmpOld) && ProvablyLe(c.value, *ln2, &cmpNew)) {
                w.hasLo = true;
                w.lo = c.value;
            }
        }
    };

    // 汇合时比较符号界的环境：类型范围 + 两边都成立的守卫（SPEC-0023）
    auto envOf = [&](const std::vector<LinExpr> &guards) {
        State t;
        t.reached = true;
        t.guards = guards;
        return Env(t);
    };
    // 形参局部的入口符号（SPEC-0043）
    auto selfOf = [&](uint32_t id) {
        const fir::Local *l = f.FindLocal(id);
        return l != nullptr && l->param >= 0 ? "p" + std::to_string(l->param) : std::string();
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
            State next = Join(in[t], o, envOf, selfOf);
            if (heads[t] && in[t].reached) {
                next = WidenState(in[t], next, &loopVars[t]);
                neThreshold(t, in[t], o, next);  // SPEC-0082
                // 无符号局部缺的界补成类型范围：真实值本来就在里面，补上后循环条件才收得住（从 1 开始的循环，SPEC-0050）
                for (uint32_t id : loopVars[t]) {
                    auto it = next.locals.find(id);
                    const fir::Type *type = LocalType(id);
                    if (it == next.locals.end() || it->second.isPtr || it->second.i.kind != IntVal::Kind::Range ||
                        type == nullptr || type->kind != fir::TypeKind::Int || type->isSigned || type->width >= 63) {
                        continue;
                    }
                    IntVal &v = it->second.i;
                    if (!v.hasHi) {
                        v.hasHi = true;
                        v.hi = LinExpr::Const((int64_t(1) << type->width) - 1);
                    }
                    if (!v.hasLo) {
                        v.hasLo = true;
                        v.lo = LinExpr::Const(0);
                    }
                }
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
                s = Join(s, contrib[e], envOf, selfOf);
            }
            in[b] = s;
            std::vector<State> out = s.reached ? FlowOut(b, s) : std::vector<State>(f.blocks[b].succ.size());
            for (size_t e : outgoing[b]) {
                contrib[e] = out[edges[e].succ];
            }
        }
    }

    // 收窄之后仍停在类型最大值（及以上）、来源还是 widen 的上界：补上的界没被循环条件收住，恢复成"没有上界"（记账方式同原来，SPEC-0050）
    for (size_t b = 0; b < n; ++b) {
        for (auto &entry : in[b].locals) {
            const fir::Type *type = LocalType(entry.first);
            IntVal &v = entry.second.i;
            if (entry.second.isPtr || v.kind != IntVal::Kind::Range || !v.hasHi || v.tag != Tag::Widen ||
                type == nullptr || type->kind != fir::TypeKind::Int || type->isSigned || type->width >= 63) {
                continue;
            }
            Limit hi = Env(in[b]).RangeOf(v.hi, false);
            if (hi.known && hi.value >= (int64_t(1) << type->width) - 1) {
                v.hasHi = false;
                v.hi = LinExpr();
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
            // 经 char / void 写过外部内存（CharAlias，SPEC-0103）的标记放在行尾
            std::string roots;  // CharAlias=verify：经 char 写过的根（SPEC-0106）
            for (const std::string &r : in[b].charRoots) {
                roots += (roots.empty() ? " char-written:" : ",") + r;
            }
            trace->push_back(ShowState(f.blocks[b], in[b]) + (heads[b] ? " (head)" : "") +
                             (in[b].externAll ? " extern-written" : "") + roots);
        }
        State s = in[b];
        for (const fir::Inst &inst : f.blocks[b].insts) {
            Step(inst, s);
        }
        // 走到函数末尾自然返回（没有 ret 指令）：出参与写效应也要按这条路径记（SPEC-0073）
        const fir::Block &block = f.blocks[b];
        bool toExit = llvm::any_of(block.succ, [&](const fir::Edge &e) { return e.to == f.exit; });
        bool hasRet = llvm::any_of(block.insts, [](const fir::Inst &i) { return i.op == fir::InstOp::Ret; });
        if (toExit && !hasRet && block.id != f.exit && s.reached) {
            fir::Inst implicit;
            implicit.op = fir::InstOp::Ret;  // 没有返回值
            RecordReturn(implicit, s);
        }
    }
    recording = false;
    facts.reqs = Merge();
    for (size_t i = 0; i < safe.size(); ++i) {
        facts.safe.push_back(ToReq(safe[i], i + 1));
    }
    facts.calls = calls;
    facts.effs = Effects();  // 先算效果：出参字段要写进 fields 表（SPEC-0061）
    facts.fields = fields;
    facts.opens = opens;
    facts.mods = mods;        // SPEC-0045
    facts.assumes = assumes;  // SPEC-0106
    return facts;
}

// 调用点记录（D-25，SPEC-0015 §2.1）：整数实参写表达式（区间写成局部名 + locals）；指针实参写 {obj, have, alias?}；
// 纯函数模型的指针实参为 null；不知道的值写成不是接口符号的名字（_unknown<k>），以后在调用点只会走向 entry_unproven
// 结构体里的整数 / 指针字段（递归展开嵌套的结构体，数组成员不展开）：(偏移, 大小, 是不是指针)
struct CopyLeaf {
    int64_t off = 0;
    int64_t size = 0;
    bool ptr = false;
};

void ScalarLeaves(const fir::Module &m, fir::TypeId t, int64_t base, int depth, std::vector<CopyLeaf> &out)
{
    if (t >= m.types.size() || depth > 4) {
        return;
    }
    const fir::Type &ty = m.types[t];
    int64_t size = 0;
    if (ty.kind == fir::TypeKind::Int && TypeSize(m, t, size)) {
        out.push_back({base, size, false});
    } else if (ty.kind == fir::TypeKind::Ptr) {
        out.push_back({base, m.target.ptr, true});
    } else if (ty.kind == fir::TypeKind::Rec) {
        if (const fir::Record *r = m.FindRecord(ty.record)) {
            for (const fir::Field &field : r->fields) {
                ScalarLeaves(m, field.type, base + field.off, depth + 1, out);
            }
        }
    }
}

void Analyzer::InvalidateFamily(State &s, const PtrVal &addr, llvm::Optional<int64_t> off, int64_t size) const
{
    auto overlap = [](int64_t a, int64_t alen, int64_t b, int64_t blen) { return a < b + blen && b < a + alen; };
    // 写成员数组：外层对象上与它重叠的格（偏移不定时整个成员的范围；成员大小不知道时外层整块）
    if (addr.hasParent) {
        bool all = !off && !addr.size.IsConst();
        int64_t lo = addr.parentOff + (off ? *off : 0);
        int64_t len = off ? size : (addr.size.IsConst() ? addr.size.Constant() : 0);
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            bool hit = std::get<0>(it->first) == addr.parentId &&
                       (all || overlap(std::get<1>(it->first), std::get<2>(it->first), lo, len));
            it = hit ? s.cells.erase(it) : std::next(it);
        }
    }
    // 写外层形参对象：与写的范围重叠的数组成员（找不到记录 / 成员、或偏移不定时全部）
    if (addr.kind == BaseKind::Param && addr.id.size() >= 2 && addr.id[0] == 'P' &&
        std::isdigit(static_cast<unsigned char>(addr.id[1]))) {
        std::string prefix = addr.id + ".";
        int k = static_cast<int>(std::strtol(addr.id.c_str() + 1, nullptr, 10));
        llvm::Optional<uint32_t> rec = PointeeRecord(k);
        const fir::Record *r = rec ? m.FindRecord(*rec) : nullptr;
        for (auto it = s.cells.begin(); it != s.cells.end();) {
            llvm::StringRef base = std::get<0>(it->first);
            bool hit = false;
            if (base.startswith(prefix)) {
                hit = true;
                llvm::StringRef member = base.drop_front(prefix.size());
                for (const fir::Field &field : off &&r != nullptr ? r->fields : std::vector<fir::Field>()) {
                    int64_t fs = 0;
                    if (field.name == member && TypeSize(m, field.type, fs)) {
                        hit = overlap(field.off, fs, *off, size);
                    }
                }
            }
            it = hit ? s.cells.erase(it) : std::next(it);
        }
    }
}

bool Analyzer::CopyStruct(const fir::Inst &inst, State &s)
{
    if (inst.args.size() < 3) {
        return false;
    }
    IntVal n = EvalInt(inst.args[2], s);
    if (!n.IsConst() || n.value.Constant() <= 0) {
        return false;
    }
    int64_t size = n.value.Constant();
    // 结构体类型：&局部 / &全局 → 它的类型；指针局部 → 所指类型；须是记录、大小等于第三个实参
    auto recordOf = [&](ExprId e) {
        while (e < f.exprs.size() && f.exprs[e].op == ExprOp::Cast) {
            e = f.exprs[e].a;
        }
        fir::TypeId t = fir::kNone;
        if (e < f.exprs.size()) {
            const fir::Expr &x = f.exprs[e];
            if (x.op == ExprOp::LAddr) {
                const fir::Local *l = f.FindLocal(static_cast<uint32_t>(x.value));
                t = l != nullptr ? l->type : fir::kNone;
            } else if (x.op == ExprOp::GAddr) {
                const fir::Global *g = GlobalOf("G" + x.text);
                t = g != nullptr ? g->type : fir::kNone;
            } else if (x.op == ExprOp::Local) {
                const fir::Type *lt = LocalType(static_cast<uint32_t>(x.value));
                t = lt != nullptr && lt->kind == fir::TypeKind::Ptr ? lt->to : fir::kNone;
            }
        }
        int64_t ts = 0;
        return t < m.types.size() && m.types[t].kind == fir::TypeKind::Rec && TypeSize(m, t, ts) && ts == size
                   ? t
                   : fir::kNone;
    };
    fir::TypeId rec = recordOf(inst.args[0]);
    if (rec == fir::kNone) {
        rec = recordOf(inst.args[1]);
    }
    PtrVal dst = EvalPtr(inst.args[0], s);
    PtrVal src = EvalPtr(inst.args[1], s);
    auto usable = [](const PtrVal &p) {
        return p.kind != BaseKind::Unknown && p.kind != BaseKind::Func && p.off.IsConst();
    };
    if (rec == fir::kNone || !usable(dst) || !usable(src)) {
        return false;
    }
    int64_t dOff = dst.off.value.Constant();
    int64_t sOff = src.off.value.Constant();
    std::vector<CopyLeaf> leaves;
    ScalarLeaves(m, rec, 0, 0, leaves);
    // 先读源：整数照常读（格、形参所指的入口符号、全局入口值）；指针只取源的格、或形参所指的（类型从字段来），其余不拷
    std::vector<std::pair<CopyLeaf, Value>> values;
    for (const CopyLeaf &leaf : leaves) {
        PtrVal at = src;
        at.off = IntVal::Exact(LinExpr::Const(sOff + leaf.off));
        auto cell = s.cells.find(CellKey(src.id, sOff + leaf.off, leaf.size));
        if (cell != s.cells.end()) {
            values.push_back({leaf, cell->second});
            continue;
        }
        if (leaf.ptr && src.kind != BaseKind::Param) {
            continue;
        }
        Value v = LoadValue(at, leaf.size, fir::kNone, s);
        if (v.isPtr == leaf.ptr && (v.isPtr || v.i.kind != IntVal::Kind::Top)) {
            values.push_back({leaf, v});
        }
    }
    // 目标那一段：重叠的格失效、整段记写过（同 store），再写入读出的值；其余对象的格不动
    for (auto it = s.cells.begin(); it != s.cells.end();) {
        bool overlap = std::get<0>(it->first) == dst.id && std::get<1>(it->first) < dOff + size &&
                       dOff < std::get<1>(it->first) + std::get<2>(it->first);
        it = overlap ? s.cells.erase(it) : std::next(it);
    }
    InvalidateFamily(s, dst, dOff, size);  // SPEC-0090
    s.written.insert(CellKey(dst.id, dOff, size));
    AliasWritten(s, dst.id);
    AddMod(dst, size);
    for (const auto &lv : values) {
        s.cells[CellKey(dst.id, dOff + lv.first.off, lv.first.size)] = lv.second;
    }
    return true;
}

void Analyzer::Clobber(const fir::Inst &inst, models::Kind kind, llvm::StringRef name, State &s)
{
    if (kind == models::Kind::None) {
        s.called = true;    // 被调者可能写全局：之后读全局标量不再是入口值（SPEC-0041）
        AddModText("g:*");  // 没有摘要：往上传时也按"可能写任何全局"（SPEC-0045 §2.3）
    }
    // 被调者可能经指针 / 全局写内存（效果在 S7d-3）；纯函数、释放函数不改被跟踪的内存；
    // 访问类模型只写 W 实参所指的对象：只失效那些基址的格（SPEC-0089）
    if (kind != models::Kind::Access) {
        s.cells.clear();
    }
    // 交给被调者的指针所指的对象可能被改（SPEC-0026）；访问类模型只写目的一侧
    std::set<size_t> writtenArgs;
    if (kind == models::Kind::Access) {
        for (const models::AccessCond &c : models.Access(name)->conds) {
            if (c.kind == "W") {
                writtenArgs.insert(static_cast<size_t>(c.base));
            }
        }
    }
    for (size_t k = 0; k < inst.args.size(); ++k) {
        if (kind == models::Kind::Access && writtenArgs.count(k) == 0) {
            continue;
        }
        Value v = Eval(inst.args[k], s);
        if (!v.isPtr) {
            if (kind == models::Kind::Access) {
                s.cells.clear();  // 目的不是指针（不认识）：保守
                s.clobbered = true;
            }
            continue;
        }
        if (v.p.kind == BaseKind::Unknown) {
            s.clobbered = true;
            s.cells.clear();
        } else {
            MarkWritten(s, v.p.id);
            if (kind == models::Kind::Access) {
                EraseObject(s, v.p);  // 这个对象整块失效（SPEC-0089）
            }
        }
        AddMod(v.p, llvm::None);  // 交给了不知道写哪里的被调者 / 访问类模型的目的：整块（SPEC-0045）
    }
}

std::map<std::string, std::string> Analyzer::GlobalsAt(const State &s,
                                                       std::map<std::string, summary::Local> *locals) const
{
    std::map<std::string, std::string> out;
    if (s.called || s.clobbered || s.externAll) {
        out["g:*"] = "?";  // 调用过可能写任何全局的函数 / 内存整个被清 / 经 char 写过外部内存（SPEC-0103）
    }
    // 只看状态里动过的全局（格、常数偏移写过、整块写过；SPEC-0080）——called / clobbered 时上面的 g:* 已经兜住其余的
    std::set<std::string> ids;
    for (const auto &cell : s.cells) {
        if (std::get<0>(cell.first).compare(0, 1, "G") == 0) {
            ids.insert(std::get<0>(cell.first));
        }
    }
    for (const CellKey &w : s.written) {
        if (std::get<0>(w).compare(0, 1, "G") == 0) {
            ids.insert(std::get<0>(w));
        }
    }
    for (const std::string &w : s.writtenAll) {
        if (w.compare(0, 1, "G") == 0) {
            ids.insert(w);
        }
    }
    // 被调者写过、本模块看不到的全局（ApplyMods 记在 writtenAll 里的 g:…）：后面调用点上当"改过"（SPEC-0086）
    for (const std::string &w : s.writtenAll) {
        if (llvm::StringRef(w).startswith("g:")) {
            out[w] = "?";
        }
    }
    for (const std::string &id : ids) {
        const fir::Global *global = GlobalOf(id);
        int64_t size = 0;
        if (global == nullptr || global->type >= m.types.size() || m.types[global->type].kind != fir::TypeKind::Int ||
            !TypeSize(m, global->type, size)) {
            continue;
        }
        const fir::Global &g = *global;
        std::string name = GlobalSymbol(g.key, g.name);  // 带身份（SPEC-0086）
        auto cell = s.cells.find(CellKey(id, 0, size));
        const IntVal *iv = cell != s.cells.end() && !cell->second.isPtr ? &cell->second.i : nullptr;
        if (iv != nullptr && iv->kind == IntVal::Kind::Exact && !iv->value.IsUnknown()) {
            if (iv->value.Show() != name) {
                out[name] = iv->value.Show();  // 本函数写过的确切值
            }
        } else if (locals != nullptr && iv != nullptr && iv->kind == IntVal::Kind::Range && IsCodeTag(iv->tag) &&
                   iv->Lower() && iv->Upper() && !iv->Lower()->IsUnknown() && !iv->Upper()->IsUnknown()) {
            std::string local = "_gv_" + name.substr(2);  // 代码给出的区间：调用点局部（SPEC-0077）
            (*locals)[local] = {iv->Lower()->Show(), iv->Upper()->Show(), TagName(iv->tag).str()};
            out[name] = local;
        } else if (Changed(s, id, 0, size)) {
            out[name] = "?";
        }
    }
    return out;
}

void Analyzer::ResolveFnLocals()
{
    // 局部的全部赋值（set）都是函数地址常量、空指针或已解析的局部（可隔着类型转换）才解析（流不敏感，可靠）；
    // 形参、内存对象、被 load / call / havoc 赋过的不解析
    std::set<uint32_t> bad;
    for (const fir::Local &l : f.locals) {
        if (l.param >= 0 || l.obj) {
            bad.insert(l.id);
        }
    }
    std::vector<std::pair<uint32_t, ExprId>> defs;
    for (const fir::Block &b : f.blocks) {
        for (const fir::Inst &inst : b.insts) {
            if (inst.dst == fir::kNone) {
                continue;
            }
            if (inst.op == fir::InstOp::Set) {
                defs.emplace_back(inst.dst, inst.val);
            } else {
                bad.insert(inst.dst);
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &d : defs) {
            if (bad.count(d.first) != 0) {
                continue;
            }
            ExprId e = StripCasts(d.second);
            if (e >= f.exprs.size()) {
                bad.insert(d.first);
                changed = true;
                continue;
            }
            const fir::Expr &x = f.exprs[e];
            std::set<std::string> &out = fnTargets[d.first];
            size_t before = out.size();
            if (x.op == ExprOp::FAddr) {
                out.insert(x.text);
            } else if (x.op == ExprOp::Const && x.value == 0) {
                // 空指针：经它调用是未定义行为，不算目标
            } else if (x.op == ExprOp::Local && bad.count(static_cast<uint32_t>(x.value)) == 0) {
                auto from = fnTargets.find(static_cast<uint32_t>(x.value));
                if (from != fnTargets.end() && from->first != d.first) {
                    out.insert(from->second.begin(), from->second.end());
                }
            } else {
                bad.insert(d.first);
                changed = true;
                continue;
            }
            changed = changed || out.size() != before;
        }
    }
    for (uint32_t l : bad) {
        fnTargets.erase(l);
    }
}

std::vector<std::string> Analyzer::TargetsOf(ExprId e) const
{
    e = StripCasts(e);
    if (e >= f.exprs.size()) {
        return {};
    }
    const fir::Expr &x = f.exprs[e];
    if (x.op == ExprOp::FAddr) {
        return {x.text};
    }
    if (x.op != ExprOp::Local) {
        return {};
    }
    auto it = fnTargets.find(static_cast<uint32_t>(x.value));
    if (it == fnTargets.end()) {
        return {};
    }
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

bool Analyzer::AltCall(const fir::Inst &inst, const std::vector<std::string> &targets, State &s)
{
    // 多个可能的目标（SPEC-0058 §2.2）：每个目标一条 alt 调用点记录；状态只按各目标的 mods 失效，不应用返回值 / 出参效果
    std::vector<const summary::Summary *> sums;
    for (const std::string &t : targets) {
        if (models.KindOf(KeyName(t)) != models::Kind::None) {
            return false;  // 目标里有模型函数：照旧按解析不出处理（fptr）
        }
        const summary::Summary *sum = nullptr;
        if (callees != nullptr) {
            auto it = callees->find(t);
            if (it == callees->end()) {
                it = callees->find(KeyName(t).str());
            }
            if (it != callees->end()) {
                sum = &it->second;
            }
        }
        sums.push_back(sum);
    }
    for (const std::string &t : targets) {
        fir::Inst one = inst;
        one.callee = t;
        one.calleeExpr = fir::kNone;
        RecordCall(one, s, true);
    }
    if (llvm::is_contained(sums, nullptr)) {
        Clobber(inst, models::Kind::None, "", s);
    } else {
        for (const summary::Summary *sum : sums) {
            ApplyMods(inst, sum->mods, s);
        }
    }
    if (inst.dst != fir::kNone) {
        Value result;  // 不知道执行的是哪个：返回值未知
        result.isPtr = IsPtrLocal(inst.dst);
        result.p = UnknownPtr("call");
        result.i = IntVal::Top("call");
        SetLocal(s, inst.dst, result);
    }
    return true;
}

void Analyzer::RecordCall(const fir::Inst &inst, const State &s, bool alt)
{
    if (!recording) {
        return;
    }
    if (inst.calleeExpr != fir::kNone) {
        // 间接调用解析不出目标（06- A-60 ①）：实参里有指针时记账 fptr，经函数指针交出去的路径不能悄悄断掉（SPEC-0057）。
        // C++ 的成员函数 / 构造函数调用（this 不建模，降级成目标未知的调用）记 cxx_call（SPEC-0121）
        const fir::Expr &target = f.exprs[inst.calleeExpr];
        const char *reason = target.op == ExprOp::Unk && target.text == "cxx_call" ? "cxx_call" : "fptr";
        for (ExprId arg : inst.args) {
            Value v = Eval(arg, s);
            if (v.isPtr && v.p.kind != BaseKind::Func) {
                AddOpen(reason, SiteOf(m, inst.loc));
                break;
            }
        }
        return;
    }
    summary::Call call;
    call.site = SiteOf(m, inst.loc);
    call.callee = KeyName(inst.callee).str();
    call.calleeKey = inst.callee;  // SPEC-0027
    call.alt = alt;                // SPEC-0058
    call.ind = alt || resolving;
    // 模型表优先（I-44，SPEC-0018 §2.1）
    models::Kind kind = models.KindOf(call.callee);
    bool pure = kind == models::Kind::Pure;
    // CharAlias=verify（SPEC-0106）：经 char 写过外部内存之后，把别的外部内存交给被调者、被调者的 Req 里用到全局 → 记不别名假设
    if (options.charVerify && !s.charRoots.empty() && kind == models::Kind::None) {
        for (ExprId arg : inst.args) {
            Value v = Eval(arg, s);
            if (v.isPtr) {
                NoteAssume(s, RootOf(v.p.id));
            }
        }
        const summary::Summary *calleeSummary = nullptr;
        if (callees != nullptr) {
            auto it = callees->find(inst.callee);  // 按函数键；找不到再按名字（SPEC-0027）
            if (it == callees->end()) {
                it = callees->find(call.callee);
            }
            calleeSummary = it != callees->end() ? &it->second : nullptr;
        }
        if (calleeSummary != nullptr) {
            static llvm::Regex global("g:[A-Za-z_][A-Za-z0-9_]*");
            for (const summary::Req &r : calleeSummary->reqs) {
                std::string text = r.have + " " + r.endLo + " " + r.endHi;
                for (const std::string &g : r.guard) {
                    text += " " + g;
                }
                llvm::StringRef rest = text;
                llvm::SmallVector<llvm::StringRef, 1> match;
                while (global.match(rest, &match)) {
                    NoteAssume(s, StripGlobalSuffix(match[0].str()));
                    rest = rest.drop_front(match[0].end() - rest.begin());
                }
            }
        }
    }
    if (kind != models::Kind::None) {
        call.model = models::KindName(kind).str();
    } else {
        std::map<std::string, summary::Local> ranges;
        std::map<std::string, std::string> globals = GlobalsAt(s, &ranges);  // 调用前改过的全局（SPEC-0064 / 0077）
        if (!globals.empty()) {
            call.gvals = std::move(globals);
        }
        if (!ranges.empty()) {
            if (!call.locals) {
                call.locals = std::map<std::string, summary::Local>();
            }
            call.locals->insert(ranges.begin(), ranges.end());
        }
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
                    // 偏移是区间：另记上下界与来源，代换时按对象起点 + 偏移区间判（L1 不能拿最小剩余判，SPEC-0095）
                    llvm::Optional<LinExpr> lo = v.p.off.Lower();
                    if (v.p.off.kind == IntVal::Kind::Range && !(lo && *lo == *hi)) {
                        if (lo && !lo->IsUnknown()) {
                            arg.ptr->offLo = lo->Show();
                        }
                        arg.ptr->offHi = hi->Show();
                        arg.ptr->offBound = TagName(v.p.off.tag == Tag::None ? Tag::Widen : v.p.off.tag).str();
                    }
                    arg.ptr->back = BackText(v.p);  // 前面取最小的（偏移下界，SPEC-0039）
                    // 指向成员数组 / 多维数组的一行：另记最内层子对象的剩余，代换时内层也查（SPEC-0104 §2.3 3b）
                    arg.ptr->sub = SubRest(v.p);
                    arg.ptr->subBack = SubBack(v.p);  // SPEC-0107 §2.2 第 3 步
                    if (v.p.kind == BaseKind::Param && v.p.off.IsConst() && v.p.off.value.Constant() == 0) {
                        arg.ptr->alias = v.p.obj.substr(1);  // *p0 → p0
                    }
                    if (v.p.hasParent) {
                        // 成员数组：外层对象也记下，代换时外层也查（SPEC-0035 §2.1）
                        summary::OuterObj outer;
                        outer.obj = v.p.parentObj;
                        outer.have = (v.p.parentSize - (LinExpr::Const(v.p.parentOff) + *hi)).Show();
                        arg.ptr->parent = outer;
                    }
                    if (v.p.kind == BaseKind::Heap && v.p.effDepth > 0) {
                        arg.ptr->effDepth = v.p.effDepth;  // 经几层 ret_new 得到的对象（D-25，SPEC-0024 §2.2）
                    }
                    std::map<std::string, summary::Local> fieldRanges;
                    std::map<std::string, summary::ArgValue> fieldValues = ArgFields(v.p, s, k, &fieldRanges);
                    if (!fieldValues.empty()) {
                        arg.ptr->fields = fieldValues;  // 局部 / 全局结构体里存进去的字段值（SPEC-0026）
                    }
                    if (!fieldRanges.empty()) {
                        if (!call.locals) {
                            call.locals = std::map<std::string, summary::Local>();
                        }
                        call.locals->insert(fieldRanges.begin(), fieldRanges.end());  // SPEC-0069
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
        if (l.param >= 0 && l.type < m.types.size() && m.types[l.type].kind == fir::TypeKind::Ptr) {
            env.SetTypeRange("nn:p" + std::to_string(l.param), 0, 1);  // 形参不是空指针（SPEC-0099）
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
        std::set<std::string> syms;
        CollectSyms(g, syms);
        for (const std::string &x : syms) {
            if (x.compare(0, 3, "nn:") == 0) {
                env.SetTypeRange(x, 0, 1);  // 不是空指针：0 / 1（SPEC-0099 / 0101）
            }
        }
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
    // 无符号、下界盖住 0：回绕后的真实值在 [0, 数学值 − k·2^W] 里，仍在区间内（SPEC-0056：i <= n 的循环不再多估）
    if (coversLo && !t->isSigned) {
        return true;
    }
    // 有符号：符号界只可能因有符号算术溢出而超出类型（不保值的转换都落到类型范围，A-27），溢出是未定义行为、不考虑
    // （06- A-21、SPEC-0014）；常数界照旧查（SPEC-0076）
    // 盖住类型最小 / 最大值的界照样可信（汇合时可能按类型范围放宽成比 INT_MIN 还小的常数）
    bool loOk = v.hasLo ? coversLo || (t->isSigned && !v.lo.IsConst()) || within(v.lo, false) : t->isSigned;
    bool hiOk = v.hasHi ? coversHi || (t->isSigned && !v.hi.IsConst()) || within(v.hi, false) : t->isSigned;
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

// 形参指针与空指针比较（SPEC-0099）：p != NULL → nn:p<k> >= 1；p == NULL → nn:p<k> <= 0；与已有守卫矛盾 → 不可达。其余不产生信息
bool Analyzer::AssumeNull(fir::CmpOp op, const Value &va, const Value &vb, State &s)
{
    auto isNull = [](const Value &v) {
        return (!v.isPtr && v.i.IsConst() && v.i.value.Constant() == 0) ||
               (v.isPtr && v.p.kind == BaseKind::Unknown && v.p.why == "null");
    };
    auto paramOf = [](const Value &v) {
        const std::string &id = v.p.id;
        bool param = v.isPtr && v.p.kind == BaseKind::Param && id.size() >= 2 && id[0] == 'P' &&
                     std::isdigit(static_cast<unsigned char>(id[1])) && id.find('.') == std::string::npos;
        return param ? static_cast<int>(std::strtol(id.c_str() + 1, nullptr, 10)) : -1;
    };
    // 指针字段所指（*p<k>->f，SPEC-0101）：符号 nn:p<k>->f
    auto fieldOf = [](const Value &v) {
        bool field = v.isPtr && v.p.kind == BaseKind::ParamPtr && v.p.obj.size() > 1 && v.p.obj[0] == '*' &&
                     v.p.obj.find("->") != std::string::npos;
        return field ? v.p.obj.substr(1) : std::string();
    };
    int k = isNull(vb) ? paramOf(va) : isNull(va) ? paramOf(vb) : -1;
    std::string field = isNull(vb) ? fieldOf(va) : isNull(va) ? fieldOf(vb) : std::string();
    if ((k < 0 && field.empty()) || (op != fir::CmpOp::Ne && op != fir::CmpOp::Eq)) {
        return true;
    }
    LinExpr nn = LinExpr::Sym(k >= 0 ? "nn:p" + std::to_string(k) : "nn:" + field);
    LinExpr atom = op == fir::CmpOp::Ne ? LinExpr::Const(1) - nn : nn;  // ≤ 0
    SymEnv env = Env(s);
    if (EntailsLe0(LinExpr::Const(1) - atom, s.guards, env)) {
        return false;  // 已有守卫说 atom >= 1：这条边走不到
    }
    if (!EntailsLe0(atom, s.guards, env)) {
        AddGuard(s, atom);
    }
    return true;
}

// 收窄出的区间非空：lo ≤ hi 在这条边上恒成立，不是已经成立的就记成守卫（SPEC-0098）
void Analyzer::NonEmptyGuard(const IntVal &v, State &s)
{
    llvm::Optional<LinExpr> lo = v.Lower();
    llvm::Optional<LinExpr> hi = v.Upper();
    if (v.kind != IntVal::Kind::Range || !lo || !hi) {
        return;
    }
    LinExpr d = *lo - *hi;
    if (d.IsUnknown() || d.IsConst() || EntailsLe0(d, s.guards, Env(s))) {
        return;
    }
    // 整数约简：各项系数除以公约数 g，常数项向上取整（Σ a·x + c ≤ 0 ⇔ Σ (a/g)·x + ⌈c/g⌉ ≤ 0），4·p1 >= 4 写成 p1 >= 1
    int64_t g = 0;
    for (const auto &term : d.Terms()) {
        g = Gcd(g, term.second);
    }
    if (g > 1) {
        LinExpr norm = LinExpr::Const(-FloorDivInt(-d.Constant(), g));
        for (const auto &term : d.Terms()) {
            norm = norm + LinExpr::OfAtom(term.first).Scale(term.second / g);
        }
        d = norm;
    }
    AddGuard(s, d);
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

llvm::Optional<LinExpr> Analyzer::SymOf(ExprId e, const State &s, int depth)
{
    if (e >= f.exprs.size() || depth > 8) {
        return llvm::None;
    }
    const fir::Expr &x = f.exprs[e];
    switch (x.op) {
        case ExprOp::Const:
            return LinExpr::Const(x.value);
        case ExprOp::Local: {
            uint32_t id = static_cast<uint32_t>(x.value);
            if (IsPtrLocal(id)) {
                return llvm::None;
            }
            auto it = s.locals.find(id);
            if (it != s.locals.end() && !it->second.isPtr && it->second.i.kind == IntVal::Kind::Exact &&
                !it->second.i.value.IsUnknown()) {
                return it->second.i.value;
            }
            return LocalSym(id, false);
        }
        case ExprOp::Cast:
            return SymOf(x.a, s, depth + 1);  // 不回绕的假设下值不变
        case ExprOp::Neg: {
            llvm::Optional<LinExpr> a = SymOf(x.a, s, depth + 1);
            return a ? llvm::Optional<LinExpr>(-*a) : llvm::None;
        }
        case ExprOp::Bin: {
            auto bop = static_cast<fir::BinOp>(x.sub);
            if (bop != fir::BinOp::Add && bop != fir::BinOp::Sub && bop != fir::BinOp::Mul) {
                return llvm::None;
            }
            llvm::Optional<LinExpr> a = SymOf(x.a, s, depth + 1);
            llvm::Optional<LinExpr> b = SymOf(x.b, s, depth + 1);
            if (!a || !b) {
                return llvm::None;
            }
            if (bop == fir::BinOp::Mul) {
                if (b->IsConst()) {
                    return a->Scale(b->Constant());
                }
                if (a->IsConst()) {
                    return b->Scale(a->Constant());
                }
                return llvm::None;
            }
            LinExpr out = bop == fir::BinOp::Add ? *a + *b : *a - *b;
            return out.IsUnknown() ? llvm::None : llvm::Optional<LinExpr>(out);
        }
        default:
            return llvm::None;
    }
}

llvm::Optional<std::pair<std::string, LinExpr>> Analyzer::SymPtr(ExprId e, const State &s, int depth)
{
    if (e >= f.exprs.size() || depth > 8) {
        return llvm::None;
    }
    const fir::Expr &x = f.exprs[e];
    if (x.op == ExprOp::Local) {
        uint32_t id = static_cast<uint32_t>(x.value);
        auto it = s.locals.find(id);
        if (!IsPtrLocal(id) || it == s.locals.end() || !it->second.isPtr || it->second.p.kind == BaseKind::Unknown ||
            it->second.p.kind == BaseKind::Func) {
            return llvm::None;
        }
        const PtrVal &p = it->second.p;
        bool exact = p.off.kind == IntVal::Kind::Exact && !p.off.value.IsUnknown();
        return std::make_pair(p.id, exact ? p.off.value : LocalSym(id, true));
    }
    if (x.op == ExprOp::PAdd) {
        llvm::Optional<std::pair<std::string, LinExpr>> base = SymPtr(x.a, s, depth + 1);
        llvm::Optional<LinExpr> d = SymOf(x.b, s, depth + 1);
        if (!base || !d) {
            return llvm::None;
        }
        return std::make_pair(base->first, base->second + *d);
    }
    if (x.op == ExprOp::SubObj) {
        return SymPtr(x.a, s, depth + 1);
    }
    Value v = Eval(e, s);  // 其余：偏移确切时直接用（同一基址身份）
    if (x.op == ExprOp::Fld) {
        llvm::Optional<std::pair<std::string, LinExpr>> base = SymPtr(x.a, s, depth + 1);
        if (base && v.isPtr && v.p.id == base->first) {
            return std::make_pair(base->first, base->second + LinExpr::Const(x.value));
        }
    }
    if (v.isPtr && v.p.kind != BaseKind::Unknown && v.p.kind != BaseKind::Func && v.p.off.kind == IntVal::Kind::Exact &&
        !v.p.off.value.IsUnknown()) {
        return std::make_pair(v.p.id, v.p.off.value);
    }
    return llvm::None;
}

// AssumeNoWrap（SPEC-0108 §2.1）：比较两边的符号式之差含局部符号 → 记成局部线性关系（同一对象的两个指针比偏移）
void Analyzer::NoteRel(fir::CmpOp op, ExprId a, ExprId b, State &s)
{
    llvm::Optional<LinExpr> sa;
    llvm::Optional<LinExpr> sb = LinExpr::Const(0);
    if (Eval(a, s).isPtr) {
        llvm::Optional<std::pair<std::string, LinExpr>> pa = SymPtr(a, s);
        llvm::Optional<std::pair<std::string, LinExpr>> pb =
            b != fir::kNone ? SymPtr(b, s) : llvm::Optional<std::pair<std::string, LinExpr>>();
        if (!pa || !pb || pa->first != pb->first) {
            return;
        }
        sa = pa->second;
        sb = pb->second;
    } else {
        sa = SymOf(a, s);
        if (b != fir::kNone) {
            sb = SymOf(b, s);
        }
    }
    if (!sa || !sb) {
        return;
    }
    LinExpr d = *sa - *sb;  // a − b
    if (d.IsUnknown() || !HasLocalSym(d)) {
        return;
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
            atoms = {-d};
            break;
        case fir::CmpOp::Eq:
            atoms = {d, -d};
            break;
        case fir::CmpOp::Ne:
            break;
    }
    for (const LinExpr &r : atoms) {
        if (!llvm::is_contained(s.rels, r)) {
            s.rels.push_back(r);
        }
        if (s.rels.size() > kMaxRels) {
            s.rels.erase(s.rels.begin());
        }
    }
}

// a op b（b 为 kNone 时是常数 0）。两边都是确切值 → 原子（常数为假 → 不可达）；一边是区间、一边是确切值 → 收窄区间并推出原子
// （SPEC-0017 §2.3）；在类型范围与已有守卫下已经成立的原子不加
bool Analyzer::AssumeCmp(fir::CmpOp op, ExprId a, ExprId b, State &s)
{
    if (options.assumeNoWrap) {
        NoteRel(op, a, b, s);  // 局部线性关系（SPEC-0108）
    }
    Value va = Eval(a, s);
    Value vb;
    vb.i = IntVal::Exact(LinExpr::Const(0));
    if (b != fir::kNone) {
        vb = Eval(b, s);
    }
    static const fir::CmpOp mirror[] = {fir::CmpOp::Eq, fir::CmpOp::Ne, fir::CmpOp::Gt,
                                        fir::CmpOp::Ge, fir::CmpOp::Lt, fir::CmpOp::Le};
    bool offsets = false;  // 比的是同一对象两个指针的偏移（数学整数，不用查忠实）
    if (va.isPtr || vb.isPtr) {
        // 同一对象的两个指针：比较偏移（越过对象的指针算术是未定义行为，偏移按数学整数，SPEC-0081）；其余不产生信息
        bool sameObject = b != fir::kNone && va.isPtr && vb.isPtr && va.p.kind == vb.p.kind && va.p.id == vb.p.id &&
                          va.p.kind != BaseKind::Unknown && va.p.kind != BaseKind::Func;
        if (!sameObject) {
            return AssumeNull(op, va, vb, s);  // 形参指针判空（SPEC-0099）
        }
        const IntVal &oa = va.p.off;
        const IntVal &ob = vb.p.off;
        // 被收窄的指针局部指向的类型宽于 1 字节（或不知道）：它按元素大小走，按字节收窄的上界比真实的大到"元素大小 − 1"，
        // 不当代码给出的界（SPEC-0083）
        auto coarse = [&](ExprId e) {
            uint32_t local = WrittenLocal(e, s);
            const fir::Type *type = local != fir::kNone ? LocalType(local) : nullptr;
            int64_t size = 0;
            return type == nullptr || type->kind != fir::TypeKind::Ptr || type->to >= m.types.size() ||
                   !TypeSize(m, type->to, size) || size != 1;
        };
        if (oa.kind == IntVal::Kind::Range && ob.kind == IntVal::Kind::Exact) {
            bool ok = NarrowPtr(a, op, ob.value, s, coarse(a));
            if (ok) {
                NonEmptyGuard(Eval(a, s).p.off, s);  // SPEC-0098
            }
            return ok;
        }
        if (ob.kind == IntVal::Kind::Range && oa.kind == IntVal::Kind::Exact) {
            bool ok = NarrowPtr(b, mirror[static_cast<int>(op)], oa.value, s, coarse(b));
            if (ok) {
                NonEmptyGuard(Eval(b, s).p.off, s);  // SPEC-0098
            }
            return ok;
        }
        if (oa.kind != IntVal::Kind::Exact || ob.kind != IntVal::Kind::Exact) {
            return true;
        }
        // 两边确切：同整数，按偏移的差推原子（差是常数直接判真假；p != p + n 推出 n >= 1，SPEC-0082）
        IntVal offA = oa;  // oa / ob 引用着 va / vb，先复制
        IntVal offB = ob;
        va = Value();
        vb = Value();
        va.i = offA;
        vb.i = offB;
        offsets = true;
    }
    // 带情形的值与常数比较：按情形筛选（SPEC-0025 §2.4）
    static const fir::CmpOp swapped[] = {fir::CmpOp::Eq, fir::CmpOp::Ne, fir::CmpOp::Gt,
                                         fir::CmpOp::Ge, fir::CmpOp::Lt, fir::CmpOp::Le};
    if (!offsets && !va.i.cases.empty() && vb.i.IsConst()) {
        return FilterCases(a, va.i, op, vb.i.value.Constant(), s);
    }
    if (!offsets && b != fir::kNone && !vb.i.cases.empty() && va.i.IsConst()) {
        return FilterCases(b, vb.i, swapped[static_cast<int>(op)], va.i.value.Constant(), s);
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
    if (!offsets && !options.assumeNoWrap && (!usable(a, va.i, aLarge) || !usable(b, vb.i, bLarge))) {
        return true;  // AssumeNoWrap：检查按数学整数，不做忠实判断（SPEC-0108 §2.2）
    }
    static const fir::CmpOp mirrored[] = {fir::CmpOp::Eq, fir::CmpOp::Ne, fir::CmpOp::Gt,
                                          fir::CmpOp::Ge, fir::CmpOp::Lt, fir::CmpOp::Le};
    const IntVal &ia = va.i;
    const IntVal &ib = vb.i;
    LinExpr one = LinExpr::Const(1);
    llvm::Optional<IntVal> narrowed;  // 收窄过的区间（SPEC-0098）
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
        narrowed = EvalInt(a, s);  // 区间非空的守卫最后再加（SPEC-0098）
        AtomsOfRange(ia, op, ib.value, atoms);
    } else if (ib.kind == IntVal::Kind::Range && ia.kind == IntVal::Kind::Exact && b != fir::kNone) {
        fir::CmpOp m = mirrored[static_cast<int>(op)];
        if (!Narrow(b, m, ia.value, s)) {
            return false;
        }
        narrowed = EvalInt(b, s);  // SPEC-0098
        AtomsOfRange(ib, m, ia.value, atoms);
    } else if (ia.kind == IntVal::Kind::Range && ib.kind == IntVal::Kind::Range && b != fir::kNone) {
        // 两边都是区间（SPEC-0110）：a < b ≤ hi(b) → 按 hi(b) 收窄 a；b > a ≥ lo(a) → 按 lo(a) 收窄 b；只用代码给出的界
        auto bound = [](const IntVal &v, bool upper) {
            llvm::Optional<LinExpr> x = upper ? v.Upper() : v.Lower();
            return x && !x->IsUnknown() && IsCodeTag(v.tag) ? x : llvm::None;
        };
        IntVal oa = ia;  // ia / ib 引用着 va / vb，收窄 a 之后还要用原来的界
        IntVal ob = ib;
        bool less = op == fir::CmpOp::Lt || op == fir::CmpOp::Le;
        bool greater = op == fir::CmpOp::Gt || op == fir::CmpOp::Ge;
        bool eq = op == fir::CmpOp::Eq;
        fir::CmpOp toA = op;
        fir::CmpOp toB = mirrored[static_cast<int>(op)];
        if (less || eq) {
            llvm::Optional<LinExpr> h = bound(ob, true);
            llvm::Optional<LinExpr> l = bound(oa, false);
            if (h && !Narrow(a, eq ? fir::CmpOp::Le : toA, *h, s)) {
                return false;
            }
            if (l && !Narrow(b, eq ? fir::CmpOp::Ge : toB, *l, s)) {
                return false;
            }
        }
        if (greater || eq) {
            llvm::Optional<LinExpr> l = bound(ob, false);
            llvm::Optional<LinExpr> h = bound(oa, true);
            if (l && !Narrow(a, eq ? fir::CmpOp::Ge : toA, *l, s)) {
                return false;
            }
            if (h && !Narrow(b, eq ? fir::CmpOp::Le : toB, *h, s)) {
                return false;
            }
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
        if (EntailsLe0(atom, s.guards, env)) {
            continue;  // 已经成立，没有信息
        }
        AddGuard(s, atom);
    }
    if (narrowed) {
        NonEmptyGuard(*narrowed, s);
    }
    return true;
}

// e op c，e 的值是区间：收窄（e 是局部、或保值的类型转换下的局部时写回）；区间为空 → 不可达
bool Analyzer::NarrowRange(const IntVal &v, fir::CmpOp op, const LinExpr &c, const State &s, IntVal &out, bool &changed,
                           bool coarse)
{
    out = v;
    changed = false;
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
    // 同余（SPEC-0084）：候选的界先取到同余类里（上界向下、下界向上）；上界取整成功 → 界就是同余类里最大的值，不必 coarse
    LinExpr anchor;
    int64_t stride = 0;
    bool cong = v.kind == IntVal::Kind::Range && CongOf(v, anchor, stride) && stride >= 2;
    if (cong && newHi && !newHi->IsUnknown()) {
        if (llvm::Optional<LinExpr> r = RoundDown(*newHi, anchor, stride)) {
            newHi = r;
            coarse = false;
        }
    }
    if (cong && newLo && !newLo->IsUnknown()) {
        if (llvm::Optional<LinExpr> r = RoundUp(*newLo, anchor, stride)) {
            newLo = r;
        }
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
    changed = true;
    if (l && h && same(l, *h)) {
        out = IntVal::Exact(*l);
    } else {
        // 标签说的是上界的来源（F-12 只看上界，同 replay.py）：上界被代码里的检查收窄 → check，循环头上的条件 → loop
        // （SPEC-0017 §2.2）；只收窄了下界 → 原标签不变
        bool hiNarrowed = h && (!hi || !(*h == *hi));
        Tag by = coarse ? Tag::Widen : edgeFromHead ? Tag::Loop : Tag::Check;
        Tag tag = !hiNarrowed ? v.tag : coarse ? by : (IsCodeTag(v.tag) ? WorseTag(v.tag, by) : by);
        out = IntVal::Range(l, h, tag);
        if (cong) {
            SetCong(out, anchor, stride);  // 收窄不改同余（SPEC-0084）
        }
    }
    return true;
}

// 写回时跳过保值的类型转换（转换的值与里面的值相同）；不是局部 → kNone
uint32_t Analyzer::WrittenLocal(ExprId e, const State &s)
{
    ExprId cur = e;
    while (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Cast && Eval(f.exprs[cur].a, s) == Eval(cur, s)) {
        cur = f.exprs[cur].a;
    }
    if (cur >= f.exprs.size() || f.exprs[cur].op != ExprOp::Local) {
        return fir::kNone;
    }
    return static_cast<uint32_t>(f.exprs[cur].value);
}

bool Analyzer::Narrow(ExprId e, fir::CmpOp op, const LinExpr &c, State &s)
{
    IntVal v = EvalInt(e, s);
    IntVal out;
    bool changed = false;
    if (!NarrowRange(v, op, c, s, out, changed)) {
        return false;
    }
    // 写回局部：跳过保值的类型转换（转换的值与里面的值相同）
    ExprId cur = e;
    while (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Cast && EvalInt(f.exprs[cur].a, s) == v) {
        cur = f.exprs[cur].a;
    }
    if (!changed || cur >= f.exprs.size() || f.exprs[cur].op != ExprOp::Local) {
        return true;
    }
    Value written;
    written.i = out;
    s.locals[static_cast<uint32_t>(f.exprs[cur].value)] = written;
    return true;
}

// 指针 e 的偏移按 op c 收窄并写回指针局部（同一对象的两个指针比较，SPEC-0081）
bool Analyzer::NarrowPtr(ExprId e, fir::CmpOp op, const LinExpr &c, State &s, bool coarse)
{
    Value v = Eval(e, s);
    IntVal out;
    bool changed = false;
    if (!NarrowRange(v.p.off, op, c, s, out, changed, coarse)) {
        return false;
    }
    uint32_t local = WrittenLocal(e, s);
    if (changed && local != fir::kNone) {
        // 内层窗口（SPEC-0104 §2.2）：与外层偏移相差定值（上下界的差相同）时同步收窄，否则不动（只会更宽）
        llvm::Optional<LinExpr> lo = v.p.off.Lower();
        llvm::Optional<LinExpr> hi = v.p.off.Upper();
        for (SubWindow &w : v.p.subs) {
            llvm::Optional<LinExpr> wlo = w.off.Lower();
            llvm::Optional<LinExpr> whi = w.off.Upper();
            if (!lo || !wlo || lo->IsUnknown() || wlo->IsUnknown() || hi.hasValue() != whi.hasValue()) {
                continue;
            }
            LinExpr d = *lo - *wlo;
            if (d.IsUnknown() || (hi && *hi - *whi != d)) {
                continue;
            }
            w.off = AddVals(out, IntVal::Exact(-d));
        }
        v.p.off = out;
        s.locals[local] = v;
    }
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
        base.faithful.push_back(!v.isPtr && (options.assumeNoWrap || Faithful(arg, v.i, env)));  // SPEC-0108
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
            // 段 B：E <= 0x7fffffff − 2^W（且 E <= −1），回绕后的值仍不超过上限，照样写，destMax = E + 2^W（AssumeNoWrap 时没有这一段）
            int64_t modulus = int64_t(1) << bits;
            Segment segB = base;
            if (!options.assumeNoWrap && guard(segB, inner - LinExpr::Const(kSecurecMemMax - modulus)) &&
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
            if (cond.upto && bytes.kind != IntVal::Kind::Top) {
                // 只是上界：实际 1 … bytes 字节；确切的上界标 model，区间的上界保留更差的标签（SPEC-0034 §2.1）
                Tag tag = bytes.kind == IntVal::Kind::Exact ? Tag::Model : WorseTag(bytes.tag, Tag::Model);
                bytes = IntVal::Range(LinExpr::Const(1), bytes.Upper(), tag);
            }
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
            // AssumeNoWrap（SPEC-0108 §2.2）：模型的字节数把实参换成实参的符号式；基址实参的符号偏移
            llvm::Optional<SymAccess> sym;
            if (options.assumeNoWrap) {
                llvm::Optional<std::pair<std::string, LinExpr>> p = SymPtr(inst.args[cond.base], seg.t);
                std::set<std::string> used;
                CollectSyms(bytesExpr, used);
                Substitution actual;
                bool ok = p && p->first == addr.id;
                for (const std::string &x : used) {
                    int k = ParamIndexOf(x);
                    llvm::Optional<LinExpr> v =
                        k >= 0 && static_cast<size_t>(k) < inst.args.size() ? SymOf(inst.args[k], seg.t) : llvm::None;
                    ok = ok && v.hasValue();
                    if (v) {
                        actual.sym[x] = *v;
                    }
                }
                if (ok) {
                    sym = SymAccess{p->second, Substitute(bytesExpr, actual), cond.upto};
                }
            }
            Access(cond.kind == "W" ? "W" : "R", addr, fir::kNone, bytes, inst.loc, u, forced, true,
                   sym ? &*sym : nullptr);
        }
    }
}

// 返回新对象（SPEC-0018 §2.3、SPEC-0024 §2.2）：新的堆对象 heap@<调用点>，大小取 size（实参 p0、p1… 的式子）代入实参的下界
Value Analyzer::HeapResult(const fir::Inst &inst, const std::string &size, int64_t effDepth, const State &s)
{
    std::vector<IntVal> ints;
    for (ExprId arg : inst.args) {
        ints.push_back(EvalInt(arg, s));
    }
    LinExpr sizeExpr;
    std::string error;
    IntVal value = ParseLin(size, sizeExpr, error) ? EvalModelExpr(sizeExpr, ints) : IntVal::Top("model_expr");
    llvm::Optional<LinExpr> lo = value.Lower();
    std::string site = SiteOf(m, inst.loc);
    Value v;
    v.isPtr = true;
    v.p = Base(BaseKind::Heap, "H" + site, "heap@" + site, lo && !lo->IsUnknown() ? lo : llvm::Optional<LinExpr>());
    v.p.effDepth = effDepth;
    return v;
}

// 记录那一遍里每个 ret 的值（SPEC-0024 §2.1）
void Analyzer::RecordReturn(const fir::Inst &inst, const State &s)
{
    if (!recording) {
        return;
    }
    if (inst.val != fir::kNone) {
        returns.push_back({Eval(inst.val, s), s.guards});
    }
    // 出参：指针形参所指对象里的指针格（*ppucBuf = …，SPEC-0053；pstUe->pucRes = …，SPEC-0061）
    std::map<std::pair<int, int64_t>, Value> out;
    for (const fir::Local &l : f.locals) {
        if (l.param < 0 || !IsPtrLocal(l.id)) {
            continue;
        }
        std::string id = "P" + std::to_string(l.param);
        for (const auto &cell : s.cells) {
            if (std::get<0>(cell.first) == id && std::get<2>(cell.first) == m.target.ptr) {
                out[{l.param, std::get<1>(cell.first)}] = cell.second;
            }
        }
    }
    outs.push_back(std::move(out));
    // 写效应 field：形参所指对象的整数字段在这条路径上写了 / 没碰 / 不知道（SPEC-0073）
    std::map<std::string, FieldAt> fieldsHere;
    for (const FieldSlot &slot : FieldSlots()) {
        const std::string &id = slot.id;
        FieldAt at;
        auto cell = s.cells.find(CellKey(id, slot.off, slot.size));
        if (cell != s.cells.end()) {
            if (!cell->second.isPtr && cell->second.i.kind == IntVal::Kind::Exact &&
                !cell->second.i.value.IsUnknown()) {
                at.kind = FieldAt::Written;
                at.value = cell->second.i.value;
            }
        } else if (!Changed(s, id, slot.off, slot.size) && (slot.param >= 0 || !s.called)) {
            at.kind = FieldAt::Unchanged;  // 全局另要求没调用过可能写任意全局的函数（SPEC-0077）
        }
        fieldsHere[slot.name] = at;
    }
    fieldOuts.push_back(std::move(fieldsHere));
    llvm::Optional<int64_t> ret;
    if (inst.val != fir::kNone) {
        Value rv = Eval(inst.val, s);
        if (!rv.isPtr && rv.i.IsConst()) {
            ret = rv.i.value.Constant();
        }
    }
    outRets.push_back(ret);
}

std::vector<Analyzer::FieldSlot> Analyzer::FieldSlots() const
{
    if (slotsCache) {
        return *slotsCache;
    }
    std::vector<FieldSlot> out;
    for (const fir::Local &l : f.locals) {
        if (l.param < 0 || l.type >= m.types.size() || m.types[l.type].kind != fir::TypeKind::Ptr) {
            continue;
        }
        fir::TypeId pointee = m.types[l.type].to;
        if (pointee >= m.types.size()) {
            continue;
        }
        std::string p = "p" + std::to_string(l.param);
        const fir::Type &t = m.types[pointee];
        int64_t size = 0;
        std::string id = "P" + std::to_string(l.param);
        if (t.kind == fir::TypeKind::Int && t.width > 8 && TypeSize(m, pointee, size)) {
            out.push_back({p + "->_0", id, l.param, 0, size, pointee});  // *pK（SPEC-0071）
        } else if (t.kind == fir::TypeKind::Rec) {
            const fir::Record *r = m.FindRecord(t.record);
            for (const fir::Field &field : r != nullptr ? r->fields : std::vector<fir::Field>()) {
                if (field.type < m.types.size() && m.types[field.type].kind == fir::TypeKind::Int &&
                    TypeSize(m, field.type, size)) {
                    out.push_back({p + "->" + field.name, id, l.param, field.off, size, field.type});
                }
            }
        }
    }
    // 整数全局标量（写效应 global，SPEC-0077）：只取本函数直接引用的、与被调者写效应的目标（别的不可能写出确切值，SPEC-0080）
    std::set<std::string> keys;
    for (const fir::Expr &x : f.exprs) {
        if (x.op == ExprOp::GAddr) {
            keys.insert(x.text);
        }
    }
    std::set<std::string> names;
    for (const fir::Block &b : f.blocks) {
        for (const fir::Inst &inst : b.insts) {
            if (inst.op != fir::InstOp::Call || callees == nullptr) {
                continue;
            }
            auto callee = callees->find(inst.callee);
            if (callee == callees->end()) {
                continue;
            }
            for (const summary::Eff &e : callee->second.effs) {
                if (e.kind == "global" && e.target) {
                    names.insert(*e.target);
                }
            }
        }
    }
    std::set<const fir::Global *> picked;
    for (const std::string &key : keys) {
        if (const fir::Global *g = GlobalOf("G" + key)) {
            picked.insert(g);
        }
    }
    for (const std::string &name : names) {
        for (const fir::Global *g : GlobalsBySym(name)) {
            picked.insert(g);
        }
    }
    for (const fir::Global &g : m.globals) {  // 按表顺序输出（结果确定）
        int64_t size = 0;
        if (picked.count(&g) != 0 && g.type < m.types.size() && m.types[g.type].kind == fir::TypeKind::Int &&
            TypeSize(m, g.type, size)) {
            out.push_back({GlobalSymbol(g.key, g.name), "G" + g.key, -1, 0, size, g.type});
        }
    }
    slotsCache = out;
    return out;
}

// 每个返回值都是常数（或带情形）→ 按常数分组、组内守卫取交集：ret_cond；再加 ret_int 说明可能的返回值（D-24，SPEC-0025 §2.2）
std::vector<summary::Eff> Analyzer::CondEffects() const
{
    std::map<int64_t, std::vector<LinExpr>> byValue;
    auto add = [&](int64_t c, const std::vector<LinExpr> &guards) {
        auto it = byValue.find(c);
        if (it == byValue.end()) {
            byValue[c] = guards;
            return;
        }
        std::vector<LinExpr> common;
        for (const LinExpr &g : it->second) {
            if (llvm::is_contained(guards, g)) {
                common.push_back(g);
            }
        }
        it->second = common;
    };
    for (const auto &ret : returns) {
        const Value &v = ret.first;
        std::vector<IntCase> cases;
        if (v.isPtr || !CasesOf(v.i, ret.second, cases)) {
            return {};  // 有一条路径返回非常数：返回 c ⇒ 守卫 不成立
        }
        for (const IntCase &c : cases) {
            add(c.value, c.guards);
        }
    }
    if (byValue.size() < 2) {
        return {};
    }
    std::vector<summary::Eff> out;
    for (const auto &entry : byValue) {
        if (entry.second.empty()) {
            continue;
        }
        summary::Eff e;
        e.kind = "ret_cond";
        e.when = "ret == " + std::to_string(entry.first);
        std::vector<std::string> then;
        for (const LinExpr &g : entry.second) {
            then.push_back(ShowLe0(g));
        }
        e.then = then;
        out.push_back(e);
    }
    if (out.empty()) {
        return {};
    }
    summary::Eff range;
    range.kind = "ret_int";
    range.range =
        std::vector<std::string>{std::to_string(byValue.begin()->first), std::to_string(byValue.rbegin()->first)};
    range.bound = std::string("const");
    out.push_back(range);
    return out;
}

// 调用点代入被调者的 ret_cond / ret_int（SPEC-0025 §2.3）：返回值是常数组成的区间，带情形；守卫里被调者的形参换成实参
llvm::Optional<LinExpr> Analyzer::CallerExpr(const fir::Inst &inst, const LinExpr &calleeExpr, const State &s,
                                             const summary::Summary *callee)
{
    SymEnv env = Env(s);
    Substitution sub;
    sub.poison = true;  // 没换掉的被调者符号（含字段）不能当成调用方的同名符号（SPEC-0063 / 0072）
    if (callee != nullptr && callee->fields) {
        for (const auto &entry : *callee->fields) {
            llvm::StringRef name = entry.first;
            unsigned k = 0;
            if (!name.consume_front("p") || name.consumeInteger(10, k) || !name.startswith("->") ||
                k >= inst.args.size()) {
                continue;
            }
            Value arg = Eval(inst.args[k], s);
            if (!arg.isPtr || arg.p.kind == BaseKind::Unknown || arg.p.kind == BaseKind::Func) {
                continue;
            }
            PtrVal addr = arg.p;
            addr.off = AddVals(addr.off, IntVal::Exact(LinExpr::Const(entry.second.off)));
            Value v = LoadValue(addr, entry.second.size, fir::kNone, s);
            if (!v.isPtr && v.i.kind == IntVal::Kind::Exact && !v.i.value.IsUnknown()) {
                sub.sym[entry.first] = v.i.value;  // 调用前的当前值（实参是调用方自己的形参时就是它的 pJ->f）
            }
        }
    }
    for (size_t k = 0; k < inst.args.size(); ++k) {
        IntVal v = EvalInt(inst.args[k], s);
        if (v.kind == IntVal::Kind::Exact && Faithful(inst.args[k], v, env)) {
            sub.sym["p" + std::to_string(k)] = v.value;
        }
    }
    // 调用前改过的全局：确切值代进去，不知道的 → 换不了（SPEC-0064 §2.3）
    for (const auto &entry : GlobalsAt(s)) {
        if (entry.first == "g:*") {
            sub.poisonGlobals = true;
            continue;
        }
        LinExpr value;
        std::string bad;
        sub.sym[entry.first] = entry.second != "?" && ParseLin(entry.second, value, bad)
                                   ? value
                                   : LinExpr::Sym("_unmapped:" + entry.first);
    }
    LinExpr out = Substitute(calleeExpr, sub);
    std::set<std::string> syms;
    CollectSyms(out, syms);
    // 结果里的全局符号登记类型（Env 给类型范围；原来只在直接读全局时登记，SPEC-0077）
    for (const std::string &x : syms) {
        if (!llvm::StringRef(x).startswith("g:") || globalTypes.count(x) != 0) {
            continue;
        }
        for (const fir::Global *g : GlobalsBySym(x)) {
            if (g->type < m.types.size() && m.types[g->type].kind == fir::TypeKind::Int) {
                globalTypes[x] = fir::TypeText(m, g->type);
            }
        }
    }
    // 没换掉的被调者接口符号已改名成 _unmapped:…；结果里的 p<k> 是调用方自己的形参（实参就是它，SPEC-0092）
    bool leaked = llvm::any_of(syms, [](const std::string &x) { return llvm::StringRef(x).startswith("_unmapped:"); });
    if (out.IsUnknown() || leaked) {
        return llvm::None;
    }
    return out;
}

llvm::Optional<summary::Eff> Analyzer::ReturnPointer() const
{
    const PtrVal *base = nullptr;
    bool maybeNull = false;
    bool sameOff = true;  // 各条返回的偏移都相同
    // 偏移都是常数（或界是常数的区间）时的最小、最大（SPEC-0094）
    bool constOffs = true;
    int64_t offLo = INT64_MAX;
    int64_t offHi = INT64_MIN;
    std::vector<PtrVal> views;  // 每条返回换成"形参所指 + 偏移"的说法
    views.reserve(returns.size());
    bool winOk = true;  // 各条返回的最内层窗口相同（SPEC-0107 §2.2）
    llvm::Optional<SubWindow> win;
    for (const auto &ret : returns) {
        const Value &v = ret.first;
        bool null = (!v.isPtr && v.i.IsConst() && v.i.value.Constant() == 0) ||
                    (v.isPtr && v.p.kind == BaseKind::Unknown && v.p.why == "null");
        if (null) {
            maybeNull = true;
            continue;
        }
        // 形参所指结构体里的成员数组（ParamField）：换成"形参所指 + 成员偏移"，成员本身当最外层的窗口（成员界不丢，SPEC-0107 §2.2）
        PtrVal view = v.isPtr ? v.p : PtrVal();
        if (v.isPtr && v.p.kind == BaseKind::ParamField && v.p.hasParent && v.p.size.IsConst()) {
            SubWindow member;
            member.size = v.p.size.Constant();
            member.off = v.p.off;
            view.kind = BaseKind::Param;
            view.id = v.p.parentId;
            view.obj = v.p.parentObj;
            view.off = AddVals(IntVal::Exact(LinExpr::Const(v.p.parentOff)), v.p.off);
            view.subs.insert(view.subs.begin(), member);
        }
        views.push_back(view);
        // 形参所指对象（*pK）或形参所指结构体的指针字段在入口时的值（*pK->f）
        if (!v.isPtr || (view.kind != BaseKind::Param && view.kind != BaseKind::ParamPtr) || view.obj.size() < 2 ||
            view.obj[0] != '*') {
            return llvm::None;
        }
        if (view.subs.empty() || view.subs.back().tail || view.subs.back().off.kind != IntVal::Kind::Exact ||
            (win && !(win->size == view.subs.back().size && win->off == view.subs.back().off))) {
            winOk = false;
        } else {
            win = view.subs.back();
        }
        const PtrVal &vp = views.back();
        llvm::Optional<LinExpr> lo = vp.off.Lower();
        llvm::Optional<LinExpr> hi = vp.off.Upper();
        if (lo && hi && lo->IsConst() && hi->IsConst()) {
            offLo = std::min(offLo, lo->Constant());
            offHi = std::max(offHi, hi->Constant());
        } else {
            constOffs = false;
        }
        bool exact = vp.off.kind == IntVal::Kind::Exact && !vp.off.value.IsUnknown();
        if (base != nullptr && base->id != vp.id) {
            return llvm::None;
        }
        sameOff = sameOff && exact && (base == nullptr || base->off == vp.off);
        base = &vp;
    }
    if (base == nullptr || (!sameOff && !constOffs)) {
        return llvm::None;
    }
    summary::Eff e;
    e.kind = "ret_ptr";
    e.target = base->obj.substr(1);  // *p0 → p0；*p0->pucBuf → p0->pucBuf
    if (sameOff) {
        e.value = base->off.value.Show();
    } else {
        e.range = std::vector<std::string>{std::to_string(offLo), std::to_string(offHi)};  // SPEC-0094
    }
    if (maybeNull) {
        e.maybeNull = true;
    }
    if (winOk && win && !win->off.value.IsUnknown()) {
        e.sub = std::to_string(win->size);  // 返回的指针在这个子对象里（SPEC-0107 §2.2）
        e.subOff = win->off.value.Show();
    }
    return e;
}

Value Analyzer::RetPtrResult(const fir::Inst &inst, const summary::Eff &e, const summary::Summary &callee,
                             const State &s)
{
    Value out;
    out.isPtr = true;
    out.p = UnknownPtr("call");
    llvm::StringRef target = *e.target;
    std::pair<llvm::StringRef, llvm::StringRef> parts = target.split("->");
    llvm::StringRef param = parts.first;
    unsigned k = 0;
    if (!param.consume_front("p") || param.getAsInteger(10, k) || k >= inst.args.size()) {
        return out;
    }
    Value arg = Eval(inst.args[k], s);
    if (!arg.isPtr || arg.p.kind == BaseKind::Unknown || arg.p.kind == BaseKind::Func) {
        return out;
    }
    PtrVal base = arg.p;
    if (!parts.second.empty()) {
        // pK->f：按被调者 fields 表里的偏移从实参所指对象读字段的指针值（同调用方自己读 pst->pucBuf）
        auto info = callee.fields ? callee.fields->find(target.str())
                                  : std::map<std::string, summary::FieldInfo>::const_iterator();
        if (!callee.fields || info == callee.fields->end()) {
            return out;
        }
        PtrVal addr = arg.p;
        addr.off = AddVals(addr.off, IntVal::Exact(LinExpr::Const(info->second.off)));
        Value loaded = LoadValue(addr, m.target.ptr, inst.dst, s);
        if (!loaded.isPtr || loaded.p.kind == BaseKind::Unknown || loaded.p.kind == BaseKind::Func) {
            return loaded.isPtr ? loaded : out;
        }
        base = loaded.p;
    }
    if (!e.value) {
        // 几个常数偏移之一（SPEC-0094）
        int64_t lo = 0;
        int64_t hi = 0;
        bool ok = e.range && e.range->size() == 2 && !llvm::StringRef((*e.range)[0]).getAsInteger(10, lo) &&
                  !llvm::StringRef((*e.range)[1]).getAsInteger(10, hi) && lo <= hi;
        IntVal delta = ok ? IntVal::Range(LinExpr::Const(lo), LinExpr::Const(hi), Tag::Branch) : IntVal::Top("call");
        base.off = AddVals(base.off, delta);
        ShiftSubs(base, delta);  // 实参原有的窗口一起平移（SPEC-0107 §2.2）
        out.p = base;
        return out;
    }
    LinExpr off;
    std::string error;
    llvm::Optional<LinExpr> mine = ParseLin(*e.value, off, error) ? CallerExpr(inst, off, s, &callee) : llvm::None;
    IntVal delta = mine ? IntVal::Exact(*mine) : IntVal::Top("call");
    base.off = AddVals(base.off, delta);
    ShiftSubs(base, delta);
    // 返回的指针在被调者的一个子对象里：推一个窗口（SPEC-0107 §2.2）
    int64_t size = 0;
    LinExpr subOff;
    if (e.sub && e.subOff && !llvm::StringRef(*e.sub).getAsInteger(10, size) && size > 0 &&
        options.subobject != SubMode::Off && ParseLin(*e.subOff, subOff, error)) {
        if (llvm::Optional<LinExpr> at = CallerExpr(inst, subOff, s, &callee)) {
            SubWindow w;
            w.size = size;
            w.off = IntVal::Exact(*at);
            base.subs.push_back(w);
            if (base.subs.size() > kMaxSubs) {
                base.subs.erase(base.subs.begin());
            }
        }
    }
    out.p = base;
    return out;
}

std::vector<std::pair<CellKey, Value>> Analyzer::FieldWrites(
    const fir::Inst &inst, const summary::Summary &callee, const State &s,
    std::vector<std::pair<int64_t, std::pair<CellKey, Value>>> *conditional)
{
    std::vector<std::pair<CellKey, Value>> out;
    for (const summary::Eff &e : callee.effs) {
        bool ranged = !e.value && e.range && e.range->size() == 2;  // 几个常数之一（SPEC-0074）
        if ((e.kind != "field" && e.kind != "global") || !e.target || (!e.value && !ranged)) {
            continue;
        }
        // 只在某个返回值下成立（SPEC-0079）
        int64_t when = 0;
        if (e.when) {
            llvm::StringRef w = *e.when;
            if (conditional == nullptr || !w.consume_front("ret == ") || w.getAsInteger(10, when)) {
                continue;
            }
        }
        // 要写的格：field → 第 K 个实参所指对象里的字段；global → 本 TU 全局表里同名的整数全局（SPEC-0077）
        PtrVal addr;
        int64_t size = 0;
        if (e.kind == "global") {
            const fir::Global *global = nullptr;
            for (const fir::Global *g : GlobalsBySym(*e.target)) {
                if (g->type < m.types.size() && m.types[g->type].kind == fir::TypeKind::Int &&
                    TypeSize(m, g->type, size)) {
                    global = g;
                }
            }
            if (global == nullptr) {
                continue;
            }
            addr = Base(BaseKind::Global, "G" + global->key, *e.target, LinExpr::Const(size));
        } else {
            if (!callee.fields) {
                continue;
            }
            auto info = callee.fields->find(*e.target);
            llvm::StringRef param = llvm::StringRef(*e.target).split("->").first;
            unsigned k = 0;
            if (info == callee.fields->end() || !param.consume_front("p") || param.getAsInteger(10, k) ||
                k >= inst.args.size()) {
                continue;
            }
            Value arg = Eval(inst.args[k], s);
            if (!arg.isPtr || arg.p.kind == BaseKind::Unknown || arg.p.kind == BaseKind::Func || !arg.p.off.IsConst()) {
                continue;
            }
            addr = arg.p;
            addr.off = AddVals(addr.off, IntVal::Exact(LinExpr::Const(info->second.off)));
            size = info->second.size;
        }
        Value v;
        if (ranged) {
            int64_t lo = 0;
            int64_t hi = 0;
            if (llvm::StringRef((*e.range)[0]).getAsInteger(10, lo) ||
                llvm::StringRef((*e.range)[1]).getAsInteger(10, hi) || hi < lo) {
                continue;
            }
            v.i = IntVal::Range(LinExpr::Const(lo), LinExpr::Const(hi), Tag::Branch);
        } else {
            LinExpr calleeValue;
            std::string error;
            if (!ParseLin(*e.value, calleeValue, error)) {
                continue;
            }
            // 值正好是被调者的形参 p<k>：写第 k 个实参的值本身（确切值、区间都行），同调用方自己写 s.len = 实参（SPEC-0093）
            int k = -1;
            if (calleeValue.Constant() == 0 && calleeValue.Terms().size() == 1 &&
                calleeValue.Terms().begin()->second == 1 &&
                calleeValue.Terms().begin()->first.kind == Atom::Kind::Sym) {
                k = ParamIndexOf(calleeValue.Terms().begin()->first.name);
            }
            if (k >= 0 && static_cast<size_t>(k) < inst.args.size()) {
                IntVal arg = EvalInt(inst.args[k], s);
                if (arg.kind == IntVal::Kind::Top) {
                    continue;
                }
                v.i = arg;
            } else {
                llvm::Optional<LinExpr> mine = CallerExpr(inst, calleeValue, s, &callee);
                if (!mine) {
                    continue;  // 换不了：格照旧被 mods 清掉（不知道）
                }
                v.i = IntVal::Exact(*mine);
            }
        }
        if (e.may.getValueOr(false)) {
            // 有的路径没写：与调用前的值汇合
            Value old = LoadValue(addr, size, fir::kNone, s);
            v = JoinValue(old, v);
            llvm::Optional<LinExpr> lo = v.i.Lower();
            llvm::Optional<LinExpr> hi = v.i.Upper();
            if (v.isPtr || v.i.kind == IntVal::Kind::Top || !lo || !hi || lo->IsUnknown() || hi->IsUnknown()) {
                continue;
            }
        }
        CellKey key(addr.id, addr.off.value.Constant(), size);
        if (e.when) {
            if (v.i.kind == IntVal::Kind::Exact) {
                conditional->push_back({when, {key, v}});
            }
            continue;
        }
        out.push_back({key, v});
    }
    return out;
}

IntVal Analyzer::CondResult(const fir::Inst &inst, const std::vector<summary::Eff> &effs, const State &s,
                            const summary::Summary *callee)
{
    const summary::Eff *range = nullptr;
    for (const summary::Eff &e : effs) {
        if (e.kind == "ret_int" && e.range && e.range->size() == 2) {
            range = &e;
        }
    }
    int64_t lo = 0;
    int64_t hi = 0;
    if (range == nullptr) {
        return IntVal::Top("call");
    }
    if (llvm::StringRef((*range->range)[0]).getAsInteger(10, lo) ||
        llvm::StringRef((*range->range)[1]).getAsInteger(10, hi) || hi < lo) {
        // 符号式（SPEC-0054 §2.2）：被调者形参换成可信的确切实参；代完还含被调者形参 → 未知
        LinExpr el;
        LinExpr eh;
        std::string error;
        if (!ParseLin((*range->range)[0], el, error) || !ParseLin((*range->range)[1], eh, error)) {
            return IntVal::Top("call");
        }
        llvm::Optional<LinExpr> a = CallerExpr(inst, el, s, callee);
        llvm::Optional<LinExpr> b = CallerExpr(inst, eh, s, callee);
        if (!a || !b) {
            return IntVal::Top("call");
        }
        return *a == *b ? IntVal::Exact(*a) : IntVal::Range(*a, *b, Tag::Branch);
    }
    llvm::Optional<Tag> bound = range->bound ? ParseTag(*range->bound) : llvm::None;
    IntVal out =
        IntVal::Range(LinExpr::Const(lo), LinExpr::Const(hi), bound && *bound != Tag::Exact ? *bound : Tag::Const);
    if (hi - lo + 1 > static_cast<int64_t>(kMaxCases)) {
        return out;
    }
    // 实参：忠实的确切值才代换
    SymEnv env = Env(s);
    Substitution sub;
    for (size_t k = 0; k < inst.args.size(); ++k) {
        IntVal v = EvalInt(inst.args[k], s);
        if (v.kind == IntVal::Kind::Exact && Faithful(inst.args[k], v, env)) {
            sub.sym["p" + std::to_string(k)] = v.value;
        }
    }
    for (int64_t c = lo; c <= hi; ++c) {
        IntCase ic;
        ic.value = c;
        for (const summary::Eff &e : effs) {
            if (e.kind != "ret_cond" || !e.when || !e.then || *e.when != "ret == " + std::to_string(c)) {
                continue;
            }
            for (const std::string &cond : *e.then) {
                std::vector<LinExpr> le0;
                std::string error;
                if (!ParseCond(cond, le0, error)) {
                    continue;
                }
                for (const LinExpr &g : le0) {
                    std::set<std::string> syms;
                    CollectSyms(g, syms);
                    bool mapped = llvm::all_of(syms, [&](const std::string &x) { return sub.sym.count(x) != 0; });
                    if (mapped) {  // 换不了的原子丢掉（守卫变弱，仍然对）
                        ic.guards.push_back(Substitute(g, sub));
                    }
                }
            }
        }
        out.cases.push_back(ic);
    }
    return out;
}

// 条件边上带情形的值（SPEC-0025 §2.4）：留下满足 c op k 的情形；一个不剩 → 不可达；否则加它们守卫的交集，写回局部
bool Analyzer::FilterCases(ExprId e, const IntVal &v, fir::CmpOp op, int64_t k, State &s)
{
    auto holds = [&](int64_t c) {
        switch (op) {
            case fir::CmpOp::Eq:
                return c == k;
            case fir::CmpOp::Ne:
                return c != k;
            case fir::CmpOp::Lt:
                return c < k;
            case fir::CmpOp::Le:
                return c <= k;
            case fir::CmpOp::Gt:
                return c > k;
            case fir::CmpOp::Ge:
                return c >= k;
        }
        return true;
    };
    std::vector<IntCase> kept;
    for (const IntCase &c : v.cases) {
        if (holds(c.value)) {
            kept.push_back(c);
        }
    }
    if (kept.empty()) {
        return false;
    }
    SymEnv env = Env(s);
    for (const LinExpr &g : kept[0].guards) {
        bool common = llvm::all_of(kept, [&](const IntCase &c) { return llvm::is_contained(c.guards, g); });
        if (common && !EntailsLe0(g, s.guards, env)) {
            AddGuard(s, g);
        }
    }
    // 只在这些返回值下成立的写效应：剩下的情形都写了同一格同一个值、格从调用后没被改过 → 写回（SPEC-0079）
    for (const CaseWrite &w : kept[0].writes) {
        bool all = llvm::all_of(kept, [&](const IntCase &c) { return llvm::is_contained(c.writes, w); });
        CellKey key(w.id, w.off, w.size);
        if (all && CellText(s, key) == w.atCall) {
            Value v;
            v.i = IntVal::Exact(w.value);
            s.cells[key] = v;
        }
    }
    ExprId cur = e;
    while (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Cast && EvalInt(f.exprs[cur].a, s) == v) {
        cur = f.exprs[cur].a;
    }
    if (cur < f.exprs.size() && f.exprs[cur].op == ExprOp::Local) {
        Value out;
        if (kept.size() == 1) {
            out.i = IntVal::Exact(LinExpr::Const(kept[0].value));
        } else {
            out.i = IntVal::Range(LinExpr::Const(kept.front().value), LinExpr::Const(kept.back().value), v.tag);
            out.i.cases = kept;
        }
        s.locals[static_cast<uint32_t>(f.exprs[cur].value)] = out;
    }
    return true;
}

// 每条返回路径都是同一个堆对象（偏移 0、大小已知）或空指针常量 → ret_new（D-24）
// 返回值范围（SPEC-0054 §2.1）：各条返回都是同一个确切式子 → [E, E]；否则各条返回在自己那条路的守卫下的常数界取并；有一条求不出来 → None
llvm::Optional<summary::Eff> Analyzer::ReturnRange() const
{
    if (returns.empty()) {
        return llvm::None;
    }
    summary::Eff e;
    e.kind = "ret_int";
    const Value &first = returns[0].first;
    bool same = !first.isPtr && first.i.kind == IntVal::Kind::Exact && !first.i.value.IsUnknown();
    for (const auto &ret : returns) {
        same = same && !ret.first.isPtr && ret.first.i == first.i;
    }
    if (same) {
        e.range = std::vector<std::string>{first.i.value.Show(), first.i.value.Show()};
        e.bound = std::string(first.i.IsConst() ? "const" : "exact");
        return e;
    }
    llvm::Optional<int64_t> lo;
    llvm::Optional<int64_t> hi;
    Tag tag = Tag::None;
    for (const auto &ret : returns) {
        const Value &v = ret.first;
        llvm::Optional<LinExpr> l = v.isPtr ? llvm::None : v.i.Lower();
        llvm::Optional<LinExpr> h = v.isPtr ? llvm::None : v.i.Upper();
        if (!l || !h || l->IsUnknown() || h->IsUnknown()) {
            return llvm::None;
        }
        State t;
        t.reached = true;
        t.guards = ret.second;
        SymEnv env = Env(t);
        Limit a = env.RangeOf(*l, false);
        Limit b = env.RangeOf(*h, true);
        if (!a.known || !b.known) {
            return llvm::None;
        }
        Tag own = v.i.kind == IntVal::Kind::Exact ? (v.i.IsConst() ? Tag::Const : Tag::Exact) : v.i.tag;
        tag = WorseTag(tag, WorseTag(own, b.tag == Tag::None ? own : b.tag));
        lo = lo ? std::min(*lo, a.value) : a.value;
        hi = hi ? std::max(*hi, b.value) : b.value;
    }
    e.range = std::vector<std::string>{std::to_string(*lo), std::to_string(*hi)};
    e.bound = TagName(tag == Tag::None ? Tag::Const : tag).str();
    return e;
}

// 一组值都是同一个新对象 + 同一个常数偏移（或空指针）：(新对象, 偏移)；否则 None（SPEC-0024 / 0052 / 0053）
llvm::Optional<std::pair<const PtrVal *, int64_t>> SameNewObject(const std::vector<const Value *> &values)
{
    const PtrVal *heap = nullptr;
    int64_t off = 0;
    for (const Value *pv : values) {
        const Value &v = *pv;
        bool null = (!v.isPtr && v.i.IsConst() && v.i.value.Constant() == 0) ||
                    (v.isPtr && v.p.kind == BaseKind::Unknown && v.p.why == "null");
        if (null) {
            continue;
        }
        bool fresh =
            v.isPtr && v.p.kind == BaseKind::Heap && v.p.hasSize && v.p.off.IsConst() && v.p.off.value.Constant() >= 0;
        if (!fresh || (heap != nullptr && (heap->id != v.p.id || off != v.p.off.value.Constant()))) {
            return llvm::None;
        }
        heap = &v.p;
        off = v.p.off.value.Constant();
    }
    if (heap == nullptr) {
        return llvm::None;
    }
    return std::make_pair(heap, off);
}

std::vector<summary::Eff> Analyzer::Effects() const
{
    std::vector<summary::Eff> out = CondEffects();
    if (out.empty()) {
        // 返回新对象（可带常数偏移：带管理头的分配器，SPEC-0052）
        std::vector<const Value *> values;
        for (const auto &ret : returns) {
            values.push_back(&ret.first);
        }
        if (llvm::Optional<std::pair<const PtrVal *, int64_t>> hit = SameNewObject(values)) {
            summary::Eff e;
            e.kind = "ret_new";
            e.size = (hit->first->size - LinExpr::Const(hit->second)).Show();  // 从返回的指针起还剩的字节
            e.maybeNull = true;
            e.effDepth = hit->first->effDepth + 1;
            out.push_back(e);
        } else if (llvm::Optional<summary::Eff> ptr = ReturnPointer()) {
            out.push_back(*ptr);  // 返回实参指针加偏移（取子缓冲区，SPEC-0070）
        } else if (llvm::Optional<summary::Eff> range = ReturnRange()) {
            out.push_back(*range);  // 返回值范围（取值 / 截断函数，SPEC-0054）
        }
    }
    // 出参写回的新对象：每条返回路径都写了 *pK 或 pK->f（SPEC-0053 / 0061）；有一条没写就不记
    for (const auto &first : outs.empty() ? std::map<std::pair<int, int64_t>, Value>() : outs[0]) {
        std::pair<int, int64_t> at = first.first;
        std::vector<const Value *> values;
        for (const auto &o : outs) {
            auto it = o.find(at);
            if (it == o.end()) {
                values.clear();
                break;
            }
            values.push_back(&it->second);
        }
        llvm::Optional<std::pair<const PtrVal *, int64_t>> hit;
        if (!values.empty()) {
            hit = SameNewObject(values);
        }
        std::string target = "p" + std::to_string(at.first);
        if (hit && at.second != 0) {
            // 字段：形参所指记录里偏移 off、指针大小的字段；写进 fields 表，调用点按它找偏移（SPEC-0061 §2.1）
            llvm::Optional<uint32_t> rec = PointeeRecord(at.first);
            const fir::Record *r = rec ? m.FindRecord(*rec) : nullptr;
            std::string name;
            for (const fir::Field &field : r != nullptr ? r->fields : std::vector<fir::Field>()) {
                int64_t size = 0;
                if (field.off == at.second && field.type < m.types.size() &&
                    m.types[field.type].kind == fir::TypeKind::Ptr && TypeSize(m, field.type, size) &&
                    size == m.target.ptr) {
                    name = field.name;
                    summary::FieldInfo info;
                    info.off = field.off;
                    info.size = size;
                    info.type = fir::TypeText(m, field.type);
                    fields[target + "->" + name] = info;
                    break;
                }
            }
            if (name.empty()) {
                continue;  // 经类型转换写的：不记（调用方照旧未知，可靠）
            }
            target += "->" + name;
        }
        if (hit) {
            summary::Eff e;
            e.kind = "out_new";
            e.target = target;  // SPEC-0053 / 0061
            e.size = (hit->first->size - LinExpr::Const(hit->second)).Show();
            e.maybeNull = true;
            e.effDepth = hit->first->effDepth + 1;
            out.push_back(e);
        }
    }
    // 写效应 field：每条路径要么写了同一个确切值、要么没碰；至少一条写了（SPEC-0073）
    for (const FieldSlot &slot : fieldOuts.empty() ? std::vector<FieldSlot>() : FieldSlots()) {
        llvm::Optional<LinExpr> value;
        bool same = true;    // 写的值都相同
        bool consts = true;  // 写的值都是常数（不同时记区间，SPEC-0074）
        int64_t lo = 0;
        int64_t hi = 0;
        bool may = false;
        bool ok = true;
        for (const auto &path : fieldOuts) {
            auto it = path.find(slot.name);
            if (it == path.end() || it->second.kind == FieldAt::Unknown) {
                ok = false;
                break;
            }
            if (it->second.kind == FieldAt::Unchanged) {
                may = true;
                continue;
            }
            const LinExpr &v = it->second.value;
            if (v.IsConst()) {
                lo = value ? std::min(lo, v.Constant()) : v.Constant();
                hi = value ? std::max(hi, v.Constant()) : v.Constant();
            } else {
                consts = false;
            }
            same = same && (!value || *value == v);
            value = v;
        }
        bool emitted = false;
        auto info = [&]() {
            if (slot.param >= 0) {
                summary::FieldInfo fi;
                fi.off = slot.off;
                fi.size = slot.size;
                fi.type = fir::TypeText(m, slot.type);
                fields[slot.name] = fi;
            }
        };
        if (ok && value && (same || consts)) {
            summary::Eff e;
            e.kind = slot.param >= 0 ? "field" : "global";  // SPEC-0073 / 0077
            e.target = slot.name;
            if (same) {
                e.value = value->Show();
            } else {
                e.range = std::vector<std::string>{std::to_string(lo), std::to_string(hi)};  // SPEC-0074
            }
            if (may) {
                e.may = true;
            }
            info();
            out.push_back(e);
            emitted = same && !may;
        }
        // 无条件规则下没有确切值：按返回常数分组，返回 c 的每条路径都写了同一个确切值 → when: ret == c（SPEC-0079）
        if (emitted || outRets.size() != fieldOuts.size() ||
            !llvm::all_of(outRets, [](const llvm::Optional<int64_t> &r) { return r.hasValue(); })) {
            continue;
        }
        std::map<int64_t, llvm::Optional<LinExpr>> groups;
        std::set<int64_t> broken;
        for (size_t i = 0; i < fieldOuts.size(); ++i) {
            int64_t c = *outRets[i];
            auto it = fieldOuts[i].find(slot.name);
            if (it == fieldOuts[i].end() || it->second.kind != FieldAt::Written) {
                broken.insert(c);
                continue;
            }
            llvm::Optional<LinExpr> &g = groups[c];
            if (g && !(*g == it->second.value)) {
                broken.insert(c);
            }
            g = it->second.value;
        }
        for (const auto &g : groups) {
            if (broken.count(g.first) != 0 || !g.second) {
                continue;
            }
            summary::Eff e;
            e.kind = slot.param >= 0 ? "field" : "global";
            e.target = slot.name;
            e.value = g.second->Show();
            e.when = "ret == " + std::to_string(g.first);
            info();
            out.push_back(e);
        }
    }
    return out;
}

// 指针实参指向局部 / 全局结构体变量（偏移 0）：内存格里有值的标量 / 指针字段（SPEC-0026）
std::map<std::string, summary::ArgValue> Analyzer::ArgFields(const PtrVal &p, const State &s, size_t k,
                                                             std::map<std::string, summary::Local> *locals) const
{
    std::map<std::string, summary::ArgValue> out;
    if (!p.off.IsConst() || p.off.value.Constant() != 0 || p.id.size() < 2) {
        return out;
    }
    fir::TypeId type = fir::kNone;
    bool alias = false;  // 自己的形参所指的结构体（实参写 alias pK）：只写本函数改过的字段（SPEC-0026）
    if (p.kind == BaseKind::Param && p.id[0] == 'P') {
        int k = static_cast<int>(std::strtol(p.id.c_str() + 1, nullptr, 10));
        for (const fir::Local &l : f.locals) {
            if (l.param == k && l.type < m.types.size() && m.types[l.type].kind == fir::TypeKind::Ptr) {
                type = m.types[l.type].to;
            }
        }
        alias = true;
    } else if (p.kind == BaseKind::Local && p.id[0] == 'L') {
        const fir::Local *l = f.FindLocal(static_cast<uint32_t>(std::strtoul(p.id.c_str() + 1, nullptr, 10)));
        type = l != nullptr ? l->type : fir::kNone;
    } else if (p.kind == BaseKind::Global && p.id[0] == 'G') {
        if (const fir::Global *g = GlobalOf(p.id)) {
            type = g->type;
        }
    }
    // 结构体：各字段；整数（或整数数组的第 0 个元素）：伪字段 _0（*pK，SPEC-0071）
    std::vector<fir::Field> list;
    if (type < m.types.size() && m.types[type].kind == fir::TypeKind::Rec) {
        const fir::Record *r = m.FindRecord(m.types[type].record);
        if (r != nullptr) {
            list = r->fields;
        }
    } else if (type < m.types.size()) {
        fir::TypeId elem = m.types[type].kind == fir::TypeKind::Arr ? m.types[type].to : type;
        if (elem < m.types.size() && m.types[elem].kind == fir::TypeKind::Int && m.types[elem].width > 8) {
            fir::Field scalar;
            scalar.name = "_0";
            scalar.off = 0;
            scalar.type = elem;
            list.push_back(scalar);
        }
    }
    for (const fir::Field &field : list) {
        int64_t size = 0;
        if (field.type >= m.types.size() || !TypeSize(m, field.type, size)) {
            continue;
        }
        fir::TypeKind kind = m.types[field.type].kind;
        if (kind != fir::TypeKind::Int && kind != fir::TypeKind::Ptr) {
            continue;  // 数组、嵌套结构体不写
        }
        // 别名：没改过的字段照旧按别名代换成入口值；改过的写当前值，不知道当前值就写成未知
        bool changed = Changed(s, p.id, field.off, size);
        if (alias && !changed) {
            continue;
        }
        std::string unknown = "_unknown_" + field.name;
        auto unknownArg = [&]() {
            summary::ArgValue u;
            if (kind == fir::TypeKind::Ptr) {
                u.kind = summary::ArgValue::Kind::Ptr;
                u.ptr = std::make_shared<summary::PtrArg>();
                u.ptr->obj = "local:" + unknown;
                u.ptr->have = unknown;
            } else {
                u.kind = summary::ArgValue::Kind::Expr;
                u.expr = unknown;
            }
            return u;
        };
        auto cell = s.cells.find(CellKey(p.id, field.off, size));
        if (cell == s.cells.end()) {
            if (alias) {
                out[field.name] = unknownArg();
            }
            continue;
        }
        const Value &v = cell->second;
        summary::ArgValue a;
        if (v.isPtr) {
            llvm::Optional<LinExpr> hi = v.p.off.Upper();
            if (v.p.kind == BaseKind::Unknown || v.p.kind == BaseKind::Func || !v.p.hasSize || !hi) {
                if (alias) {
                    out[field.name] = unknownArg();
                }
                continue;
            }
            a.kind = summary::ArgValue::Kind::Ptr;
            a.ptr = std::make_shared<summary::PtrArg>();
            a.ptr->obj = v.p.obj;
            a.ptr->have = (v.p.size - *hi).Show();
            a.ptr->back = BackText(v.p);  // SPEC-0039
            a.ptr->sub = SubRest(v.p);    // 字段里的指针也记内层剩余（SPEC-0107 §2.1）
            a.ptr->subBack = SubBack(v.p);
            if (v.p.kind == BaseKind::Heap && v.p.effDepth > 0) {
                a.ptr->effDepth = v.p.effDepth;
            }
        } else if (v.i.kind == IntVal::Kind::Exact) {
            a.kind = summary::ArgValue::Kind::Expr;
            a.expr = v.i.value.Show();
        } else if (locals != nullptr && v.i.kind == IntVal::Kind::Range && IsCodeTag(v.i.tag) && v.i.Lower() &&
                   v.i.Upper() && !v.i.Lower()->IsUnknown() && !v.i.Upper()->IsUnknown()) {
            // 代码给出的区间：调用点局部符号 + locals 里的区间，判定时按调用方局部换成区间（SPEC-0069）；
            // 只有类型范围的（data / type / widen）照旧写成未知
            std::string name = "_fld" + std::to_string(k) + "_" + field.name;
            (*locals)[name] = {v.i.Lower()->Show(), v.i.Upper()->Show(), TagName(v.i.tag).str()};
            a.kind = summary::ArgValue::Kind::Expr;
            a.expr = name;
        } else {
            if (alias) {
                out[field.name] = unknownArg();
            }
            continue;
        }
        out[field.name] = a;
    }
    return out;
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
        if (v.isPtr) {
            for (const SubWindow &w : v.p.subs) {
                text += "{sub " + std::to_string(w.size) + "@" + w.off.Show() + "}";  // 内层窗口（SPEC-0104）
            }
        }
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
    if (!s.rels.empty()) {
        // 局部关系（SPEC-0108）：局部符号写成 $名字（指针局部的偏移 $名字.off）
        static llvm::Regex sym("\\$([lo])([0-9]+)");
        out += " rels:";
        for (size_t i = 0; i < s.rels.size(); ++i) {
            std::string text = ShowLe0(s.rels[i]);
            llvm::SmallVector<llvm::StringRef, 3> m;
            std::string shown;
            llvm::StringRef rest = text;
            while (sym.match(rest, &m)) {
                size_t at = m[0].data() - rest.data();
                shown += rest.substr(0, at).str();
                unsigned id = 0;
                m[2].getAsInteger(10, id);
                const fir::Local *l = f.FindLocal(id);
                shown += "$" + (l != nullptr ? l->name : m[2].str()) + (m[1] == "o" ? ".off" : "");
                rest = rest.drop_front(at + m[0].size());
            }
            out += (i == 0 ? " " : ", ") + shown + rest.str();
        }
    }
    return out;
}

}  // namespace

FunctionFacts AnalyzeFunction(const fir::Module &m, const fir::Function &f, std::vector<std::string> *stateTrace,
                              const models::ModelTable &models, const std::map<std::string, summary::Summary> *callees,
                              const std::map<std::string, int64_t> *globalSizes, const Options &options)
{
    Analyzer a(m, f, models);
    a.options = options;
    a.trace = stateTrace;
    a.callees = callees;
    a.globalSizes = globalSizes;
    FunctionFacts facts = a.Run();
    // CharAlias=verify（SPEC-0106 §2.2）：假设只在起作用时保留——按 true 再分析一遍，摘要相同（别名改变不了任何结果）就丢掉。
    // 不比"用到了哪些形参字段"（fields）：它只是符号的类型登记，按 true 读到的不是入口值就不登记
    if (options.charVerify && !facts.assumes.empty()) {
        Analyzer b(m, f, models);
        b.options = options;
        b.options.charVerify = false;
        b.options.charAlias = true;
        b.callees = callees;
        b.globalSizes = globalSizes;
        summary::Summary mine = BuildSummary(m, f, facts);
        mine.assumes.clear();
        mine.fields.reset();
        summary::Summary aliased = BuildSummary(m, f, b.Run());
        aliased.fields.reset();
        std::string x;
        std::string y;
        llvm::raw_string_ostream xs(x);
        llvm::raw_string_ostream ys(y);
        xs << summary::SummaryToJson(mine);
        ys << summary::SummaryToJson(aliased);
        if (xs.str() == ys.str()) {
            facts.assumes.clear();
        }
    }
    return facts;
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
    s.effs = facts.effs;
    s.opens = facts.opens;
    s.mods.assign(facts.mods.begin(), facts.mods.end());           // SPEC-0045
    s.assumes.assign(facts.assumes.begin(), facts.assumes.end());  // SPEC-0106
    return s;
}

}  // namespace interp
}  // namespace oobsum
