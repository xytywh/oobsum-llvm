//===-- LinExpr.cpp - Linear expressions over interface symbols -*- C++ -*-===//
//
// 见 LinExpr.h。语义逐项对照 oobsum/cases/summaries/replay.py（SPEC-0010）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/LinExpr.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cctype>

namespace oobsum {

namespace {

// Python 的 // 与 %：向下取整，余数与除数同号（SPEC-0010 §2.1）；溢出返回 false
bool FloorDiv(int64_t a, int64_t n, int64_t &out)
{
    if (n == 0 || (a == INT64_MIN && n == -1)) {
        return false;
    }
    int64_t q = a / n;
    if (a % n != 0 && ((a < 0) != (n < 0))) {
        --q;
    }
    out = q;
    return true;
}

bool FloorMod(int64_t a, int64_t n, int64_t &out)
{
    if (n == 0) {
        return false;
    }
    if (n == -1) {
        out = 0;
        return true;
    }
    int64_t m = a % n;
    if (m != 0 && ((m < 0) != (n < 0))) {
        m += n;
    }
    out = m;
    return true;
}

// 规范排序键（同 replay.py 的 atom_order）
void SetSymKey(Atom &atom)
{
    llvm::StringRef name = atom.name;
    atom.text = atom.name;
    atom.index = 0;
    if (name.size() > 1 && name[0] == 'p' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
        llvm::StringRef digits = name.drop_front().take_while(isDigit);
        llvm::StringRef rest = name.drop_front(1 + digits.size());
        unsigned long long index = 0;
        if (!digits.getAsInteger(10, index) && (rest.empty() || rest.startswith("->"))) {
            atom.group = rest.empty() ? 0 : 1;
            atom.index = static_cast<int64_t>(index > INT64_MAX ? INT64_MAX : index);
            return;
        }
    }
    atom.group = name.startswith("g:") ? 3 : 4;
}

}  // namespace

LinExpr LinExpr::Const(int64_t k)
{
    LinExpr e;
    e.k = k;
    return e;
}

LinExpr LinExpr::Unknown()
{
    LinExpr e;
    e.unknown = true;
    return e;
}

LinExpr LinExpr::OfAtom(const Atom &atom)
{
    LinExpr e;
    e.t.emplace(atom, 1);
    return e;
}

LinExpr LinExpr::Sym(llvm::StringRef name)
{
    Atom atom;
    atom.kind = Atom::Kind::Sym;
    atom.name = name.str();
    SetSymKey(atom);
    return OfAtom(atom);
}

LinExpr LinExpr::Ext(llvm::StringRef name)
{
    Atom atom;
    atom.kind = Atom::Kind::Ext;
    atom.name = name.str();
    atom.group = 2;
    atom.text = atom.name;
    return OfAtom(atom);
}

// 排在所有 ext 之后（同 replay.py 的 atom_order：(2, 1, 名字)）
LinExpr LinExpr::BExt(llvm::StringRef name)
{
    Atom atom;
    atom.kind = Atom::Kind::BExt;
    atom.name = name.str();
    atom.group = 2;
    atom.index = 1;
    atom.text = atom.name;
    return OfAtom(atom);
}

int LinExpr::Depth() const
{
    int d = 0;
    for (const auto &term : Terms()) {
        d = std::max(d, term.first.depth);
    }
    return d;
}

Atom LinExpr::OpaqueAtom(Atom::Kind kind, const LinExpr &a, const LinExpr *b, int64_t n)
{
    Atom atom;
    atom.kind = kind;
    atom.n = n;
    atom.depth = 1 + std::max(a.Depth(), b != nullptr ? b->Depth() : 0);
    atom.a = std::make_shared<const LinExpr>(a);
    if (b != nullptr) {
        atom.b = std::make_shared<const LinExpr>(*b);
    }
    atom.group = 5;
    switch (kind) {
        case Atom::Kind::Div:
            atom.text = "div(" + a.Show() + ", " + std::to_string(n) + ")";
            break;
        case Atom::Kind::Mod:
            atom.text = "mod(" + a.Show() + ", " + std::to_string(n) + ")";
            break;
        case Atom::Kind::Wrap:
            atom.text = "wrap" + std::to_string(n) + "(" + a.Show() + ")";
            break;
        case Atom::Kind::Mul:
            atom.text = "mul(" + a.Show() + ", " + b->Show() + ")";
            break;
        default:
            break;
    }
    return atom;
}

LinExpr LinExpr::Div(const LinExpr &a, int64_t n)
{
    if (a.unknown || n == 0) {
        return Unknown();
    }
    if (a.IsConst()) {
        int64_t q = 0;
        return FloorDiv(a.k, n, q) ? Const(q) : Unknown();
    }
    if (a.Depth() >= kMaxOpaqueDepth) {
        return Unknown();  // 嵌套太深（SPEC-0080）
    }
    return OfAtom(OpaqueAtom(Atom::Kind::Div, a, nullptr, n));
}

LinExpr LinExpr::Mod(const LinExpr &a, int64_t n)
{
    if (a.unknown || n == 0) {
        return Unknown();
    }
    if (a.IsConst()) {
        int64_t m = 0;
        return FloorMod(a.k, n, m) ? Const(m) : Unknown();
    }
    if (a.Depth() >= kMaxOpaqueDepth) {
        return Unknown();  // 嵌套太深（SPEC-0080）
    }
    return OfAtom(OpaqueAtom(Atom::Kind::Mod, a, nullptr, n));
}

LinExpr LinExpr::Wrap(unsigned bits, const LinExpr &a)
{
    if (a.unknown || bits == 0 || bits > 62) {
        return Unknown();
    }
    if (a.IsConst()) {
        int64_t m = 0;
        FloorMod(a.k, int64_t(1) << bits, m);
        return Const(m);
    }
    if (a.Depth() >= kMaxOpaqueDepth) {
        return Unknown();  // 嵌套太深（SPEC-0080）
    }
    return OfAtom(OpaqueAtom(Atom::Kind::Wrap, a, nullptr, bits));
}

LinExpr LinExpr::Mul(const LinExpr &a, const LinExpr &b)
{
    if (a.unknown || b.unknown) {
        return Unknown();
    }
    if (a.IsConst() && b.IsConst()) {
        int64_t p = 0;
        return llvm::MulOverflow(a.k, b.k, p) ? Unknown() : Const(p);
    }
    if (std::max(a.Depth(), b.Depth()) >= kMaxOpaqueDepth) {
        return Unknown();  // 嵌套太深（SPEC-0080）
    }
    return OfAtom(OpaqueAtom(Atom::Kind::Mul, a, &b, 0));
}

int64_t LinExpr::Coeff(const Atom &atom) const
{
    auto it = t.find(atom);
    return it == t.end() ? 0 : it->second;
}

LinExpr LinExpr::operator+(const LinExpr &o) const
{
    if (unknown || o.unknown) {
        return Unknown();
    }
    LinExpr out = *this;
    if (llvm::AddOverflow(k, o.k, out.k)) {
        return Unknown();
    }
    for (const auto &term : o.t) {
        int64_t sum = 0;
        if (llvm::AddOverflow(out.Coeff(term.first), term.second, sum)) {
            return Unknown();
        }
        if (sum == 0) {
            out.t.erase(term.first);
        } else {
            out.t[term.first] = sum;
        }
    }
    return out;
}

LinExpr LinExpr::Scale(int64_t c) const
{
    if (unknown) {
        return Unknown();
    }
    if (c == 0) {
        return Const(0);
    }
    LinExpr out;
    if (llvm::MulOverflow(k, c, out.k)) {
        return Unknown();
    }
    for (const auto &term : t) {
        int64_t v = 0;
        if (llvm::MulOverflow(term.second, c, v)) {
            return Unknown();
        }
        out.t.emplace(term.first, v);
    }
    return out;
}

LinExpr LinExpr::operator-() const
{
    return Scale(-1);
}

LinExpr LinExpr::operator-(const LinExpr &o) const
{
    return *this + (-o);
}

bool LinExpr::operator==(const LinExpr &o) const
{
    if (unknown || o.unknown) {
        return unknown == o.unknown;
    }
    return k == o.k && t == o.t;
}

std::string LinExpr::Show() const
{
    if (unknown) {
        return "?";
    }
    std::vector<std::string> parts;
    for (const auto &term : t) {
        const Atom &a = term.first;
        std::string s = a.kind == Atom::Kind::Sym    ? a.name
                        : a.kind == Atom::Kind::Ext  ? "ext(" + a.name + ")"
                        : a.kind == Atom::Kind::BExt ? "bext(" + a.name + ")"
                                                     : a.text;
        if (term.second == 1) {
            parts.push_back(s);
        } else if (term.second == -1) {
            parts.push_back("-" + s);
        } else {
            parts.push_back(std::to_string(term.second) + "*" + s);
        }
    }
    if (k != 0 || parts.empty()) {
        parts.push_back(std::to_string(k));
    }
    std::string out = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        out += parts[i][0] == '-' ? " - " + parts[i].substr(1) : " + " + parts[i];
    }
    return out;
}

