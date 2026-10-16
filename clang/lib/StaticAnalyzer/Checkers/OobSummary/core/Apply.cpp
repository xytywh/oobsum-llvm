//===-- Apply.cpp - Call-site substitution and decision ---------*- C++ -*-===//
//
// 见 Apply.h。每个函数都标了它对应的 replay.py 函数；说明文字（why）与 replay.py 逐字相同（Dump=apply 与 replay.py -v 逐行比对）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Apply.h"

#include "OobSummary/core/Fingerprint.h"

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
    for (size_t i = 0; i < call.args.size(); ++i) {
        std::string p = "p" + std::to_string(i);
        const ArgValue &arg = call.args[i];
        if (arg.kind == ArgValue::Kind::Ptr) {
            out.m.ext[p] = P(arg.ptr->have);
            out.effDepth = std::max(out.effDepth, arg.ptr->effDepth.getValueOr(0));
            if (arg.ptr->alias) {
                out.m.alias[p] = *arg.ptr->alias;
            }
            if (arg.ptr->fields) {
                for (const auto &entry : *arg.ptr->fields) {
                    std::string name = p + "->" + entry.first;
                    if (entry.second.kind == ArgValue::Kind::Ptr) {
                        out.m.ext[name] = P(entry.second.ptr->have);
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

Eliminated EliminateLocals(const LinExpr &l, const Locals &locs, bool upper)
{
    Eliminated r;
    r.out = LinExpr::Const(l.Constant());
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
        if (name.consume_front("p")) {
            llvm::StringRef digits = name.take_while([](char c) { return c >= '0' && c <= '9'; });
            llvm::StringRef rest = name.drop_front(digits.size());
            if (!digits.empty() && (rest.empty() || rest.startswith("->"))) {
                return true;
            }
        }
        return llvm::StringRef(a.name).startswith("g:");
    }
    if (a.kind == Atom::Kind::Ext) {
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
    }
    if (caller.fields) {
        for (const auto &entry : *caller.fields) {
            add(entry.first, entry.second.type.getValueOr(""));
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

Outcome Classify(const summary::Req &req, const Call &call, const Summary &caller, const EnvGuards &envGuards)
{
    Map map = BuildMap(call);
    Outcome o;
    o.have = Substitute(P(req.have), map.m);
    o.endLo = Substitute(P(req.endLo), map.m);
    o.endHi = Substitute(P(req.endHi), map.m);
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
    // ② 局部换成区间
    Eliminated hi = EliminateLocals(o.endHi, locs, true);
    Eliminated lo = EliminateLocals(o.endLo, locs, false);
    o.hasBase = true;
    o.end2Hi = hi.out;
    o.end2Lo = lo.out;
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
        o.reason = o.bound == "type" ? "type_range_only" : "pointer_stepping";
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
    o.reason = "entry_unproven";
    o.why = "含调用方局部且没有界";
    return o;
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
    rep.fp = DecisionFingerprint(rep.level, caller.key, rep.access.key, rep.kind, rep.access.end, rep.obj);
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
            best = std::max(best, lv(c.callee, stack));
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
                Outcome mine = Classify(*r, call, s, envGuards);
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
                std::string msg = "    · " + call.callee + "." + r->id + "：末端 " + showEnd + "，对象 " + mine.obj +
                                  "（" + Show(mine.have) + " 字节）→ " + mine.outcome;
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
                    summary::Rep got = RepOf(mine, call, fn, s, *r);
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
