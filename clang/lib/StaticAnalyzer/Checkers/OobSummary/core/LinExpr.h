//===-- LinExpr.h - Linear expressions over interface symbols ---*- C++ -*-===//
//
// 线性式（SPEC-0010；06- D-20、D-21、A-10、A-61）：常数 k + Σ c·原子，c 为 int64。原子是符号（p0、p1->ulLen、g:g_x、调用方局部名）、
// ext(…)，或不透明项 div(e, n) / mod(e, n) / wrapN(e) / mul(a, b)。与 replay.py 的 Lin / Parser / C / subst 同语义（A-92）：
//   * 规范形式：pN → pN->… → ext(…) → g:… → 其它符号 → 不透明项，常数最后；两条式子相等当且仅当规范形式相同。
//   * 不透明项构造时内层是常数就当场算（div / mod 向下取整）；显式的 mul(2, p0) 保持不透明，2*p0 是系数。
//   * 溢出 int64 → 整个式子未知（打印 ?，只导向 SUSPECT）。
// 不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_LINEXPR_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_LINEXPR_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace oobsum {

class LinExpr;

struct Atom {
    enum class Kind : uint8_t { Sym, Ext, Div, Mod, Wrap, Mul };

    Kind kind = Kind::Sym;
    std::string name;                  // Sym / Ext
    int64_t n = 0;                     // Div / Mod：除数；Wrap：位数
    std::shared_ptr<const LinExpr> a;  // Div / Mod / Wrap / Mul
    std::shared_ptr<const LinExpr> b;  // Mul

    // 规范排序键（D-21）：组、形参下标、文本
    int group = 0;
    int64_t index = 0;
    std::string text;

    bool operator<(const Atom &o) const
    {
        return std::tie(group, index, text) < std::tie(o.group, o.index, o.text);
    }
    bool operator==(const Atom &o) const
    {
        return group == o.group && index == o.index && text == o.text;
    }
    bool IsOpaque() const
    {
        return kind != Kind::Sym && kind != Kind::Ext;
    }
};

class LinExpr {
public:
    LinExpr() = default;  // 0

    static LinExpr Const(int64_t k);
    static LinExpr Sym(llvm::StringRef name);
    static LinExpr Ext(llvm::StringRef name);
    // 不透明项：内层是常数就当场算出来（同 replay.py 的 mk_opaque）；n 为 0 时未知
    static LinExpr Div(const LinExpr &a, int64_t n);
    static LinExpr Mod(const LinExpr &a, int64_t n);
    static LinExpr Wrap(unsigned bits, const LinExpr &a);
    static LinExpr Mul(const LinExpr &a, const LinExpr &b);
    static LinExpr Unknown();
    // 系数为 1 的单项
    static LinExpr OfAtom(const Atom &atom);

    bool IsUnknown() const
    {
        return unknown;
    }
    bool IsConst() const
    {
        return !unknown && t.empty();
    }
    int64_t Constant() const
    {
        return k;
    }
    const std::map<Atom, int64_t> &Terms() const
    {
        return t;
    }
    int64_t Coeff(const Atom &atom) const;

    LinExpr operator+(const LinExpr &o) const;
    LinExpr operator-(const LinExpr &o) const;
    LinExpr operator-() const;
    LinExpr Scale(int64_t c) const;
    bool operator==(const LinExpr &o) const;
    bool operator!=(const LinExpr &o) const
    {
        return !(*this == o);
    }

    // D-21 的规范打印
    std::string Show() const;

private:
    static Atom OpaqueAtom(Atom::Kind kind, const LinExpr &a, const LinExpr *b, int64_t n);

    int64_t k = 0;
    std::map<Atom, int64_t> t;
    bool unknown = false;
};

// D-21 的语法；失败时返回 false 与带位置的原因（不 assert，A-96）
bool ParseLin(llvm::StringRef text, LinExpr &out, std::string &error);
// "a <= b" 等 → 若干个 L ≤ 0（同 replay.py 的 C）
bool ParseCond(llvm::StringRef text, std::vector<LinExpr> &le0, std::string &error);
// L ≤ 0 打印成可读的不等式（同 replay.py 的 show_atom_le0）
std::string ShowLe0(const LinExpr &g);

// 代换（A-61，同 replay.py 的 subst）：sym 名 → 式子；ext 名 → 式子；alias：被调者形参名 → 调用方形参名（p0->f → p2->f）
struct Substitution {
    std::map<std::string, LinExpr> sym;
    std::map<std::string, LinExpr> ext;
    std::map<std::string, std::string> alias;
};
LinExpr Substitute(const LinExpr &e, const Substitution &m);

}  // namespace oobsum

#endif