// ---- 解析（同 replay.py 的 tokenize / Parser）----

namespace {

struct Token {
    std::string text;
    size_t pos = 0;
};

bool IsIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool IsIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool Tokenize(llvm::StringRef s, std::vector<Token> &out, std::string &error)
{
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        size_t start = i;
        if (c == '0' && i + 2 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X') &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            i += 2;
            while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
        } else if (s.substr(i).startswith("->") || s.substr(i).startswith("<=") || s.substr(i).startswith(">=") ||
                   s.substr(i).startswith("==") || s.substr(i).startswith("!=")) {
            i += 2;
        } else if (IsIdentStart(c)) {
            while (i < s.size() && IsIdentChar(s[i])) {
                ++i;
            }
            if (i + 1 < s.size() && s[i] == ':' && IsIdentStart(s[i + 1])) {
                ++i;
                while (i < s.size() && IsIdentChar(s[i])) {
                    ++i;
                }
            }
        } else if (llvm::StringRef("-+*(),<>").contains(c)) {
            ++i;
        } else {
            error = "unexpected character '" + std::string(1, c) + "' at " + std::to_string(i);
            return false;
        }
        out.push_back({s.substr(start, i - start).str(), start});
    }
    return true;
}

class Parser {
public:
    Parser(const std::vector<Token> &toks, std::string &error) : toks(toks), error(error)
    {
    }

