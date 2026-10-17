//===-- Apply.cpp - Call-site substitution and decision ---------*- C++ -*-===//
//
// 见 Apply.h。每个函数都标了它对应的 replay.py 函数；说明文字（why）与 replay.py 逐字相同（Dump=apply 与 replay.py -v 逐行比对）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Apply.h"

#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Key.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Regex.h"

#include <algorithm>
#include <functional>
#include <set>

namespace oobsum {
namespace apply {

using summary::ArgValue;
using summary::Call;
using summary::Summary;

namespace {

LinExpr P(const std::string &text)
{
    LinExpr e;
    std::string error;
    return ParseLin(text, e, error) ? e : LinExpr::Unknown();
}

std::vector<LinExpr> C(const std::string &text)
{
    std::vector<LinExpr> le0;
    std::string error;
    ParseCond(text, le0, error);
    return le0;
}

std::string Show(const LinExpr &e)
{
    return e.Show();
}

std::string Join(const std::vector<std::string> &parts, const std::string &sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        out += (i == 0 ? "" : sep) + parts[i];
    }
    return out;
}

bool HasConst(const LinExpr &e)
{
    return e.IsConst();
}

// 同 replay.py 的 build_map：实参 → 代换表（符号、ext、字段、别名）与实参的 eff_depth
struct Map {
    Substitution m;
    int64_t effDepth = 0;
};

Map BuildMap(const Call &call)
{
    Map out;
    out.m.poison = true;  // 表里没有的被调者符号不能当成调用方的（SPEC-0063）
    for (size_t i = 0; i < call.args.size(); ++i) {
        std::string p = "p" + std::to_string(i);
        const ArgValue &arg = call.args[i];
        // 第 i 个形参不是空指针（SPEC-0099）：常数 0 → 0、非 0 常数 → 1；局部 / 全局 / 字符串的地址 → 1；调用方形参所指 → 调用方的 nn:p<j>；其余不换
        if (arg.kind == ArgValue::Kind::Expr) {
            LinExpr v = P(arg.expr);
            if (v.IsConst()) {
                out.m.sym["nn:" + p] = LinExpr::Const(v.Constant() != 0 ? 1 : 0);
            }
        } else if (arg.kind == ArgValue::Kind::Ptr) {
            llvm::StringRef obj = arg.ptr->obj;
            unsigned j = 0;
            llvm::StringRef rest = obj;
            if ((obj.startswith("local:") && !obj.startswith("local:_unknown")) || obj.startswith("g:")) {
                out.m.sym["nn:" + p] = LinExpr::Const(1);
            } else if (rest.consume_front("*p") && !rest.consumeInteger(10, j) && rest.empty()) {
                out.m.sym["nn:" + p] = LinExpr::Sym("nn:p" + std::to_string(j));
            }
        }
        if (arg.kind == ArgValue::Kind::Ptr) {
            out.m.ext[p] = P(arg.ptr->have);
            // 指针前面的字节数；实参没写 back → 不是接口符号的名字，调用点上只会走向记账（不当成 0，SPEC-0039 §2.2）
            out.m.bext[p] = arg.ptr->back ? P(*arg.ptr->back) : LinExpr::Sym("_noback" + std::to_string(i));
            out.effDepth = std::max(out.effDepth, arg.ptr->effDepth.getValueOr(0));
            if (arg.ptr->alias) {
                out.m.alias[p] = *arg.ptr->alias;
            }
            if (arg.ptr->fields) {
                for (const auto &entry : *arg.ptr->fields) {
                    std::string name = p + "->" + entry.first;
                    // 字段不是空指针（SPEC-0101）：常数 → 0 / 1；指向局部 / 全局 / 字符串 → 1；调用方形参所指 → 调用方的 nn:p<j>
                    if (entry.second.kind == ArgValue::Kind::Expr) {
                        LinExpr fv = P(entry.second.expr);
                        if (fv.IsConst()) {
                            out.m.sym["nn:" + name] = LinExpr::Const(fv.Constant() != 0 ? 1 : 0);
                        }
                    } else if (entry.second.kind == ArgValue::Kind::Ptr) {
                        llvm::StringRef fobj = entry.second.ptr->obj;
                        llvm::StringRef frest = fobj;
                        unsigned fj = 0;
                        if ((fobj.startswith("local:") && !fobj.startswith("local:_unknown")) ||
                            fobj.startswith("g:")) {
                            out.m.sym["nn:" + name] = LinExpr::Const(1);
                        } else if (frest.consume_front("*p") && !frest.consumeInteger(10, fj) && frest.empty()) {
                            out.m.sym["nn:" + name] = LinExpr::Sym("nn:p" + std::to_string(fj));
                        }
                    }
                    if (entry.second.kind == ArgValue::Kind::Ptr) {
                        out.m.ext[name] = P(entry.second.ptr->have);
                        out.m.bext[name] = entry.second.ptr->back
                                               ? P(*entry.second.ptr->back)
                                               : LinExpr::Sym("_noback" + std::to_string(i) + "_" + entry.first);
                        out.effDepth = std::max(out.effDepth, entry.second.ptr->effDepth.getValueOr(0));
                    } else {
                        out.m.sym[name] = P(entry.second.expr);
                    }
                }
            }
        } else if (arg.kind == ArgValue::Kind::Expr) {
            out.m.sym[p] = P(arg.expr);
        }
    }
    // 调用前已经不是入口值的全局（SPEC-0064）：确切值代进去，不知道的改名成非接口符号
    if (call.gvals) {
        for (const auto &entry : *call.gvals) {
            if (entry.first == "g:*") {
                out.m.poisonGlobals = true;
            } else {
                out.m.sym[entry.first] =
                    entry.second == "?" ? LinExpr::Sym("_unmapped:" + entry.first) : P(entry.second);
            }
        }
    }
    return out;
}

// 同 replay.py 的 subst_cond：两边分别代换，比较符原样保留
std::string SubstCond(const std::string &cond, const Substitution &m)
{
    std::string lhs;
    std::string op;
    std::string rhs;
    if (!SplitCond(cond, lhs, op, rhs)) {
        return cond;
    }
    return Show(Substitute(P(lhs), m)) + " " + op + " " + Show(Substitute(P(rhs), m));
}

// 同 replay.py 的 subst_obj：*pK → 第 K 个指针实参的 obj；*pK->f → 它的 fields.f 的 obj（经由 via）；pK->f → 调用方的 pJ->f
std::pair<std::string, llvm::Optional<std::string>> SubstObj(const std::string &obj, const Call &call)
{
    static llvm::Regex re("^(\\*?)p([0-9]+)(->([A-Za-z0-9_]+))?$");
    llvm::SmallVector<llvm::StringRef, 5> groups;
    if (!re.match(obj, &groups)) {
        return {obj, llvm::None};  // g: / local: / heap@ 与调用点无关，原样上传
    }
    bool star = !groups[1].empty();
    unsigned long long k = 0;
    groups[2].getAsInteger(10, k);
    std::string f = groups[4].str();
    const ArgValue *arg = k < call.args.size() ? &call.args[k] : nullptr;
    if (arg == nullptr || arg->kind != ArgValue::Kind::Ptr) {
        return {"?", llvm::None};
    }
    const std::string &base = arg->ptr->obj;
    if (groups[3].empty()) {
        return {base, llvm::None};
    }
    static llvm::Regex param("^\\*(p[0-9]+)$");
    llvm::SmallVector<llvm::StringRef, 2> bm;
    bool isParam = param.match(base, &bm);
    if (star) {
        if (arg->ptr->fields) {
            auto it = arg->ptr->fields->find(f);
            if (it != arg->ptr->fields->end() && it->second.kind == ArgValue::Kind::Ptr) {
                llvm::StringRef b = base;
                llvm::Optional<std::string> via;
                if (b.startswith("g:") || b.startswith("local:")) {
                    via = base + "." + f;
                }
                return {it->second.ptr->obj, via};
            }
        }
        return {isParam ? "*" + bm[1].str() + "->" + f : "?", llvm::None};
    }
    return {isParam ? bm[1].str() + "->" + f : "?", llvm::None};
}

// 调用点处调用方局部的区间（call.locals）
struct LocalRange {
    LinExpr lo;
    LinExpr hi;
    std::string tag;
};
using Locals = std::map<std::string, LocalRange>;

// 同 replay.py 的 eliminate_locals：把调用方局部换成它在调用点的界；返回新式、用到的标签、非空约束（lo − hi ≤ 0）
struct Eliminated {
    LinExpr out;
    std::set<std::string> tags;
    std::vector<LinExpr> ne;
};

// 同 replay.py 的 has_local：不透明项的内层（递归）是否含调用方局部
bool HasLocal(const Atom &a, const Locals &locs)
{
    if (a.kind == Atom::Kind::Sym) {
        return locs.count(a.name) != 0;
    }
    if (a.kind == Atom::Kind::Ext || a.kind == Atom::Kind::BExt) {
        return false;
    }
    for (const LinExpr *inner : {a.a.get(), a.b.get()}) {
        if (inner == nullptr) {
            continue;
        }
        for (const auto &term : inner->Terms()) {
            if (HasLocal(term.first, locs)) {
                return true;
            }
        }
    }
    return false;
}

Eliminated EliminateLocals(const LinExpr &l, const Locals &locs, bool upper)
{
    Eliminated r;
    r.out = LinExpr::Const(l.Constant());
    auto absorb = [&](const Eliminated &e) {
        r.tags.insert(e.tags.begin(), e.tags.end());
        r.ne.insert(r.ne.end(), e.ne.begin(), e.ne.end());
    };
    for (const auto &term : l.Terms()) {
        const Atom &a = term.first;
        int64_t c = term.second;
        auto it = a.kind == Atom::Kind::Sym ? locs.find(a.name) : locs.end();
        if (it != locs.end()) {
            const LocalRange &range = it->second;
            r.out = r.out + ((c > 0) == upper ? range.hi : range.lo).Scale(c);
            r.tags.insert(range.tag);
            LinExpr g = range.lo - range.hi;
            if (!g.IsConst()) {
                r.ne.push_back(g);
            }
            continue;
        }
        bool opaque = a.kind == Atom::Kind::Div || a.kind == Atom::Kind::Mod || a.kind == Atom::Kind::Wrap;
        if (!opaque || !HasLocal(a, locs)) {
            r.out = r.out + LinExpr::OfAtom(a).Scale(c);
            continue;
        }
        // 不透明项里的局部也换（SPEC-0049）：div 单调代入；mod / wrap 在区间内时等同内层，否则取 [0, n − 1] / 类型范围
        bool up = (c > 0) == upper;
        const LinExpr &inner = *a.a;
        if (a.kind == Atom::Kind::Div) {
            Eliminated b = EliminateLocals(inner, locs, a.n > 0 ? up : !up);
            r.out = r.out + LinExpr::Div(b.out, a.n).Scale(c);
            absorb(b);
            continue;
        }
        if (a.kind == Atom::Kind::Mod && a.n <= 0) {
            r.out = r.out + LinExpr::OfAtom(a).Scale(c);
            continue;
        }
        Eliminated lo = EliminateLocals(inner, locs, false);
        Eliminated hi = EliminateLocals(inner, locs, true);
        int64_t span = a.kind == Atom::Kind::Mod ? a.n : (int64_t(1) << a.n);
        if (lo.out.IsConst() && hi.out.IsConst() && lo.out.Constant() >= 0 && hi.out.Constant() < span) {
            r.out = r.out + (up ? hi.out : lo.out).Scale(c);
            absorb(lo);
            absorb(hi);
        } else if (a.kind == Atom::Kind::Wrap || (lo.out.IsConst() && lo.out.Constant() >= 0)) {
            r.out = r.out + LinExpr::Const(up ? span - 1 : 0).Scale(c);
            r.tags.insert(a.kind == Atom::Kind::Mod ? "mask" : "type");
        } else {
            r.out = r.out + LinExpr::OfAtom(a).Scale(c);
        }
    }
    return r;
}

// 同 replay.py 的 is_iface：调用方的接口符号（形参、字段、ext、全局；不透明项要求内层全是）
bool IsIface(const Atom &a)
{
    if (a.kind == Atom::Kind::Sym) {
        // p<数字> 或 p<数字>->…（同 replay.py 的 p\d+($|->)），或全局 g:…
        llvm::StringRef name = a.name;
        name.consume_front("nn:");  // 形参不是空指针（SPEC-0099）
        if (name.consume_front("p")) {
            llvm::StringRef digits = name.take_while([](char c) { return c >= '0' && c <= '9'; });
            llvm::StringRef rest = name.drop_front(digits.size());
            if (!digits.empty() && (rest.empty() || rest.startswith("->"))) {
                return true;
            }
        }
        return llvm::StringRef(a.name).startswith("g:");
    }
    if (a.kind == Atom::Kind::Ext || a.kind == Atom::Kind::BExt) {
        return true;
    }
    for (const LinExpr *inner : {a.a.get(), a.b.get()}) {
        if (inner == nullptr) {
            continue;
        }
        for (const auto &term : inner->Terms()) {
            if (!IsIface(term.first)) {
                return false;
            }
        }
    }
    return true;
}

bool AllIface(const LinExpr &e)
{
    return llvm::all_of(e.Terms(), [](const std::pair<const Atom, int64_t> &t) { return IsIface(t.first); });
}

Tag TagOf(const std::string &name)
{
    llvm::Optional<Tag> t = ParseTag(name);
    return t ? *t : Tag::None;
}

std::string NumberOrNone(bool has, int64_t v)
{
    return has ? std::to_string(v) : std::string("None");
}

}  // namespace

// 同 replay.py 的 Env(caller.params, caller.fields)
SymEnv CallerEnv(const Summary &caller)
{
    SymEnv env;
    auto add = [&](const std::string &name, const std::string &type) {
        int64_t lo = 0;
        int64_t hi = 0;
        if (!llvm::StringRef(type).endswith("*") && TypeRangeOf(type, lo, hi)) {
            env.SetTypeRange(name, lo, hi);
        }
    };
    for (size_t i = 0; i < caller.params.size(); ++i) {
        add("p" + std::to_string(i), caller.params[i].type);
        if (llvm::StringRef(caller.params[i].type).endswith("*")) {
            env.SetTypeRange("nn:p" + std::to_string(i), 0, 1);  // SPEC-0099
        }
    }
    if (caller.fields) {
        for (const auto &entry : *caller.fields) {
            add(entry.first, entry.second.type.getValueOr(""));
            if (llvm::StringRef(entry.second.type.getValueOr("")).endswith("*")) {
                env.SetTypeRange("nn:" + entry.first, 0, 1);  // 指针字段不是空指针（SPEC-0101）
            }
        }
    }
    return env;
}

EnvGuards EnvGuardsOf(const Call &call)
{
    EnvGuards out;
    if (call.guards) {
        for (const summary::CallGuard &g : *call.guards) {
            for (const LinExpr &atom : C(g.cond)) {
                out.emplace_back(atom, TagOf(g.bound.empty() ? "guard" : g.bound));
            }
        }
    }
    return out;
}

namespace {

Outcome ClassifyPlain(const summary::Req &req, const Call &call, const Summary &caller, const EnvGuards &envGuards)
{
    Map map = BuildMap(call);
    Outcome o;
    o.have = Substitute(P(req.have), map.m);
    o.endLo = Substitute(P(req.endLo), map.m);
    o.endHi = Substitute(P(req.endHi), map.m);
    // 指针实参的偏移是区间（SPEC-0095）：have 正好是 ext(pK) 时按对象起点判——剩余加偏移上界、末端加偏移区间、
    // 标签并入偏移的来源；偏移下界不知道 → 最小末端记 0（不判 L1，往上传也不会）
    llvm::Optional<std::string> offBound;
    if (req.dir.empty()) {
        LinExpr rh = P(req.have);
        llvm::StringRef name = rh.Terms().size() == 1 && rh.Terms().begin()->second == 1 &&
                                       rh.Terms().begin()->first.kind == Atom::Kind::Ext
                                   ? llvm::StringRef(rh.Terms().begin()->first.name)
                                   : llvm::StringRef();
        unsigned k = 0;
        if (name.consume_front("p") && !name.getAsInteger(10, k) && k < call.args.size() &&
            call.args[k].kind == ArgValue::Kind::Ptr && call.args[k].ptr->offHi) {
            const summary::PtrArg &pa = *call.args[k].ptr;
            LinExpr offHi = P(*pa.offHi);
            o.have = o.have + offHi;
            o.endHi = o.endHi + offHi;
            o.endLo = pa.offLo ? o.endLo + P(*pa.offLo) : LinExpr::Const(0);
            offBound = pa.offBound ? *pa.offBound : std::string("widen");
        }
    }
    for (const std::string &g : req.guard) {
        for (const LinExpr &atom : C(SubstCond(g, map.m))) {
            o.guards.push_back(atom);
        }
    }
    Locals locs;
    if (call.locals) {
        for (const auto &entry : *call.locals) {
            locs[entry.first] = {P(entry.second.lo), P(entry.second.hi),
                                 entry.second.bound.empty() ? "loop" : entry.second.bound};
        }
    }
    SymEnv env = CallerEnv(caller);
    for (const auto &g : envGuards) {
        env.AddGuard(g.first, g.second);
    }
    std::tie(o.obj, o.via) = SubstObj(req.obj, call);

    // ① 守卫：常量假 → 这次调用走不到访问
    std::vector<LinExpr> live;
    for (const LinExpr &g : o.guards) {
        LinExpr gmax = EliminateLocals(g, locs, true).out;
        LinExpr gmin = EliminateLocals(g, locs, false).out;
        if (HasConst(gmin) && gmin.Constant() > 0) {
            o.outcome = "safe";
            o.reason = "guard_false";  // 判安全的机器原因（判定日志的 SAFE 用，SPEC-0021 §2.2）
            o.why = "守卫 " + ShowLe0(g) + " 在此为假，访问走不到";
            return o;
        }
        if (HasConst(gmax) && gmax.Constant() <= 0) {
            continue;
        }
        live.push_back(gmax);
    }
    // 实参是不跟踪的指针（调用点记录的占位符 local:_unknownK / _unknownK）：判不了也传不上去（SPEC-0062）
    if (llvm::StringRef(o.obj).startswith("local:_unknown") && o.have.Show() == o.obj.substr(6)) {
        o.outcome = "open";
        o.reason = "untracked_pointer";
        o.why = "实参是不跟踪的指针，大小未知";
        return o;
    }
    // 被调者经实参所指对象里的指针访问，调用方不知道那个指针指向哪里（大小里有没映射的被调者符号，SPEC-0063）
    if (o.have.Show().find("_unmapped:") != std::string::npos) {
        o.outcome = "open";
        o.reason = "untracked_pointer";
        o.why = "对象大小 " + Show(o.have) + " 在调用方不知道";
        return o;
    }
    // ② 局部换成区间
    Eliminated hi = EliminateLocals(o.endHi, locs, true);
    Eliminated lo = EliminateLocals(o.endLo, locs, false);
    o.hasBase = true;
    o.end2Hi = hi.out;
    o.end2Lo = lo.out;
    if (call.alt) {
        o.end2Lo =
            LinExpr::Const(0);  // 函数指针的多个目标之一：这次不一定执行，访问可能不发生（只判 L2，SPEC-0058 §2.3）
    }
    if (hi.tags.empty()) {
        o.bound = req.bound.empty() ? "exact" : req.bound;
    } else {
        // replay.py 用 set.pop()（多个标签时不确定）；这里取最"差"的那个，单个标签时与它相同（SPEC-0012 §2.2）
        Tag worst = Tag::None;
        for (const std::string &t : hi.tags) {
            worst = WorseTag(worst, TagOf(t));
        }
        o.bound = worst == Tag::None ? *hi.tags.begin() : TagName(worst).str();
    }
    if (offBound) {
        o.bound = TagName(WorseTag(TagOf(o.bound), TagOf(*offBound))).str();  // SPEC-0095
    }
    std::vector<LinExpr> liveNe = live;
    liveNe.insert(liveNe.end(), hi.ne.begin(), hi.ne.end());
    for (const LinExpr &g : liveNe) {
        env.AddGuard(g, Tag::Guard);
    }
    LinExpr dhi = o.end2Hi - o.have;
    LinExpr dlo = o.end2Lo - o.have;
    o.depth = req.depth + 1;
    o.effDepth = std::max(req.effDepth, map.effDepth);
    // ③ 判定
    if (HasConst(dhi)) {
        if (dhi.Constant() <= 0) {
            o.outcome = "safe";
            o.reason = "within";
            o.why = "最大末端 " + Show(o.end2Hi) + " ≤ " + Show(o.have);
            return o;
        }
        if (HasConst(dlo) && dlo.Constant() > 0) {
            o.outcome = "L1";
            o.why = "最小末端 " + Show(o.end2Lo) + " > " + Show(o.have) + "，对所有取值都越界";
            return o;
        }
        if (IsCodeTag(TagOf(o.bound))) {
            o.outcome = "L2";
            o.why = "最大末端 " + Show(o.end2Hi) + " > " + Show(o.have) + "（上界来自 " + o.bound + "），最小末端 " +
                    Show(o.end2Lo) + " 不越界";
            return o;
        }
        o.outcome = "open";
        // widening 去掉的上界是指针步进（A-45）；type / data 只剩类型范围（SPEC-0038）
        o.reason = o.bound == "widen" ? "pointer_stepping" : "type_range_only";
        o.why = "上界只来自 " + o.bound + "，不算代码给出的界";
        return o;
    }
    std::vector<LinExpr> all = liveNe;
    for (const auto &g : envGuards) {
        all.push_back(g.first);
    }
    if (EntailsLe0(dhi, all, env)) {
        o.outcome = "safe";
        o.reason = "guard_entailed";
        o.why = "守卫蕴含 " + Show(o.end2Hi) + " ≤ " + Show(o.have);
        return o;
    }
    // 守卫界判定（F-12b）：对象已知（have 是常数），剩下的符号都被代码给出的界框住
    if (HasConst(o.have)) {
        Limit vmax = env.RangeOf(dhi, true);
        if (vmax.known && IsCodeTag(vmax.tag)) {
            std::string tmax = TagName(vmax.tag).str();
            if (vmax.value <= 0) {
                o.outcome = "safe";
                o.reason = "guard_bounds";
                o.why = "在守卫给出的范围内最大末端也不越界";
                return o;
            }
            Limit vmin = env.RangeOf(dlo, false);
            int64_t hiEnd = vmax.value + o.have.Constant();
            bool hasLo = vmin.known;
            int64_t loEnd = hasLo ? vmin.value + o.have.Constant() : 0;
            if (hasLo && vmin.value > 0) {
                o.outcome = "L1";
                o.bound = tmax;
                o.why = "在 " + tmax + " 给出的范围内最小末端 " + std::to_string(loEnd) + " 也越界";
                return o;
            }
            o.outcome = "L2";
            o.bound = tmax;
            o.hasHiEnd = true;
            o.hiEnd = hiEnd;
            o.hasLoEnd = hasLo;
            o.loEnd = loEnd;
            o.why = tmax + " 给出的范围内最大末端 " + std::to_string(hiEnd) + " > " + Show(o.have) + "，最小末端 " +
                    NumberOrNone(hasLo, loEnd) + " 不越界";
            return o;
        }
    }
    if (AllIface(dhi) && AllIface(dlo)) {
        o.outcome = "up";
        for (const LinExpr &g : liveNe) {
            if (AllIface(g)) {
                o.guards2.push_back(g);
            }
        }
        return o;
    }
    o.outcome = "open";
    // 末端里有调用点上值不知道的全局（调用前改过，SPEC-0064）：global_value；其余照旧
    o.reason = (o.end2Hi.Show() + " " + o.end2Lo.Show()).find("_unmapped:g:") != std::string::npos ? "global_value"
                                                                                                   : "entry_unproven";
    o.why = "含调用方局部且没有界";
    return o;
}

}  // namespace

Outcome Classify(const summary::Req &req, const Call &call, const Summary &caller, const EnvGuards &envGuards,
                 bool subLabel)
{
    Outcome o = ClassifyPlain(req, call, caller, envGuards);
    // 内层 Req（成员数组、多维数组的一行，SPEC-0104 §2.3 3a、用户决定 D22）：越出内层、没越出外层只记 SUSPECT——
    // L1 / L2 改判 open，账本原因一律 subobject_overflow；safe / up 不变（同 replay.py 的 classify）。
    // Subobject=label（SPEC-0105）：L1 / L2 照常，只有 open 的原因换成 subobject_overflow
    bool relabel = o.outcome == "open" || (!subLabel && (o.outcome == "L1" || o.outcome == "L2"));
    if (req.dir == "sub" && relabel) {
        o.why = o.outcome + "（" + (o.reason.empty() ? o.why : o.reason) + "）→ 内层只记 SUSPECT";
        o.outcome = "open";
        o.reason = "subobject_overflow";
    }
    return o;
}

std::string FingerprintObj(const std::string &obj, const std::string &dir)
{
    return dir.empty() ? obj : obj + "#" + dir;
}

llvm::Optional<Call> ParentCall(const summary::Req &req, const Call &call)
{
    if (!req.dir.empty()) {
        return llvm::None;  // 反向 Req 只查成员起点（SPEC-0039 §2.1）
    }
    static llvm::Regex re("^\\*p([0-9]+)$");
    llvm::SmallVector<llvm::StringRef, 2> groups;
    unsigned long long k = 0;
    if (!re.match(req.obj, &groups) || groups[1].getAsInteger(10, k) || k >= call.args.size()) {
        return llvm::None;
    }
    const ArgValue &arg = call.args[k];
    if (arg.kind != ArgValue::Kind::Ptr || !arg.ptr->parent) {
        return llvm::None;
    }
    Call out = call;
    auto outer = std::make_shared<summary::PtrArg>();
    outer->obj = arg.ptr->parent->obj;
    outer->have = arg.ptr->parent->have;
    outer->effDepth = arg.ptr->effDepth;
    outer->offLo = arg.ptr->offLo;  // 外层的剩余同样按偏移上界算（SPEC-0095）
    outer->offHi = arg.ptr->offHi;
    outer->offBound = arg.ptr->offBound;
    out.args[k].ptr = outer;
    return out;
}

// 带窗口的实参（SPEC-0104 §2.3 3b）：req 是 *pK 上的正向 Req、have 正好是 ext(pK)、第 K 个实参带 sub →
// 这个实参换成"只剩 sub"的调用点（同 replay.py 的 sub_call）
llvm::Optional<Call> SubCall(const summary::Req &req, const Call &call)
{
    if (!req.dir.empty()) {
        return llvm::None;
    }
    // *p<k>：实参本身；*p<k>->f：实参所指结构体里记着的指针字段值（SPEC-0107 §2.1）
    static llvm::Regex re("^\\*p([0-9]+)(->([A-Za-z0-9_]+))?$");
    llvm::SmallVector<llvm::StringRef, 4> groups;
    unsigned long long k = 0;
    if (!re.match(req.obj, &groups) || groups[1].getAsInteger(10, k) || k >= call.args.size()) {
        return llvm::None;
    }
    const ArgValue &arg = call.args[k];
    std::string field = groups[3].str();
    std::string root = "p" + std::to_string(k) + (field.empty() ? "" : "->" + field);
    if (arg.kind != ArgValue::Kind::Ptr || P(req.have) != LinExpr::Ext(root)) {
        return llvm::None;
    }
    const ArgValue *target = &arg;
    if (!field.empty()) {
        if (!arg.ptr->fields || arg.ptr->fields->count(field) == 0) {
            return llvm::None;
        }
        target = &arg.ptr->fields->at(field);
    }
    if (target->kind != ArgValue::Kind::Ptr || !target->ptr->sub) {
        return llvm::None;
    }
    auto inner = std::make_shared<summary::PtrArg>();
    inner->obj = target->ptr->obj;
    inner->have = *target->ptr->sub;
    inner->effDepth = target->ptr->effDepth;
    inner->fields = target->ptr->fields;
    Call out = call;
    if (field.empty()) {
        out.args[k].ptr = inner;
    } else {
        auto outer = std::make_shared<summary::PtrArg>(*arg.ptr);
        (*outer->fields)[field].ptr = inner;
        out.args[k].ptr = outer;
    }
    return out;
}

namespace {

// 一个根换成调用方的说法（SPEC-0106 §2.3）：具体对象（local: / g: / heap@，kind 'c'）、调用方的根（p<j>、p<j>->f，kind 'r'）、不知道（kind '?'）
std::pair<char, std::string> CallerRoot(const std::string &obj)
{
    llvm::StringRef o = obj;
    if ((o.startswith("local:") && !o.startswith("local:_unknown")) || o.startswith("heap@")) {
        return {'c', obj};
    }
    if (o.startswith("g:")) {
        return {'c', StripGlobalSuffix(obj)};
    }
    static llvm::Regex param("^\\*?p([0-9]+)(->([A-Za-z0-9_]+))?$");
    llvm::SmallVector<llvm::StringRef, 4> m;
    if (param.match(o, &m)) {
        // *p<j> → p<j>；*p<j>->f（指针字段所指）→ p<j>->f；p<j>->m（形参所指结构体里的成员数组）→ p<j>
        bool pointee = o.startswith("*") && !m[3].empty();
        return {'r', "p" + m[1].str() + (pointee ? "->" + m[3].str() : "")};
    }
    return {'?', obj};
}

std::pair<char, std::string> MapRoot(const std::string &root, const Call &call)
{
    if (llvm::StringRef(root).startswith("g:")) {
        return {'c', StripGlobalSuffix(root)};
    }
    static llvm::Regex re("^p([0-9]+)(->([A-Za-z0-9_]+))?$");
    llvm::SmallVector<llvm::StringRef, 4> m;
    unsigned long long k = 0;
    if (!re.match(root, &m) || m[1].getAsInteger(10, k) || k >= call.args.size() ||
        call.args[k].kind != ArgValue::Kind::Ptr) {
        return {'?', root};
    }
    const summary::PtrArg &arg = *call.args[k].ptr;
    if (m[3].empty()) {
        return CallerRoot(arg.obj);
    }
    // 指针字段所指：实参记着的字段值；实参就是调用方形参所指 → 调用方的 p<j>->f
    std::string field = m[3].str();
    if (arg.fields && arg.fields->count(field) != 0 && arg.fields->at(field).kind == ArgValue::Kind::Ptr) {
        return CallerRoot(arg.fields->at(field).ptr->obj);
    }
    std::pair<char, std::string> base = CallerRoot(arg.obj);
    if (base.first == 'r' && llvm::StringRef(arg.obj).startswith("*") && base.second.find("->") == std::string::npos) {
        return {'r', base.second + "->" + field};
    }
    return {'?', root};
}

}  // namespace

AssumeCheck CheckAssume(const std::pair<std::string, std::string> &assume, const Call &call)
{
    std::pair<char, std::string> a = MapRoot(assume.first, call);
    std::pair<char, std::string> b = MapRoot(assume.second, call);
    AssumeCheck out;
    if (a.first == '?' || b.first == '?') {
        out.verdict = "unknown";
        out.detail = a.first == '?' ? a.second : b.second;
        return out;
    }
    if (a.second == b.second) {
        out.verdict = "same";
        out.detail = a.second;
        return out;
    }
    if (a.first == 'c' && b.first == 'c') {
        out.verdict = "ok";  // 两个不同的具体对象
        return out;
    }
    // 一边是调用方的根：另一边是调用方的局部 / 堆 → 不可能重叠（形参进函数时就定了）；是全局 → 往上传
    const std::pair<char, std::string> &other = a.first == 'c' ? a : b;
    if (other.first == 'c' && !llvm::StringRef(other.second).startswith("g:")) {
        out.verdict = "ok";
        return out;
    }
    out.verdict = "up";
    out.up = a.second < b.second ? std::make_pair(a.second, b.second) : std::make_pair(b.second, a.second);
    out.detail = out.up.first + "&" + out.up.second;
    return out;
}

// 同 replay.py 的 classify_all：成员已经报 / 记账 → 只有它；成员 safe → 外层不 safe 就换成外层的；成员 up → 外层不 safe 时两条都要。
// 带窗口的实参：前面的结局都是 safe / up 时内层另判一次，不是 safe 就加上（dir "sub"，SPEC-0104 §2.3 3b）
std::vector<SiteClass> ClassifyAll(const summary::Req &req, const Call &call, const Summary &caller,
                                   const EnvGuards &envGuards, bool subLabel)
{
    std::vector<SiteClass> out{{Classify(req, call, caller, envGuards, subLabel), call, false}};
    llvm::Optional<Call> outer = ParentCall(req, call);
    const std::string first = out[0].mine.outcome;
    if (outer && (first == "safe" || first == "up")) {
        SiteClass o{Classify(req, *outer, caller, envGuards, subLabel), *outer, true};
        if (o.mine.outcome != "safe") {
            if (first == "safe") {
                out.clear();
            }
            out.push_back(std::move(o));
        }
    }
    llvm::Optional<Call> inner = SubCall(req, call);
    bool clean =
        llvm::all_of(out, [](const SiteClass &x) { return x.mine.outcome == "safe" || x.mine.outcome == "up"; });
    if (inner && clean) {
        summary::Req rs = req;
        rs.dir = "sub";
        SiteClass i{Classify(rs, *inner, caller, envGuards, subLabel), *inner, false};
        if (i.mine.outcome != "safe") {
            i.mine.dir = "sub";
            out.push_back(std::move(i));
        }
    }
    return out;
}

summary::Rep RepOf(const Outcome &mine, const Call &call, const std::string &fn, const Summary &caller,
                   const summary::Req &calleeReq)
{
    summary::Rep rep;
    rep.level = mine.outcome;
    rep.reported = std::max(mine.depth, mine.effDepth) > kInlineDepth;
    rep.site = call.site;
    rep.access = calleeReq.access;
    rep.kind = calleeReq.kind;
    rep.dir = mine.dir.empty() ? calleeReq.dir : mine.dir;  // 带窗口实参的内层那条（SPEC-0104 3b / SPEC-0105）
    rep.obj = mine.obj;
    rep.via = mine.via;
    LinExpr lo = mine.end2Lo;
    LinExpr hi = mine.end2Hi;
    if (mine.hasHiEnd) {  // 守卫界判定（F-12b）：末端取守卫给出的范围
        hi = LinExpr::Const(mine.hiEnd);
        if (mine.hasLoEnd) {
            lo = LinExpr::Const(mine.loEnd);
        }
    }
    rep.have = Show(mine.have);
    rep.endLo = Show(lo);
    rep.endHi = Show(hi);
    rep.bound = mine.bound;
    rep.depth = mine.depth;
    rep.effDepth = mine.effDepth;
    rep.chain.push_back(fn + "@" + call.site);
    rep.chain.insert(rep.chain.end(), calleeReq.path.begin(), calleeReq.path.end());
    rep.fp = DecisionFingerprint(rep.level, caller.key, rep.access.key, rep.kind, rep.access.end,
                                 FingerprintObj(rep.obj, rep.dir));
    return rep;
}

namespace {

// 同 replay.py 的 canon_cond
std::string CanonCond(const std::string &cond)
{
    std::vector<std::string> parts;
    for (const LinExpr &g : C(cond)) {
        parts.push_back(ShowLe0(g));
    }
    return Join(parts, " && ");
}

bool SameApply(const llvm::Optional<summary::Apply> &a, const summary::Apply &b)
{
    if (!a || a->kind != b.kind) {
        return false;
    }
    if (a->kind == "ret_new") {
        return a->size && b.size && P(*a->size) == P(*b.size) &&
               a->maybeNull.getValueOr(false) == b.maybeNull.getValueOr(false);
    }
    if (a->kind == "ret_cond") {
        std::vector<std::string> mine = a->then.getValueOr(std::vector<std::string>());
        std::vector<std::string> theirs;
        for (const std::string &t : b.then.getValueOr(std::vector<std::string>())) {
            theirs.push_back(CanonCond(t));
        }
        std::sort(mine.begin(), mine.end());
        std::sort(theirs.begin(), theirs.end());
        return a->when == b.when && mine == theirs;
    }
    if (a->kind == "field") {
        return a->arg == b.arg && a->field == b.field && a->value && b.value && P(*a->value) == P(*b.value);
    }
    if (a->kind == "out_new") {  // SPEC-0053 / 0061
        return a->arg == b.arg && a->field == b.field && a->size && b.size && P(*a->size) == P(*b.size) &&
               a->maybeNull.getValueOr(false) == b.maybeNull.getValueOr(false);
    }
    if (a->kind == "ret_int") {
        std::vector<std::string> x = a->range.getValueOr(std::vector<std::string>());
        std::vector<std::string> y = b.range.getValueOr(std::vector<std::string>());
        if (x.size() != y.size()) {
            return false;
        }
        for (size_t i = 0; i < x.size(); ++i) {
            if (P(x[i]) != P(y[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

// 同 replay.py 的 fmt_apply（ret_int 走 json.dumps 的写法）
std::string FmtApply(const llvm::Optional<summary::Apply> &a)
{
    if (!a) {
        return "?";
    }
    if (a->kind == "ret_new") {
        return "new(" + a->size.getValueOr("") + ")" + (a->maybeNull.getValueOr(false) ? " 或 NULL" : "");
    }
    if (a->kind == "ret_cond") {
        return a->when.getValueOr("") + " ⇒ " + Join(a->then.getValueOr(std::vector<std::string>()), " 且 ");
    }
    if (a->kind == "field") {
        return "实参 " + std::to_string(a->arg.getValueOr(0)) + " 所指对象的 " + a->field.getValueOr("") +
               " := " + a->value.getValueOr("");
    }
    if (a->kind == "out_new") {  // 同 replay.py 的 fmt_apply（SPEC-0053 / 0061）
        std::string lhs = a->field ? "实参 " + std::to_string(a->arg.getValueOr(0)) + " 所指对象的 " + *a->field
                                   : "*实参 " + std::to_string(a->arg.getValueOr(0));
        return lhs + " := new(" + a->size.getValueOr("") + ")" + (a->maybeNull.getValueOr(false) ? " 或 NULL" : "");
    }
    std::string out = "{\"kind\": \"" + a->kind + "\"";
    if (a->range) {
        std::vector<std::string> quoted;
        for (const std::string &r : *a->range) {
            quoted.push_back("\"" + r + "\"");
        }
        out += ", \"range\": [" + Join(quoted, ", ") + "]";
    }
    return out + "}";
}

// 同 replay.py 的 fmt_range
std::string FmtRange(const LinExpr &lo, const LinExpr &hi)
{
    return lo == hi ? Show(lo) : "[" + Show(lo) + ", " + Show(hi) + "]";
}

std::vector<std::string> SameReq(const summary::Req &r, const Outcome &mine)
{
    std::vector<std::string> probs;
    if (P(r.have) != mine.have) {
        probs.push_back("have: JSON " + r.have + " / 回放 " + Show(mine.have));
    }
    if (P(r.endLo) != mine.end2Lo || P(r.endHi) != mine.end2Hi) {
        probs.push_back("end: JSON [" + r.endLo + ", " + r.endHi + "] / 回放 [" + Show(mine.end2Lo) + ", " +
                        Show(mine.end2Hi) + "]");
    }
    std::set<std::string> jg;
    for (const std::string &g : r.guard) {
        for (const LinExpr &atom : C(g)) {
            jg.insert(ShowLe0(atom));
        }
    }
    std::set<std::string> mg;
    for (const LinExpr &g : mine.guards2) {
        mg.insert(ShowLe0(g));
    }
    if (jg != mg) {
        probs.push_back("guard: JSON " + Join(std::vector<std::string>(jg.begin(), jg.end()), "; ") + " / 回放 " +
                        Join(std::vector<std::string>(mg.begin(), mg.end()), "; "));
    }
    if (r.depth != mine.depth) {
        probs.push_back("depth: JSON " + std::to_string(r.depth) + " / 回放 " + std::to_string(mine.depth));
    }
    if (r.obj != mine.obj) {
        probs.push_back("obj: JSON " + r.obj + " / 回放 " + mine.obj);
    }
    return probs;
}

std::string Label(const ArgValue &a)
{
    switch (a.kind) {
        case ArgValue::Kind::Ptr:
            return a.ptr->obj;  // 机器版没有 doc，replay.py 的 label 也取 obj
        case ArgValue::Kind::Expr:
            return a.expr;
        case ArgValue::Kind::Null:
            return "…";
    }
    return "…";
}

}  // namespace

// 同 replay.py 的 topo：按"离叶子的层数"排，同层按名字
const std::string &CalleeOf(const Call &call, const std::map<std::string, Summary> &fns)
{
    if (call.calleeKey && fns.count(*call.calleeKey) != 0) {
        return *call.calleeKey;
    }
    return call.callee;
}

std::vector<std::string> TopoOrder(const std::map<std::string, Summary> &fns)
{
    std::map<std::string, int> level;
    std::function<int(const std::string &, std::vector<std::string> &)> lv = [&](const std::string &n,
                                                                                 std::vector<std::string> &stack) {
        auto known = level.find(n);
        if (known != level.end()) {
            return known->second;
        }
        auto it = fns.find(n);
        if (it == fns.end() || llvm::is_contained(stack, n)) {
            return -1;
        }
        int best = -1;
        stack.push_back(n);
        for (const Call &c : it->second.calls) {
            best = std::max(best, lv(CalleeOf(c, fns), stack));
        }
        stack.pop_back();
        level[n] = best + 1;
        return best + 1;
    };
    std::vector<std::string> names;
    for (const auto &entry : fns) {
        std::vector<std::string> stack;
        lv(entry.first, stack);
        names.push_back(entry.first);
    }
    std::stable_sort(names.begin(), names.end(), [&](const std::string &a, const std::string &b) {
        return std::make_pair(level[a], a) < std::make_pair(level[b], b);
    });
    return names;
}

std::vector<std::string> DiffRep(const summary::Rep &want, const summary::Rep &got)
{
    std::vector<std::string> bad;
    auto cmp = [&](const char *key, const std::string &a, const std::string &b) {
        if (a != b) {
            bad.push_back(std::string(key) + ": JSON " + a + " / 推出 " + b);
        }
    };
    cmp("level", want.level, got.level);
    cmp("reported", want.reported ? "true" : "false", got.reported ? "true" : "false");
    cmp("site", want.site, got.site);
    cmp("access", want.access.fn + "|" + want.access.key + "|" + want.access.at + "|" + want.access.end,
        got.access.fn + "|" + got.access.key + "|" + got.access.at + "|" + got.access.end);
    cmp("kind", want.kind, got.kind);
    cmp("obj", want.obj, got.obj);
    cmp("have", want.have, got.have);
    cmp("end", "[" + want.endLo + ", " + want.endHi + "]", "[" + got.endLo + ", " + got.endHi + "]");
    cmp("bound", want.bound, got.bound);
    cmp("depth", std::to_string(want.depth), std::to_string(got.depth));
    cmp("eff_depth", std::to_string(want.effDepth), std::to_string(got.effDepth));
    cmp("chain", Join(want.chain, " "), Join(got.chain, " "));
    cmp("fp", want.fp, got.fp);
    cmp("via", want.via.getValueOr("null"), got.via.getValueOr("null"));
    cmp("dir", want.dir, got.dir);
    return bad;
}

llvm::Optional<summary::Apply> ApplyEff(const summary::Eff &eff, const Call &call)
{
    Map map = BuildMap(call);
    summary::Apply a;
    a.kind = eff.kind;
    if (eff.kind == "ret_new") {
        a.size = Show(Substitute(P(eff.size.getValueOr("0")), map.m));
        a.maybeNull = eff.maybeNull.getValueOr(false);
        return a;
    }
    if (eff.kind == "ret_int") {
        std::vector<std::string> range;
        for (const std::string &x : eff.range.getValueOr(std::vector<std::string>())) {
            range.push_back(Show(Substitute(P(x), map.m)));
        }
        a.range = range;
        return a;
    }
    if (eff.kind == "ret_cond") {
        a.when = eff.when;
        std::vector<std::string> then;
        for (const std::string &t : eff.then.getValueOr(std::vector<std::string>())) {
            then.push_back(CanonCond(SubstCond(t, map.m)));
        }
        a.then = then;
        return a;
    }
    if (eff.kind == "field") {
        static llvm::Regex re("p([0-9]+)->([A-Za-z0-9_]+)$");
        llvm::SmallVector<llvm::StringRef, 3> groups;
        if (!eff.target || !re.match(*eff.target, &groups)) {
            return llvm::None;
        }
        unsigned long long k = 0;
        groups[1].getAsInteger(10, k);
        a.arg = static_cast<int64_t>(k);
        a.field = groups[2].str();
        a.value = Show(Substitute(P(eff.value.getValueOr("0")), map.m));
        return a;
    }
    if (eff.kind == "out_new") {
        // 出参写回的新对象（SPEC-0053）：第 arg 个实参所指的格（或它所指结构体的字段，SPEC-0061）成了新对象
        static llvm::Regex re("^p([0-9]+)(->([A-Za-z_][A-Za-z0-9_]*))?$");
        llvm::SmallVector<llvm::StringRef, 4> groups;
        if (!eff.target || !re.match(*eff.target, &groups)) {
            return llvm::None;
        }
        unsigned long long k = 0;
        groups[1].getAsInteger(10, k);
        a.arg = static_cast<int64_t>(k);
        if (!groups[3].empty()) {
            a.field = groups[3].str();
        }
        a.size = Show(Substitute(P(eff.size.getValueOr("0")), map.m));
        a.maybeNull = eff.maybeNull.getValueOr(false);
        return a;
    }
    return llvm::None;
}

CheckResult CheckCase(const std::string &caseId, const std::map<std::string, Summary> &fns)
{
    CheckResult out;
    std::set<std::string> callees;  // 有调用方的函数
    for (const auto &entry : fns) {
        for (const Call &c : entry.second.calls) {
            callees.insert(c.callee);
        }
    }
    for (const std::string &fn : TopoOrder(fns)) {
        const Summary &s = fns.at(fn);
        for (const Call &call : s.calls) {
            std::vector<std::string> labels;
            for (const ArgValue &a : call.args) {
                labels.push_back(Label(a));
            }
            out.lines.push_back("- " + fn + " @ " + call.site + "  调用 " + call.callee + "(" + Join(labels, ", ") +
                                ")");
            if (call.results.empty()) {
                out.lines.push_back("    · 被调者没有 Req、没有要用的 Eff");
            }
            auto calleeIt = fns.find(call.callee);
            if (calleeIt == fns.end()) {
                for (const summary::Result &res : call.results) {
                    if (res.kind == summary::Result::Kind::Gen) {
                        out.lines.push_back("    · 访问类模型 " + call.callee + " → 在本函数生成 " +
                                            Join(res.gen, "、"));
                    } else if (res.kind == summary::Result::Kind::GenEff) {
                        std::vector<std::string> effs;
                        for (int64_t i : res.genEff) {
                            effs.push_back("effs[" + std::to_string(i) + "]");
                        }
                        out.lines.push_back("    · 分配器模型 " + call.callee + " → 本函数的效果 " + Join(effs, "、"));
                    } else if (res.kind == summary::Result::Kind::Pure) {
                        out.lines.push_back("    · 纯函数模型 " + call.callee + "：不失效、不产生条件");
                    } else if (res.kind == summary::Result::Kind::Open) {
                        out.lines.push_back("    · 未知函数 " + call.callee + "：记账 " + res.open);
                    }
                }
                continue;
            }
            const Summary &callee = calleeIt->second;
            EnvGuards envGuards = EnvGuardsOf(call);
            for (const summary::Result &res : call.results) {
                if (res.kind == summary::Result::Kind::Eff) {
                    if (res.eff < 0 || static_cast<size_t>(res.eff) >= callee.effs.size()) {
                        out.errors.push_back(caseId + " " + fn + ": 被调者没有 effs[" + std::to_string(res.eff) + "]");
                        continue;
                    }
                    const summary::Eff &eff = callee.effs[res.eff];
                    llvm::Optional<summary::Apply> got = ApplyEff(eff, call);
                    bool ok = SameApply(got, res.apply);
                    out.lines.push_back("    · 效果 " + eff.kind + " → " + FmtApply(got) + " " +
                                        (ok ? "" : "✗ JSON 记的是 " + FmtApply(res.apply)));
                    if (!ok) {
                        out.errors.push_back(caseId + " " + fn + " 效果不一致");
                    }
                    continue;
                }
                if (res.kind != summary::Result::Kind::Req) {
                    continue;
                }
                const summary::Req *r = nullptr;
                for (const summary::Req &x : callee.reqs) {
                    if (x.id == res.req && r == nullptr) {
                        r = &x;
                    }
                }
                if (r == nullptr) {
                    out.errors.push_back(caseId + " " + call.callee + ": 被调者没有 " + res.req);
                    continue;
                }
                llvm::Optional<Call> outer = res.parent ? ParentCall(*r, call) : llvm::None;
                if (res.parent && !outer) {
                    out.errors.push_back(caseId + " " + fn + ": " + call.site + " 的实参没有外层对象（parent）");
                    continue;
                }
                const Call &used = outer ? *outer : call;
                Outcome mine = Classify(*r, used, s, envGuards);
                std::string showEnd = FmtRange(mine.endLo, mine.endHi);
                if (mine.hasBase && (mine.end2Lo != mine.endLo || mine.end2Hi != mine.endHi)) {
                    showEnd += " → 局部换成区间 " + FmtRange(mine.end2Lo, mine.end2Hi);
                }
                std::vector<LinExpr> gs;
                for (const LinExpr &g : mine.guards) {
                    if (!g.IsConst()) {
                        gs.push_back(g);
                    }
                }
                if (mine.outcome == "up") {
                    gs = mine.guards2;
                }
                if (!gs.empty()) {
                    std::vector<std::string> shown;
                    for (const LinExpr &g : gs) {
                        shown.push_back(ShowLe0(g));
                    }
                    showEnd += "，守卫 " + Join(shown, " 且 ");
                }
                bool anyConst = false;
                bool anyFalse = false;
                for (const LinExpr &g : mine.guards) {
                    if (g.IsConst()) {
                        anyConst = true;
                        anyFalse = anyFalse || g.Constant() > 0;
                    }
                }
                if (anyConst) {
                    showEnd += std::string("，守卫在此") + (anyFalse ? "不成立" : "成立");
                }
                std::string msg = "    · " + call.callee + "." + r->id + (res.parent ? "（外层）" : "") + "：末端 " +
                                  showEnd + "，对象 " + mine.obj + "（" + Show(mine.have) + " 字节）→ " + mine.outcome;
                if (!mine.why.empty()) {
                    msg += "（" + mine.why + "）";
                }
                bool ok = mine.outcome == res.outcome;
                if (ok && mine.outcome == "up") {
                    const summary::Req *mineReq = nullptr;
                    for (const summary::Req &x : s.reqs) {
                        if (x.id == res.as && mineReq == nullptr) {
                            mineReq = &x;
                        }
                    }
                    std::vector<std::string> probs = mineReq != nullptr
                                                         ? SameReq(*mineReq, mine)
                                                         : std::vector<std::string>{"JSON 里没有 " + res.as};
                    if (mineReq != nullptr && mineReq->dir != r->dir) {
                        probs.push_back("dir: JSON " + mineReq->dir + " / 被调者 " + r->dir);  // SPEC-0039
                    }
                    msg += " → 本函数 " + res.as;
                    if (!probs.empty()) {
                        ok = false;
                        msg += " ✗ " + Join(probs, "；");
                    }
                }
                if (ok && (mine.outcome == "L1" || mine.outcome == "L2")) {
                    const summary::Rep *rep = nullptr;
                    for (const summary::Rep &x : s.reps) {
                        if (x.id == res.report && rep == nullptr) {
                            rep = &x;
                        }
                    }
                    summary::Rep got = RepOf(mine, used, fn, s, *r);
                    std::vector<std::string> bad =
                        rep != nullptr ? DiffRep(*rep, got) : std::vector<std::string>{"JSON 里没有 " + res.report};
                    if (!bad.empty()) {
                        ok = false;
                        msg += " ✗ 判定记录不一致：" + Join(bad, "；");
                    } else {
                        msg += " → " + res.report + "（depth " + std::to_string(mine.depth) + ", eff_depth " +
                               std::to_string(mine.effDepth) + "，" +
                               (got.reported ? std::string("报")
                                             : "≤ " + std::to_string(kInlineDepth) + " 层，交现有规则") +
                               "）";
                    }
                }
                if (ok && mine.outcome == "open" && mine.reason != res.reason) {
                    ok = false;
                    msg += " ✗ 账本原因 JSON " + res.reason + " / 回放 " + mine.reason;
                }
                if (!ok && mine.outcome != res.outcome) {
                    msg += " ✗ JSON 记的是 " + res.outcome;
                }
                if (!ok) {
                    out.errors.push_back(caseId + " " + fn + " @ " + call.site + " " + r->id);
                }
                out.lines.push_back(msg);
            }
        }
        if (callees.count(fn) == 0 && !s.reqs.empty()) {
            for (const summary::Req &r : s.reqs) {
                const summary::Open *op = nullptr;
                for (const summary::Open &x : s.opens) {
                    if (x.req && *x.req == r.id && op == nullptr) {
                        op = &x;
                    }
                }
                out.lines.push_back("- " + fn + " 是入口（没有调用方）：" + r.id + " 到此为止 → 记账 " +
                                    (op != nullptr ? op->reason : std::string("✗ 缺账本记录")));
                if (op == nullptr) {
                    out.errors.push_back(caseId + " " + fn + " 入口 Req 未记账");
                }
            }
        }
    }
    return out;
}

}  // namespace apply
}  // namespace oobsum
