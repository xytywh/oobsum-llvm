//===-- Range.cpp - Numeric ranges, guards and entailment -------*- C++ -*-===//
//
// 见 Range.h。语义逐项对照 oobsum/cases/summaries/replay.py（SPEC-0011）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Range.h"

#include "llvm/Support/MathExtras.h"

namespace oobsum {

namespace {

const char *const kTagNames[] = {"",     "type", "widen",  "data", "check", "guard",
                                 "mask", "loop", "branch", "ret",  "const", "exact"};

// ⌊a / b⌋（b > 0 或 b < 0），溢出返回 false
bool FloorDiv(int64_t a, int64_t b, int64_t &out)
{
    if (b == 0 || (a == INT64_MIN && b == -1)) {
        return false;
    }
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) {
        --q;
    }
    out = q;
    return true;
}

// ⌈a / b⌉ = −⌊−a / b⌋
bool CeilDiv(int64_t a, int64_t b, int64_t &out)
{
    if (a == INT64_MIN) {
        return false;
    }
    int64_t q = 0;
    if (!FloorDiv(-a, b, q) || q == INT64_MIN) {
        return false;
    }
    out = -q;
    return true;
}

}  // namespace

llvm::StringRef TagName(Tag tag)
{
    return kTagNames[static_cast<unsigned>(tag)];
}

llvm::Optional<Tag> ParseTag(llvm::StringRef name)
{
    for (unsigned i = 1; i < sizeof(kTagNames) / sizeof(kTagNames[0]); ++i) {
        if (name == kTagNames[i]) {
            return static_cast<Tag>(i);
        }
    }
    return llvm::None;
}

bool IsCodeTag(Tag tag)
{
    switch (tag) {
        case Tag::Exact:
        case Tag::Const:
        case Tag::Check:
        case Tag::Mask:
        case Tag::Branch:
        case Tag::Loop:
        case Tag::Ret:
        case Tag::Guard:
            return true;
        default:
            return false;
    }
}

Tag WorseTag(Tag a, Tag b)
{
    if (a == Tag::None) {
        return b;
    }
    if (b == Tag::None) {
        return a;
    }
    return a < b ? a : b;
}

bool TypeRangeOf(llvm::StringRef typeName, int64_t &lo, int64_t &hi)
{
    struct Entry {
        const char *name;
        int64_t lo;
        int64_t hi;
    };
    static const Entry table[] = {{"UINT8", 0, 0xFF},          {"UINT16", 0, 0xFFFF},
                                  {"UINT32", 0, 0xFFFFFFFF},    {"size_t", 0, 0xFFFFFFFF},
                                  {"CHAR", -0x80, 0x7F},        {"INT32", -0x80000000LL, 0x7FFFFFFF}};
    for (const Entry &e : table) {
        if (typeName == e.name) {
            lo = e.lo;
            hi = e.hi;
            return true;
        }
    }
    return false;
}

void SymEnv::SetTypeRange(llvm::StringRef name, int64_t lowest, int64_t highest)
{
    lo[name.str()] = Limit::Of(lowest, Tag::Type);
    hi[name.str()] = Limit::Of(highest, Tag::Type);
}

Limit SymEnv::Lower(llvm::StringRef name) const
{
    auto it = lo.find(name.str());
    return it == lo.end() ? Limit() : it->second;
}

Limit SymEnv::Upper(llvm::StringRef name) const
{
    auto it = hi.find(name.str());
    return it == hi.end() ? Limit() : it->second;
}