    bool Expr(LinExpr &out)
    {
        bool neg = Peek() == "-";
        if (neg) {
            ++i;
        }
        if (!Term(out)) {
            return false;
        }
        if (neg) {
            out = -out;
        }
        while (Peek() == "+" || Peek() == "-") {
            bool plus = Take() == "+";
            LinExpr r;
            if (!Term(r)) {
                return false;
            }
            out = plus ? out + r : out - r;
        }
        return true;
    }

    bool AtEnd() const
    {
        return i >= toks.size();
    }
    std::string Peek() const
    {
        return i < toks.size() ? toks[i].text : std::string();
    }
    size_t Pos() const
    {
        return i < toks.size() ? toks[i].pos : (toks.empty() ? 0 : toks.back().pos + toks.back().text.size());
    }

private:
    std::string Take()
    {
        return i < toks.size() ? toks[i++].text : std::string();
    }
    bool Expect(llvm::StringRef want)
    {
        if (Peek() != want) {
            return Fail("expected '" + want.str() + "'");
        }
        ++i;
        return true;
    }
    bool Fail(const std::string &message)
    {
        if (error.empty()) {
            error = message + " at " + std::to_string(Pos()) + (AtEnd() ? " (end)" : " ('" + Peek() + "')");
        }
        return false;
    }

    bool Term(LinExpr &out)
    {
        if (!Factor(out)) {
            return false;
        }
        while (Peek() == "*") {
            ++i;
            LinExpr r;
            if (!Factor(r)) {
                return false;
            }
            // 至多一个非常数因子；两个都不是常数时成为 mul(a, b)（同 replay.py 的 term）
            if (out.IsConst()) {
                out = r.Scale(out.Constant());
            } else if (r.IsConst()) {
                out = out.Scale(r.Constant());
            } else {
                out = LinExpr::Mul(out, r);
            }
        }
        return true;
    }

    bool Name(std::string &out)
    {
        std::string t = Peek();
        if (t.empty() || !IsIdentStart(t[0])) {
            return Fail("expected a name");
        }
        ++i;
        out = t;
        while (Peek() == "->") {
            ++i;
            std::string field = Peek();
            if (field.empty() || !IsIdentStart(field[0])) {
                return Fail("expected a field name after '->'");
            }
            ++i;
            out += "->" + field;
        }
        return true;
    }

