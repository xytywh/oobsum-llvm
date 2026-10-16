//===-- Range.cpp - Numeric ranges, guards and entailment -------*- C++ -*-===//
//
// 见 Range.h。语义逐项对照 oobsum/cases/summaries/replay.py（SPEC-0011）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Range.h"

#include "llvm/Support/MathExtras.h"

namespace oobsum {

namespace {

const char *const kTagNames[] = {"",     "type", "widen",  "data", "model", "check", "guard",
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
        case Tag::Model:
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
    static const Entry table[] = {{"UINT8", 0, 0xFF},        {"UINT16", 0, 0xFFFF},
                                  {"UINT32", 0, 0xFFFFFFFF}, {"size_t", 0, 0xFFFFFFFF},
                                  {"CHAR", -0x80, 0x7F},     {"INT32", -0x80000000LL, 0x7FFFFFFF}};
    for (const Entry &e : table) {
        if (typeName == e.name) {
            lo = e.lo;
            hi = e.hi;
            return true;
        }
    }
    // FIR 的类型写法（引擎产出的摘要用它，SPEC-0015 §2.2）：u8 / u16 / u32 / i8 / i16 / i32
    unsigned width = 0;
    if (typeName.size() >= 2 && (typeName[0] == 'u' || typeName[0] == 'i') &&
        !typeName.drop_front().getAsInteger(10, width) && (width == 8 || width == 16 || width == 32)) {
        bool isSigned = typeName[0] == 'i';
        lo = isSigned ? -(int64_t(1) << (width - 1)) : 0;
        hi = isSigned ? (int64_t(1) << (width - 1)) - 1 : (int64_t(1) << width) - 1;
        return true;
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

std::pair<Limit, Limit> SymEnv::BoundBoth(const Atom &atom) const
{
    switch (atom.kind) {
        case Atom::Kind::Ext:
        case Atom::Kind::BExt:  // 字节数：不小于 0（SPEC-0039）
            return {Limit::Of(0, Tag::Type), Limit()};
        case Atom::Kind::Sym:
            return {Lower(atom.name), Upper(atom.name)};
        case Atom::Kind::Div: {
            std::pair<Limit, Limit> inner = RangeBoth(*atom.a);
            auto div = [&](const Limit &x) {
                int64_t q = 0;
                if (!x.known || !FloorDiv(x.value, atom.n, q)) {
                    return Limit();
                }
                return Limit::Of(q, x.tag);
            };
            return {div(inner.first), div(inner.second)};
        }
        case Atom::Kind::Mod:
            return {Limit::Of(0, Tag::Mask), Limit::Of(atom.n - 1, Tag::Mask)};
        case Atom::Kind::Wrap: {
            std::pair<Limit, Limit> inner = RangeBoth(*atom.a);
            const Limit &l = inner.first;
            const Limit &h = inner.second;
            int64_t top = (int64_t(1) << atom.n) - 1;
            if (l.known && l.value >= 0 && h.known && h.value <= top) {
                return {Limit::Of(l.value, h.tag), Limit::Of(h.value, h.tag)};  // 标签都取上界的（同 replay.py）
            }
            return {Limit::Of(0, Tag::Type), Limit::Of(top, Tag::Type)};
        }
        case Atom::Kind::Mul: {
            std::pair<Limit, Limit> x = RangeBoth(*atom.a);
            std::pair<Limit, Limit> y = RangeBoth(*atom.b);
            auto mul = [](const Limit &a, const Limit &b) {
                int64_t p = 0;
                if (!a.known || !b.known || llvm::MulOverflow(a.value, b.value, p)) {
                    return Limit();
                }
                return Limit::Of(p, a.tag != Tag::None ? a.tag : b.tag);
            };
            return {mul(x.first, y.first), mul(x.second, y.second)};
        }
    }
    return {Limit(), Limit()};
}

Limit SymEnv::Bound(const Atom &atom, bool upper) const
{
    std::pair<Limit, Limit> both = BoundBoth(atom);
    return upper ? both.second : both.first;
}

std::pair<Limit, Limit> SymEnv::RangeBoth(const LinExpr &l) const
{
    if (l.IsUnknown()) {
        return {Limit(), Limit()};
    }
    int64_t lo = l.Constant();
    int64_t hi = l.Constant();
    bool loOk = true;
    bool hiOk = true;
    Tag loTag = Tag::None;
    Tag hiTag = Tag::None;
    auto add = [](int64_t c, const Limit &b, int64_t &acc, bool &ok, Tag &tag) {
        int64_t product = 0;
        if (!ok || !b.known || llvm::MulOverflow(c, b.value, product) || llvm::AddOverflow(acc, product, acc)) {
            ok = false;
            return;
        }
        tag = WorseTag(tag, b.tag);
    };
    for (const auto &term : l.Terms()) {
        int64_t c = term.second;
        std::pair<Limit, Limit> b = BoundBoth(term.first);
        // 系数为正：下界用原子的下界、上界用上界；为负反过来（同原来的 RangeOf）
        add(c, c > 0 ? b.first : b.second, lo, loOk, loTag);
        add(c, c > 0 ? b.second : b.first, hi, hiOk, hiTag);
        if (!loOk && !hiOk) {
            break;
        }
    }
    return {loOk ? Limit::Of(lo, loTag) : Limit(), hiOk ? Limit::Of(hi, hiTag) : Limit()};
}

Limit SymEnv::RangeOf(const LinExpr &l, bool upper) const
{
    std::pair<Limit, Limit> both = RangeBoth(l);
    return upper ? both.second : both.first;
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
