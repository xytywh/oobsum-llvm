//===-- Solver.cpp - Bottom-up propagation of requirements ------*- C++ -*-===//
//
// 见 Solver.h。逐项对照 replay.py 的 derive / absorb / req_key（SPEC-0013）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Solver.h"

#include "OobSummary/core/Apply.h"
#include "OobSummary/core/Range.h"

#include <algorithm>
#include <set>

namespace oobsum {
namespace solver {

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

std::string GuardKey(const std::vector<LinExpr> &guards)
{
    std::set<std::string> shown;
    for (const LinExpr &g : guards) {
        shown.insert(ShowLe0(g));
    }
    std::string out;
    for (const std::string &s : shown) {
        out += (out.empty() ? "" : " ; ") + s;
    }
    return out;
}

std::string Key(const LinExpr &have, const std::string &obj, const LinExpr &lo, const LinExpr &hi,
                const std::vector<LinExpr> &guards, int64_t depth)
{
    return have.Show() + " | " + obj + " | " + lo.Show() + " | " + hi.Show() + " | {" + GuardKey(guards) + "} | " +
           std::to_string(depth);
}

// 推出的 Req → 交给 Classify 的被调者 Req（同 derive 里的 rj：字符串都是规范打印）
summary::Req AsReq(const DerivedReq &r)
{
    summary::Req out;
    out.have = r.have.Show();
    out.obj = r.obj;
    out.endLo = r.lo.Show();
    out.endHi = r.hi.Show();
    for (const LinExpr &g : r.guards) {
        out.guard.push_back(ShowLe0(g));
    }
    out.depth = r.depth;
    out.effDepth = r.effDepth;
    out.bound = r.bound;
    out.access = r.access;
    out.path = r.path;
    out.kind = r.kind;
    return out;
}

}  // namespace

std::string ReqKey(const DerivedReq &r)
{
    return Key(r.have, r.obj, r.lo, r.hi, r.guards, r.depth);
}

std::string ReqKey(const summary::Req &r)
{
    std::vector<LinExpr> guards;
    for (const std::string &g : r.guard) {
        std::vector<LinExpr> atoms = C(g);
        guards.insert(guards.end(), atoms.begin(), atoms.end());
    }
    return Key(P(r.have), r.obj, P(r.endLo), P(r.endHi), guards, r.depth);
}

// SPEC-0019 §2.1：对象是入口自己的形参所指 *pK、have 就是 ext(pK)、末端上界是常数且 ≤ 形参声明的所指类型大小 → typed_field
bool TypedField(const DerivedReq &r, const summary::Summary &s)
{
    if (r.obj.size() < 3 || r.obj.compare(0, 2, "*p") != 0 || !r.hi.IsConst()) {
        return false;
    }
    size_t k = 0;
    for (size_t i = 2; i < r.obj.size(); ++i) {
        if (r.obj[i] < '0' || r.obj[i] > '9') {
            return false;  // *p0->pucBuf 之类：不是形参自己所指的对象
        }
        k = k * 10 + static_cast<size_t>(r.obj[i] - '0');
    }
    if (k >= s.params.size() || !s.params[k].size || !(r.have == LinExpr::Ext(r.obj.substr(1)))) {
        return false;
    }
    return r.hi.Constant() <= *s.params[k].size;
}

std::vector<DerivedReq> Absorb(const std::vector<DerivedReq> &cand)
{
    std::vector<DerivedReq> keep;
    for (size_t i = 0; i < cand.size(); ++i) {
        const DerivedReq &r = cand[i];
        bool dominated = false;
        for (size_t j = 0; j < cand.size() && !dominated; ++j) {
            const DerivedReq &r2 = cand[j];
            if (i == j || r.have.Show() != r2.have.Show() || r.obj != r2.obj || r.gkey != r2.gkey) {
                continue;
            }
            LinExpr d = r.hi - r2.hi;
            if (d.IsConst() && (d.Constant() < 0 || (d.Constant() == 0 && j < i))) {
                dominated = true;
            }
        }
        if (!dominated) {
            keep.push_back(r);
        }
    }
    return keep;
}

PropagateResult Propagate(const std::map<std::string, summary::Summary> &fns)
{
    PropagateResult out;
    std::set<std::string> callees;
    for (const auto &entry : fns) {
        for (const summary::Call &c : entry.second.calls) {
            callees.insert(c.callee);
        }
    }
    out.order = apply::TopoOrder(fns);
    for (const std::string &fn : out.order) {
        const summary::Summary &s = fns.at(fn);
        FunctionResult &result = out.fns[fn];
        std::vector<DerivedReq> cand;
        // 本函数访问点当场生成的 Req（depth 0，含被覆盖的 absorbed：每个访问各一条）
        for (const summary::Req &r : s.reqs) {
            if (r.depth != 0) {
                continue;  // 上传来的不拿，自己推
            }
            std::vector<LinExpr> guards;
            for (const std::string &g : r.guard) {
                std::vector<LinExpr> atoms = C(g);
                guards.insert(guards.end(), atoms.begin(), atoms.end());
            }
            struct Access1 {
                std::string kind;
                std::string lo;
                std::string hi;
                std::string at;
            };
            std::vector<Access1> accesses{{r.kind, r.endLo, r.endHi, r.access.at}};
            if (r.absorbed) {
                for (const summary::Absorbed &a : *r.absorbed) {
                    accesses.push_back({a.kind, a.endLo, a.endHi, a.at});
                }
            }
            for (const Access1 &a : accesses) {
                DerivedReq d;
                d.have = P(r.have);
                d.obj = r.obj;
                d.lo = P(a.lo);
                d.hi = P(a.hi);
                d.guards = guards;
                d.gkey = GuardKey(guards);
                d.depth = 0;
                d.effDepth = r.effDepth;
                d.bound = r.bound;
                d.path = r.path;
                d.access = r.access;
                d.kind = a.kind;
                cand.push_back(std::move(d));
            }
        }
        // 每个调用点：被调者推出的 Req 代进来
        for (const summary::Call &call : s.calls) {
            auto calleeIt = out.fns.find(call.callee);
            if (fns.count(call.callee) == 0 || calleeIt == out.fns.end()) {
                continue;
            }
            apply::EnvGuards envGuards = apply::EnvGuardsOf(call);
            for (const DerivedReq &r : calleeIt->second.reqs) {
                summary::Req rj = AsReq(r);
                apply::Outcome mine = apply::Classify(rj, call, s, envGuards);
                std::vector<std::string> path{fn + "@" + call.site};
                path.insert(path.end(), r.path.begin(), r.path.end());
                if (mine.outcome == "up") {
                    DerivedReq d;
                    d.have = mine.have;
                    d.obj = mine.obj;
                    d.lo = mine.end2Lo;
                    d.hi = mine.end2Hi;
                    d.guards = mine.guards2;
                    d.gkey = GuardKey(mine.guards2);
                    d.depth = mine.depth;
                    d.effDepth = mine.effDepth;
                    d.bound = mine.bound;
                    d.path = path;
                    d.access = r.access;
                    d.kind = r.kind;
                    cand.push_back(std::move(d));
                    ++out.up;
                } else if (mine.outcome == "L1" || mine.outcome == "L2") {
                    result.reps.push_back(apply::RepOf(mine, call, fn, s, rj));
                    ++out.rep;
                }
            }
        }
        result.reqs = Absorb(cand);
        // 入口（没有调用方）：每条推出的 Req 到此为止，记账
        if (callees.count(fn) == 0) {
            result.entry = true;
            SymEnv env = apply::CallerEnv(s);
            for (const DerivedReq &r : result.reqs) {
                Limit l = env.RangeOf(r.hi - r.have, true);
                bool typeOnly = r.have.IsConst() && l.known && l.tag == Tag::Type;
                result.entryOpens.push_back(TypedField(r, s) ? "typed_field"
                                                             : (typeOnly ? "type_range_only" : "entry_unproven"));
                ++out.entry;
            }
        }
    }
    return out;
}

}  // namespace solver
}  // namespace oobsum