    bool Factor(LinExpr &out)
    {
        std::string t = Peek();
        if (t == "(") {
            ++i;
            return Expr(out) && Expect(")");
        }
        if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) {
            ++i;
            unsigned long long v = 0;
            bool hex = t.size() > 1 && (t[1] == 'x' || t[1] == 'X');
            llvm::StringRef digits = hex ? llvm::StringRef(t).drop_front(2) : llvm::StringRef(t);
            if (llvm::getAsUnsignedInteger(digits, hex ? 16 : 10, v) ||
                v >= static_cast<unsigned long long>(INT64_MAX)) {
                out = LinExpr::Unknown();  // 字面量超出 int64（SPEC-0010 §2.1）
            } else {
                out = LinExpr::Const(static_cast<int64_t>(v));
            }
            return true;
        }
        bool call = i + 1 < toks.size() && toks[i + 1].text == "(";
        if (call &&
            (t == "ext" || t == "bext" || t == "div" || t == "mod" || t == "wrap32" || t == "wrap16" || t == "mul")) {
            i += 2;
            if (t == "ext" || t == "bext") {
                std::string name;
                if (!Name(name) || !Expect(")")) {
                    return false;
                }
                out = t == "ext" ? LinExpr::Ext(name) : LinExpr::BExt(name);
                return true;
            }
            LinExpr a;
            if (!Expr(a)) {
                return false;
            }
            if (t == "wrap32" || t == "wrap16") {
                if (!Expect(")")) {
                    return false;
                }
                out = LinExpr::Wrap(t == "wrap32" ? 32 : 16, a);
                return true;
            }
            LinExpr b;
            if (!Expect(",") || !Expr(b) || !Expect(")")) {
                return false;
            }
            if (t == "mul") {
                out = LinExpr::Mul(a, b);
                return true;
            }
            if (!b.IsConst()) {
                return Fail(t + " needs a constant divisor");
            }
            if (b.Constant() == 0) {
                return Fail(t + " by zero");
            }
            out = t == "div" ? LinExpr::Div(a, b.Constant()) : LinExpr::Mod(a, b.Constant());
            return true;
        }
        std::string name;
        if (!Name(name)) {
            return false;
        }
        out = LinExpr::Sym(name);
        return true;
    }

    const std::vector<Token> &toks;
    std::string &error;
    size_t i = 0;
};

}  // namespace

bool ParseLin(llvm::StringRef text, LinExpr &out, std::string &error)
{
    error.clear();
    std::vector<Token> toks;
    if (!Tokenize(text, toks, error)) {
        return false;
    }
    Parser parser(toks, error);
    if (!parser.Expr(out)) {
        return false;
    }
    if (!parser.AtEnd()) {
        error = "trailing '" + parser.Peek() + "' at " + std::to_string(parser.Pos());
        return false;
    }
    return true;
}

bool SplitCond(llvm::StringRef text, std::string &lhs, std::string &op, std::string &rhs)
{
    // 第一个比较符（同 replay.py 的 <=|>=|==|<|(?<!-)>）
    for (size_t i = 0; i < text.size(); ++i) {
        llvm::StringRef rest = text.substr(i);
        size_t len = 0;
        if (rest.startswith("<=") || rest.startswith(">=") || rest.startswith("==")) {
            len = 2;
        } else if (text[i] == '<' || (text[i] == '>' && (i == 0 || text[i - 1] != '-'))) {
            len = 1;
        }
        if (len != 0) {
            lhs = text.substr(0, i).str();
            op = text.substr(i, len).str();
            rhs = text.substr(i + len).str();
            return true;
        }
    }
    return false;
}

