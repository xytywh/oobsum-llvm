//===-- Engine.cpp - One-TU pipeline from FIR to decisions ------*- C++ -*-===//
//
// 见 Engine.h。规则见 SPEC-0021 §2。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Engine.h"

#include "OobSummary/core/Alert.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Label.h"

#include "llvm/Support/Path.h"

#include <functional>

namespace oobsum {
namespace engine {

namespace {

// 摘要里的位置是 文件名:行（D-23）；判定要完整路径（Reporter 按它找 FileEntry），按本 TU 的文件表换回
class SiteMap {
public:
    explicit SiteMap(const fir::Module &m)
    {
        for (const std::string &file : m.files) {
            std::string base = llvm::sys::path::filename(file, llvm::sys::path::Style::posix).str();
            paths.emplace(base, file);  // 同名取第一个
        }
    }
    Site Of(const std::string &site) const
    {
        Site out;
        size_t colon = site.rfind(':');
        std::string base = colon == std::string::npos ? site : site.substr(0, colon);
        if (colon != std::string::npos) {
            out.line = static_cast<unsigned>(std::strtoul(site.c_str() + colon + 1, nullptr, 10));
        }
        auto it = paths.find(base);
        out.file = it != paths.end() ? it->second : base;
        return out;
    }
    // 函数@文件名:行 → Hop
    Hop HopOf(const std::string &hop) const
    {
        Hop h;
        size_t at = hop.find('@');
        h.fn = at == std::string::npos ? hop : hop.substr(0, at);
        h.site = Of(at == std::string::npos ? "" : hop.substr(at + 1));
        return h;
    }
    std::vector<Hop> Chain(const std::vector<std::string> &path) const
    {
        std::vector<Hop> out;
        for (const std::string &p : path) {
            out.push_back(HopOf(p));
        }
        return out;
    }

private:
    std::map<std::string, std::string> paths;
};

std::string Word(const std::string &kind)
{
    return kind == "W" ? "write" : "read";
}

// 大小：常数写 "N bytes"，符号写 "size ext(p0)"
std::string SizeText(const std::string &have)
{
    bool number = !have.empty() && have.find_first_not_of("0123456789") == std::string::npos;
    return number ? have + " bytes" : "size " + have;
}

}  // namespace

std::string SuspectMessage(const std::string &kind, int64_t depth, const std::string &object, const std::string &have,
                           const std::string &endHi)
{
    std::string levels = depth > 0 ? " through a " + std::to_string(depth) + "-level call chain" : "";
    return "Cannot prove the " + Word(kind) + levels + " stays within " + object + " (" + SizeText(have) +
           "): up to byte " + endHi + " may be accessed";
}

std::string SafeMessage(const std::string &object, const std::string &have, const std::string &endHi)
{
    return "Proven within bounds: up to byte " + endHi + " of " + object + " (" + SizeText(have) + ")";
}

std::string LedgerMessage(const std::string &reason, const std::string &callee)
{
    std::string phrase;
    if (reason == "untracked_pointer") {
        phrase = "the pointer's target is not tracked";
    } else if (reason == "alloc_size_unknown") {
        phrase = "the size of the object is unknown";
    } else if (reason == "type_range_only") {
        phrase = "the offset is bounded only by its type";
    } else if (reason == "pointer_stepping") {
        phrase = "the pointer is advanced in a loop";
    } else if (reason == "not_in_tu") {
        phrase = "a pointer is passed to '" + callee + "', whose body is not in this translation unit";
    } else if (reason.compare(0, 11, "unanalyzed:") == 0) {
        phrase = "the function was not fully analyzed (" + reason.substr(11) + ")";
    } else {
        phrase = reason;
    }
    return "Cannot prove this access is within bounds: " + phrase;
}

// FIR 的直接调用图上 DFS 后序：被调者先（递归成环的按任意顺序，SPEC-0024 §2.3）
static std::vector<size_t> BottomUp(const fir::Module &m)
{
    std::map<std::string, size_t> byKey;
    for (size_t i = 0; i < m.funcs.size(); ++i) {
        byKey[m.funcs[i].key] = i;
    }
    std::vector<int> mark(m.funcs.size(), 0);
    std::vector<size_t> order;
    std::function<void(size_t)> visit = [&](size_t i) {
        mark[i] = 1;
        for (const fir::Block &b : m.funcs[i].blocks) {
            for (const fir::Inst &inst : b.insts) {
                if (inst.op != fir::InstOp::Call || inst.calleeExpr != fir::kNone) {
                    continue;
                }
                auto it = byKey.find(inst.callee);
                if (it != byKey.end() && mark[it->second] == 0) {
                    visit(it->second);
                }
            }
        }
        mark[i] = 2;
        order.push_back(i);
    };
    for (size_t i = 0; i < m.funcs.size(); ++i) {
        if (mark[i] == 0) {
            visit(i);
        }
    }
    return order;
}

InlineResult RunInline(const fir::Module &m, const models::ModelTable &models,
                       const std::function<bool(const std::string &)> &traceFilter)
{
    InlineResult out;
    // ① 自底向上：函数内解释（代入已算出的被调者的效果）→ 摘要（只有局部事实）
    for (size_t i : BottomUp(m)) {
        const fir::Function &f = m.funcs[i];
        std::vector<std::string> trace;
        bool traced = traceFilter && traceFilter(f.name);
        interp::FunctionFacts facts = interp::AnalyzeFunction(m, f, traced ? &trace : nullptr, models, &out.summaries);
        out.summaries[f.key] = interp::BuildSummary(m, f, facts);  // 按函数键（SPEC-0027）
        out.facts[f.key] = std::move(facts);
        out.order.push_back(f.key);
        if (traced) {
            out.traces[f.name] = std::move(trace);
        }
    }
    // ② 推到底
    out.propagate = solver::Propagate(out.summaries);
    SiteMap sites(m);
    auto base = [&](Label label, const std::string &reason, const std::string &fn) {
        Decision d;
        d.label = label;
        d.reason = reason;
        d.fn = out.summaries.at(fn).key;
        d.origin = "engine";
        return d;
    };
    // ③ 判定（SPEC-0021 §2.2）
    for (const std::string &fn : out.propagate.order) {
        const summary::Summary &s = out.summaries.at(fn);
        const solver::FunctionResult &result = out.propagate.fns.at(fn);
        // 调用点判出的 L1 / L2
        for (const summary::Rep &rep : result.reps) {
            Decision d = base(rep.level == "L1" ? Label::Definite : Label::Possible, "", fn);
            d.site = sites.Of(rep.site);
            d.kind = rep.kind;
            d.obj = rep.obj;
            d.have = rep.have;
            d.endLo = rep.endLo;
            d.endHi = rep.endHi;
            d.bound = rep.bound;
            d.depth = static_cast<unsigned>(rep.depth);
            d.effDepth = static_cast<unsigned>(rep.effDepth);
            d.chain = sites.Chain(rep.chain);
            d.msg = alert::AlertMessage(rep, s.params);
            d.fp = rep.fp;
            out.decisions.push_back(std::move(d));
        }
        // 调用点上判安全 / 记账的
        for (const solver::SiteOutcome &so : result.sites) {
            const apply::Outcome &mine = so.mine;
            auto calleeSummary = out.summaries.find(so.callee);
            std::string calleeName = calleeSummary != out.summaries.end() ? calleeSummary->second.fn : so.callee;
            std::string line =
                s.fn + " @ " + so.site + " " + calleeName + "." + so.req.access.at + " → " + mine.outcome;
            if (!mine.reason.empty()) {
                line += " (" + mine.reason + ")";
            }
            out.applyLines.push_back(line);
            if (mine.outcome != "safe" && mine.outcome != "open") {
                continue;
            }
            bool safe = mine.outcome == "safe";
            Decision d = base(safe ? Label::Safe : Label::Suspect, mine.reason, fn);
            d.site = sites.Of(so.site);
            d.kind = so.req.kind;
            d.obj = mine.obj;
            d.have = mine.have.Show();
            d.endLo = (mine.hasBase ? mine.end2Lo : mine.endLo).Show();
            d.endHi = (mine.hasBase ? mine.end2Hi : mine.endHi).Show();
            d.bound = mine.hasBase ? mine.bound : so.req.bound;
            d.depth = static_cast<unsigned>(mine.hasBase ? mine.depth : so.req.depth + 1);
            d.effDepth = static_cast<unsigned>(mine.hasBase ? mine.effDepth : so.req.effDepth);
            std::vector<std::string> path{fn + "@" + so.site};
            path.insert(path.end(), so.req.path.begin(), so.req.path.end());
            d.chain = sites.Chain(path);
            // 对象代换不过来（成员的 Req 落到调用方的局部数组上）：用被调者的形参名写成员
            std::string object = d.obj == "?" && out.summaries.count(so.callee) != 0
                                     ? alert::ObjName(so.req.obj, out.summaries.at(so.callee).params)
                                     : alert::ObjName(d.obj, s.params);
            d.msg =
                safe ? SafeMessage(object, d.have, d.endHi) : SuspectMessage(d.kind, d.depth, object, d.have, d.endHi);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, so.req.access.key, d.kind, so.req.access.end, d.obj);
            out.decisions.push_back(std::move(d));
        }
        // 入口函数上推不下去的 Req（A-80）
        if (result.entry) {
            for (size_t i = 0; i < result.reqs.size(); ++i) {
                const solver::DerivedReq &r = result.reqs[i];
                Decision d =
                    base(Label::Suspect, i < result.entryOpens.size() ? result.entryOpens[i] : "entry_unproven", fn);
                d.site = sites.Of(r.depth == 0 ? r.access.at : s.loc);
                d.kind = r.kind;
                d.obj = r.obj;
                d.have = r.have.Show();
                d.endLo = r.lo.Show();
                d.endHi = r.hi.Show();
                d.bound = r.bound;
                d.depth = static_cast<unsigned>(r.depth);
                d.effDepth = static_cast<unsigned>(r.effDepth);
                d.chain = sites.Chain(r.path);
                d.msg = SuspectMessage(d.kind, d.depth, alert::ObjName(d.obj, s.params), d.have, d.endHi);
                d.fp = DecisionFingerprint(LabelName(d.label), s.key, r.access.key, d.kind, r.access.end, d.obj);
                out.decisions.push_back(std::move(d));
            }
        }
    }
    for (const auto &entry : out.summaries) {
        const std::string &fn = entry.first;
        const summary::Summary &s = entry.second;
        const interp::FunctionFacts &facts = out.facts.at(fn);
        // 本函数当场证明安全的访问
        for (const summary::Req &r : facts.safe) {
            Decision d = base(Label::Safe, "local_entailed", fn);
            d.site = sites.Of(r.access.at);
            d.kind = r.kind;
            d.obj = r.obj;
            d.have = r.have;
            d.endLo = r.endLo;
            d.endHi = r.endHi;
            d.bound = r.bound;
            d.chain = sites.Chain(r.path);
            d.msg = SafeMessage(alert::ObjName(d.obj, s.params), d.have, d.endHi);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, r.access.key, d.kind, r.access.end, d.obj);
            out.decisions.push_back(std::move(d));
        }
        // 本函数的账本
        for (const summary::Open &o : facts.opens) {
            Decision d = base(Label::Suspect, o.reason, fn);
            d.site = sites.Of(o.at);
            d.chain = {sites.HopOf(fn + "@" + o.at)};
            d.msg = LedgerMessage(o.reason);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, o.at, "", "", o.reason);
            out.decisions.push_back(std::move(d));
        }
        // 没分析完的函数：函数声明处一条
        if (s.status.compare(0, 8, "partial:") == 0) {
            std::string reason = "unanalyzed:" + s.status.substr(8);
            Decision d = base(Label::Suspect, reason, fn);
            d.site = sites.Of(s.loc);
            d.msg = LedgerMessage(reason);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, s.loc, "", "", reason);
            out.decisions.push_back(std::move(d));
        }
        // 指针流进本 TU 没有函数体、也不在模型表里的被调者（D-27 not_in_tu）：每个调用点一条
        for (const summary::Call &c : s.calls) {
            if (out.summaries.count(apply::CalleeOf(c, out.summaries)) != 0 ||
                models.KindOf(c.callee) != models::Kind::None) {
                continue;
            }
            bool pointer = false;
            for (const summary::ArgValue &a : c.args) {
                pointer = pointer || a.kind != summary::ArgValue::Kind::Expr;
            }
            if (!pointer) {
                continue;
            }
            Decision d = base(Label::Suspect, "not_in_tu", fn);
            d.site = sites.Of(c.site);
            d.chain = {sites.HopOf(fn + "@" + c.site)};
            d.msg = LedgerMessage("not_in_tu", c.callee);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, c.site, "", "", "not_in_tu:" + c.callee);
            out.decisions.push_back(std::move(d));
        }
    }
    return out;
}

}  // namespace engine
}  // namespace oobsum