void SymEnv::AddGuard(const LinExpr &le0, Tag tag)
{
    if (le0.IsUnknown() || le0.Terms().size() != 1) {
        return;
    }
    const Atom &atom = le0.Terms().begin()->first;
    int64_t c = le0.Terms().begin()->second;
    if (atom.kind != Atom::Kind::Sym) {
        return;  // replay.py 也记，但 bound() 只读符号的界，等于不记
    }
    int64_t k = le0.Constant();
    if (c > 0) {
        // c·s + k ≤ 0 → s ≤ ⌊−k / c⌋
        int64_t v = 0;
        if (k == INT64_MIN || !FloorDiv(-k, c, v)) {
            return;
        }
        auto it = hi.find(atom.name);
        if (it == hi.end() || v < it->second.value || it->second.tag == Tag::Type) {
            hi[atom.name] = Limit::Of(v, tag);
        }
    } else {
        // c·s + k ≤ 0（c < 0）→ s ≥ ⌈k / −c⌉
        int64_t v = 0;
        if (c == INT64_MIN || !CeilDiv(k, -c, v)) {
            return;
        }
        auto it = lo.find(atom.name);
        if (it == lo.end() || v > it->second.value || it->second.tag == Tag::Type) {
            lo[atom.name] = Limit::Of(v, tag);
        }
    }
}

Limit SymEnv::Bound(const Atom &atom, bool upper) const
{
    switch (atom.kind) {
        case Atom::Kind::Ext:
            return upper ? Limit() : Limit::Of(0, Tag::Type);
        case Atom::Kind::Sym:
            return upper ? Upper(atom.name) : Lower(atom.name);
        case Atom::Kind::Div: {
            Limit inner = RangeOf(*atom.a, upper);
            int64_t q = 0;
            if (!inner.known || !FloorDiv(inner.value, atom.n, q)) {
                return Limit();
            }
            return Limit::Of(q, inner.tag);
        }
        case Atom::Kind::Mod:
            return upper ? Limit::Of(atom.n - 1, Tag::Mask) : Limit::Of(0, Tag::Mask);
        case Atom::Kind::Wrap: {
            Limit l = RangeOf(*atom.a, false);
            Limit h = RangeOf(*atom.a, true);
            int64_t top = (int64_t(1) << atom.n) - 1;
            if (l.known && l.value >= 0 && h.known && h.value <= top) {
                return Limit::Of(upper ? h.value : l.value, h.tag);  // 标签都取上界的（同 replay.py）
            }
            return upper ? Limit::Of(top, Tag::Type) : Limit::Of(0, Tag::Type);
        }
        case Atom::Kind::Mul: {
            Limit x = RangeOf(*atom.a, upper);
            Limit y = RangeOf(*atom.b, upper);
            int64_t p = 0;
            if (!x.known || !y.known || llvm::MulOverflow(x.value, y.value, p)) {
                return Limit();
            }
            return Limit::Of(p, x.tag != Tag::None ? x.tag : y.tag);
        }
    }
    return Limit();
}

Limit SymEnv::RangeOf(const LinExpr &l, bool upper) const
{
    if (l.IsUnknown()) {
        return Limit();
    }
    int64_t v = l.Constant();
    Tag tag = Tag::None;
    for (const auto &term : l.Terms()) {
        int64_t c = term.second;
        Limit b = Bound(term.first, c > 0 ? upper : !upper);
        int64_t product = 0;
        if (!b.known || llvm::MulOverflow(c, b.value, product) || llvm::AddOverflow(v, product, v)) {
            return Limit();
        }
        tag = WorseTag(tag, b.tag);
    }
    return Limit::Of(v, tag);
}

bool EntailsLe0(const LinExpr &d, const std::vector<LinExpr> &guards, const SymEnv &env)
{
    Limit direct = env.RangeOf(d, true);
    if (direct.known && direct.value <= 0) {
        return true;
    }
    for (const LinExpr &g : guards) {
        for (const auto &term : d.Terms()) {
            int64_t c = term.second;
            int64_t b = g.Coeff(term.first);
            if (c <= 0 || b <= 0) {
                continue;
            }
            // replay.py：d − (c/b)·g；乘以 b > 0 不改变符号，这里全用整数（SPEC-0011 §2.4）
            LinExpr rest = d.Scale(b) - g.Scale(c);
            Limit r = env.RangeOf(rest, true);
            if (r.known && r.value <= 0) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace oobsum