bool ParseCond(llvm::StringRef text, std::vector<LinExpr> &le0, std::string &error)
{
    // 第一个比较符（同 replay.py 的 <=|>=|==|<|(?<!-)>）
    size_t at = llvm::StringRef::npos;
    size_t len = 0;
    for (size_t i = 0; i < text.size() && at == llvm::StringRef::npos; ++i) {
        llvm::StringRef rest = text.substr(i);
        if (rest.startswith("<=") || rest.startswith(">=") || rest.startswith("==")) {
            at = i;
            len = 2;
        } else if (text[i] == '<' || (text[i] == '>' && (i == 0 || text[i - 1] != '-'))) {
            at = i;
            len = 1;
        }
    }
    if (at == llvm::StringRef::npos) {
        error = "not a condition";
        return false;
    }
    llvm::StringRef op = text.substr(at, len);
    LinExpr l;
    LinExpr r;
    if (!ParseLin(text.substr(0, at), l, error)) {
        error = "left side: " + error;
        return false;
    }
    if (!ParseLin(text.substr(at + len), r, error)) {
        error = "right side: " + error;
        return false;
    }
    le0.clear();
    if (op == "<=") {
        le0.push_back(l - r);
    } else if (op == "<") {
        le0.push_back(l - r + LinExpr::Const(1));
    } else if (op == ">=") {
        le0.push_back(r - l);
    } else if (op == ">") {
        le0.push_back(r - l + LinExpr::Const(1));
    } else {
        le0.push_back(l - r);
        le0.push_back(r - l);
    }
    return true;
}

std::string ShowLe0(const LinExpr &g)
{
    if (g.IsUnknown()) {
        return "?";
    }
    LinExpr pos;
    LinExpr neg;
    for (const auto &term : g.Terms()) {
        if (term.second > 0) {
            pos = pos + LinExpr::OfAtom(term.first).Scale(term.second);
        } else {
            neg = neg + LinExpr::OfAtom(term.first).Scale(-term.second);
        }
    }
    int64_t k = g.Constant();
    if (pos.Terms().empty()) {
        return neg.Show() + " >= " + std::to_string(k);
    }
    return pos.Show() + " <= " + (neg + LinExpr::Const(-k)).Show();
}

// 被调者的接口符号名：p<i> 或 p<i>->…（全局 g:… 两边同名同义，不算）
static bool IsCalleeIface(const std::string &name)
{
    if (name.size() < 2 || name[0] != 'p' || !std::isdigit(static_cast<unsigned char>(name[1]))) {
        return false;
    }
    size_t i = 1;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) {
        ++i;
    }
    return i == name.size() || name.compare(i, 2, "->") == 0;
}

LinExpr Substitute(const LinExpr &e, const Substitution &m)
{
    if (e.IsUnknown()) {
        return e;
    }
    LinExpr out = LinExpr::Const(e.Constant());
    for (const auto &term : e.Terms()) {
        const Atom &a = term.first;
        int64_t c = term.second;
        LinExpr piece;
        if (a.kind == Atom::Kind::Sym || a.kind == Atom::Kind::Ext || a.kind == Atom::Kind::BExt) {
            const auto &table = a.kind == Atom::Kind::Sym ? m.sym : a.kind == Atom::Kind::Ext ? m.ext : m.bext;
            auto hit = table.find(a.name);
            size_t arrow = a.name.find("->");
            if (hit != table.end()) {
                piece = hit->second;
            } else if (arrow != std::string::npos && m.alias.count(a.name.substr(0, arrow)) != 0) {
                std::string renamed = m.alias.at(a.name.substr(0, arrow)) + a.name.substr(arrow);
                piece = a.kind == Atom::Kind::Sym   ? LinExpr::Sym(renamed)
                        : a.kind == Atom::Kind::Ext ? LinExpr::Ext(renamed)
                                                    : LinExpr::BExt(renamed);
            } else if ((m.poison && IsCalleeIface(a.name)) ||
                       (m.poisonGlobals && a.kind == Atom::Kind::Sym && a.name.compare(0, 2, "g:") == 0)) {
                std::string text = a.kind == Atom::Kind::Sym   ? a.name
                                   : a.kind == Atom::Kind::Ext ? "ext(" + a.name + ")"
                                                               : "bext(" + a.name + ")";
                piece = LinExpr::Sym("_unmapped:" + text);  // SPEC-0063
            } else {
                piece = LinExpr::OfAtom(a);
            }
        } else if (a.kind == Atom::Kind::Div) {
            piece = LinExpr::Div(Substitute(*a.a, m), a.n);
        } else if (a.kind == Atom::Kind::Mod) {
            piece = LinExpr::Mod(Substitute(*a.a, m), a.n);
        } else if (a.kind == Atom::Kind::Wrap) {
            piece = LinExpr::Wrap(static_cast<unsigned>(a.n), Substitute(*a.a, m));
        } else {
            piece = LinExpr::Mul(Substitute(*a.a, m), Substitute(*a.b, m));
        }
        out = out + piece.Scale(c);
    }
    return out;
}

}  // namespace oobsum
