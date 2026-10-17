//===-- Engine.cpp - One-TU pipeline from FIR to decisions ------*- C++ -*-===//
//
// 见 Engine.h。规则见 SPEC-0021 §2。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Engine.h"

#include "OobSummary/core/Alert.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Key.h"
#include "OobSummary/core/Label.h"

#include "llvm/Support/Path.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <set>

namespace oobsum {
namespace engine {

namespace {

// 摘要里的位置是 文件名:行（D-23）；判定要完整路径（Reporter 按它找 FileEntry），按本 TU 的文件表换回
class SiteMap {
public:
    explicit SiteMap(const std::vector<const fir::Module *> &mods)
    {
        for (const fir::Module *m : mods) {
            for (const std::string &file : m->files) {
                std::string base = llvm::sys::path::filename(file, llvm::sys::path::Style::posix).str();
                paths.emplace(base, file);  // 同名取第一个
            }
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
    if (llvm::StringRef(have).startswith("_unknown") || have.find("_unmapped:") != std::string::npos) {
        return "size unknown";  // 不跟踪的指针实参（SPEC-0062）、调用方不知道的字段指针（SPEC-0063）
    }
    return number ? have + " bytes" : "size " + have;
}

}  // namespace

// 调用点上不知道的符号（SPEC-0063 / 0064）显示时去掉内部前缀；给了被调者的形参表时 p<k> 写成被调者的形参名（SPEC-0067）
std::string Readable(std::string text, const std::vector<summary::Param> *callee = nullptr)
{
    text = StripGlobalSuffix(text);  // 内部链接全局符号的身份后缀（SPEC-0086）
    // 伪字段 _0（*pK，SPEC-0071）的未知值没有名字可写
    for (size_t at = text.find("_unknown__0"); at != std::string::npos; at = text.find("_unknown__0", at)) {
        text.replace(at, 11, "an unknown value");
    }
    // 调用点上不知道的整数实参 _unknown<数字>：没有名字可写（SPEC-0092）
    for (size_t at = text.find("_unknown"); at != std::string::npos; at = text.find("_unknown", at + 1)) {
        size_t end = at + 8;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        bool ident = end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
        if (end > at + 8 && !ident) {
            text.replace(at, end - at, "an unknown value");
        }
    }
    // 调用点记录里不知道的字段值的占位符 _unknown_<字段>：写成字段名（SPEC-0068）
    for (size_t at = text.find("_unknown_"); at != std::string::npos; at = text.find("_unknown_", at)) {
        text.erase(at, 9);
    }
    // 指向整数的形参的伪字段：pK->_0 写成 pK[0]（SPEC-0071）
    for (size_t at = text.find("->_0"); at != std::string::npos; at = text.find("->_0", at)) {
        text.replace(at, 4, "[0]");
    }
    for (size_t at = text.find("_unmapped:"); at != std::string::npos; at = text.find("_unmapped:", at)) {
        text.erase(at, 10);
        size_t p = at;
        for (const char *wrap : {"ext(", "bext("}) {
            if (text.compare(at, std::strlen(wrap), wrap) == 0) {
                p = at + std::strlen(wrap);
            }
        }
        size_t digits = p + 1;
        while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits]))) {
            ++digits;
        }
        unsigned k = 0;
        if (callee != nullptr && p < text.size() && text[p] == 'p' && digits > p + 1 &&
            !llvm::StringRef(text).substr(p + 1, digits - p - 1).getAsInteger(10, k) && k < callee->size()) {
            text.replace(p, digits - p, (*callee)[k].name);
        }
    }
    return text;
}

std::string SuspectMessage(const std::string &kind, int64_t depth, const std::string &object, const std::string &have,
                           const std::string &rawEndHi, const std::string &dir)
{
    std::string endHi = Readable(rawEndHi);
    std::string levels = depth > 0 ? " through a " + std::to_string(depth) + "-level call chain" : "";
    if (!dir.empty()) {  // 负偏移（SPEC-0039 §2.3）
        return "Cannot prove the " + Word(kind) + levels + " stays within " + object + " (" + SizeText(have) +
               " before the pointer): up to " + endHi + " bytes before it may be accessed";
    }
    return "Cannot prove the " + Word(kind) + levels + " stays within " + object + " (" + SizeText(have) +
           "): up to byte " + endHi + " may be accessed";
}

std::string SafeMessage(const std::string &object, const std::string &have, const std::string &endHi,
                        const std::string &dir)
{
    if (!dir.empty()) {
        return "Proven within bounds: up to " + endHi + " bytes before " + object + " (" + SizeText(have) +
               " before the pointer)";
    }
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
    } else if (reason == "subobject_overflow") {
        // SPEC-0104：越出成员数组 / 多维数组的一行（整个对象由外层的检查判）
        phrase = "it may leave the array member or the row of the multi-dimensional array it starts in";
    } else if (reason == "recursion") {
        phrase = "the call is part of a recursion; accesses deeper in the recursion are not checked";  // SPEC-0066
    } else if (reason == "unsupported_construct") {
        phrase = "the address or size comes from a construct the engine does not support";  // SPEC-0060
    } else if (reason == "struct_by_value") {
        phrase = "the value comes from a structure passed or returned by value";
    } else if (reason == "inline_asm") {
        phrase = "the value is written by inline assembly";
    } else if (reason == "fptr") {
        phrase = "a pointer is passed through a function pointer whose target is not resolved";  // SPEC-0057
    } else if (reason == "not_in_tu") {
        phrase = "a pointer is passed to '" + callee + "', whose body is not in this translation unit";
    } else if (reason == "unknown_callee") {
        phrase = "a pointer is passed to '" + callee + "', which is not defined in any analyzed translation unit";
    } else if (reason.compare(0, 11, "unanalyzed:") == 0) {
        phrase = "the function was not fully analyzed (" + reason.substr(11) + ")";
    } else {
        phrase = reason;
    }
    return "Cannot prove this access is within bounds: " + phrase;
}

// 所有模块的函数按函数键去重（头文件里的 static inline 各 TU 一份，取第一份，D-08）；全局直接调用图上 DFS 后序：被调者先
// （递归成环的按任意顺序，SPEC-0024 §2.3、SPEC-0029 §2.1）
static std::vector<std::pair<size_t, size_t>> BottomUp(const std::vector<const fir::Module *> &mods)
{
    std::map<std::string, std::pair<size_t, size_t>> byKey;
    std::vector<std::pair<size_t, size_t>> all;
    for (size_t mi = 0; mi < mods.size(); ++mi) {
        for (size_t fi = 0; fi < mods[mi]->funcs.size(); ++fi) {
            if (byKey.emplace(mods[mi]->funcs[fi].key, std::make_pair(mi, fi)).second) {
                all.push_back({mi, fi});
            }
        }
    }
    std::set<std::string> visited;
    std::vector<std::pair<size_t, size_t>> order;
    std::function<void(std::pair<size_t, size_t>)> visit = [&](std::pair<size_t, size_t> at) {
        const fir::Function &f = mods[at.first]->funcs[at.second];
        visited.insert(f.key);
        for (const fir::Block &b : f.blocks) {
            for (const fir::Inst &inst : b.insts) {
                if (inst.op != fir::InstOp::Call || inst.calleeExpr != fir::kNone) {
                    continue;
                }
                auto it = byKey.find(inst.callee);
                if (it != byKey.end() && visited.count(inst.callee) == 0) {
                    visit(it->second);
                }
            }
        }
        // 取了地址的函数也先算（局部函数指针解析成的被调者，SPEC-0058）
        for (const fir::Expr &x : f.exprs) {
            if (x.op != fir::ExprOp::FAddr) {
                continue;
            }
            auto it = byKey.find(x.text);
            if (it != byKey.end() && visited.count(x.text) == 0) {
                visit(it->second);
            }
        }
        order.push_back(at);
    };
    for (const auto &at : all) {
        if (visited.count(mods[at.first]->funcs[at.second].key) == 0) {
            visit(at);
        }
    }
    return order;
}

// 递归调用点记账（D-27 recursion，SPEC-0066）：按摘要的调用点记录建调用图，强连通分量内的每条调用边（含自己调自己）
// 在调用方的账本里记一条——环上不求不动点，经递归传上来的条件会丢，不能让剩下的部分被一条 SAFE 盖住
static void RecursionLedger(InlineResult &out)
{
    std::map<std::string, std::vector<std::string>> succ;
    for (const auto &entry : out.summaries) {
        std::vector<std::string> &to = succ[entry.first];
        for (const summary::Call &c : entry.second.calls) {
            const std::string &callee = apply::CalleeOf(c, out.summaries);
            if (out.summaries.count(callee) != 0) {
                to.push_back(callee);
            }
        }
    }
    std::map<std::string, int> order;
    std::map<std::string, int> low;
    std::map<std::string, int> sccOf;
    std::set<std::string> onStack;
    std::vector<std::string> stack;
    int counter = 0;
    int sccs = 0;
    std::function<void(const std::string &)> visit = [&](const std::string &v) {
        order[v] = low[v] = counter++;
        stack.push_back(v);
        onStack.insert(v);
        for (const std::string &w : succ[v]) {
            if (order.count(w) == 0) {
                visit(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack.count(w) != 0) {
                low[v] = std::min(low[v], order[w]);
            }
        }
        if (low[v] == order[v]) {
            std::string w;
            do {
                w = stack.back();
                stack.pop_back();
                onStack.erase(w);
                sccOf[w] = sccs;
            } while (w != v);
            ++sccs;
        }
    };
    for (const auto &entry : out.summaries) {
        if (order.count(entry.first) == 0) {
            visit(entry.first);
        }
    }
    for (auto &entry : out.summaries) {
        for (const summary::Call &c : entry.second.calls) {
            const std::string &callee = apply::CalleeOf(c, out.summaries);
            if (out.summaries.count(callee) == 0 || sccOf[callee] != sccOf[entry.first]) {
                continue;
            }
            std::vector<summary::Open> &opens = entry.second.opens;
            bool seen = std::any_of(opens.begin(), opens.end(),
                                    [&](const summary::Open &o) { return o.reason == "recursion" && o.at == c.site; });
            if (!seen) {
                opens.push_back({"recursion", c.site, llvm::None});
                out.facts[entry.first].opens.push_back({"recursion", c.site, llvm::None});
            }
        }
    }
}

InlineResult RunInline(const fir::Module &m, const models::ModelTable &models,
                       const std::function<bool(const std::string &)> &traceFilter, const interp::Options &options)
{
    return RunProgram({&m}, models, traceFilter, false, options);
}

InlineResult RunSolve(const std::vector<const fir::Module *> &mods, const models::ModelTable &models,
                      const interp::Options &options)
{
    return RunProgram(mods, models, nullptr, true, options);
}

InlineResult RunProgram(const std::vector<const fir::Module *> &mods, const models::ModelTable &models,
                        const std::function<bool(const std::string &)> &traceFilter, bool linked,
                        const interp::Options &options)
{
    InlineResult out;
    // 全局大小表（D-34，SPEC-0032）：类型完整的全局按键记大小，定义优先
    std::map<std::string, int64_t> globalSizes;
    for (int pass = 0; pass < 2; ++pass) {
        for (const fir::Module *mod : mods) {
            for (const fir::Global &g : mod->globals) {
                int64_t size = 0;
                if ((pass == 0) == g.def && interp::TypeSize(*mod, g.type, size)) {
                    globalSizes.emplace(g.key, size);
                }
            }
        }
    }
    // ① 自底向上：函数内解释（代入已算出的被调者的效果，被调者可以在别的模块里）→ 摘要（只有局部事实）
    for (const auto &at : BottomUp(mods)) {
        const fir::Module &m = *mods[at.first];
        const fir::Function &f = m.funcs[at.second];
        std::vector<std::string> trace;
        bool traced = traceFilter && traceFilter(f.name);
        interp::FunctionFacts facts =
            interp::AnalyzeFunction(m, f, traced ? &trace : nullptr, models, &out.summaries, &globalSizes, options);
        out.summaries[f.key] = interp::BuildSummary(m, f, facts);  // 按函数键（SPEC-0027）
        out.facts[f.key] = std::move(facts);
        out.order.push_back(f.key);
        out.moduleOf[f.key] = at.first;
        if (traced) {
            out.traces[f.name] = std::move(trace);
        }
    }
    RecursionLedger(out);  // SPEC-0066
    // ② 推到底
    out.propagate = solver::Propagate(out.summaries);
    std::vector<SiteMap> siteMaps;
    for (const fir::Module *mod : mods) {
        siteMaps.emplace_back(std::vector<const fir::Module *>{mod});
    }
    SiteMap allSites(mods);  // 调用链上的各跳可能在别的模块里
    // 位置的文件名按函数所在模块的文件表换回完整路径（SPEC-0029 §2.1）
    auto sitesOf = [&](const std::string &fn) -> const SiteMap & { return siteMaps[out.moduleOf.at(fn)]; };
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
        // 调用点判出的 L1 / L2。同一调用点同一次访问（同读写、同方向）只报主判定：DEFINITE 优先、对象名写得出来的优先，
        // 其余记 dup_of（成员界与外层界各一条，SPEC-0044）
        size_t firstRep = out.decisions.size();
        std::map<std::string, size_t> primary;  // 分组键 → 主判定在 out.decisions 里的下标
        std::vector<std::string> repKeys;       // 每条判定的分组键（与 out.decisions[firstRep…] 对齐）
        auto better = [](const Decision &x, const Decision &y) {  // x 比 y 更适合当主判定
            if (x.label != y.label) {
                return x.label == Label::Definite;
            }
            return x.obj != "?" && y.obj == "?";
        };
        for (const summary::Rep &rep : result.reps) {
            Decision d = base(rep.level == "L1" ? Label::Definite : Label::Possible, "", fn);
            d.site = sitesOf(fn).Of(rep.site);
            d.kind = rep.kind;
            d.obj = rep.obj;
            d.have = rep.have;
            d.endLo = rep.endLo;
            d.endHi = rep.endHi;
            d.bound = rep.bound;
            d.dir = rep.dir;
            d.depth = static_cast<unsigned>(rep.depth);
            d.effDepth = static_cast<unsigned>(rep.effDepth);
            d.chain = allSites.Chain(rep.chain);
            d.msg = alert::AlertMessage(rep, s.params);
            d.fp = rep.fp;
            std::string key = rep.site + "|" + rep.access.key + "@" + rep.access.at + "|" + rep.kind + "|" + rep.dir;
            repKeys.push_back(key);
            auto hit = primary.find(key);
            if (hit == primary.end()) {
                primary[key] = out.decisions.size();
            } else if (better(d, out.decisions[hit->second])) {
                hit->second = out.decisions.size();
            }
            out.decisions.push_back(std::move(d));
        }
        for (size_t i = firstRep; i < out.decisions.size(); ++i) {
            size_t p = primary.at(repKeys[i - firstRep]);
            if (p != i) {
                out.decisions[i].dupOf = out.decisions[p].fp;
            }
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
            d.site = sitesOf(fn).Of(so.site);
            d.kind = so.req.kind;
            d.obj = mine.obj;
            d.have = mine.have.Show();
            d.endLo = (mine.hasBase ? mine.end2Lo : mine.endLo).Show();
            d.endHi = (mine.hasBase ? mine.end2Hi : mine.endHi).Show();
            d.bound = mine.hasBase ? mine.bound : so.req.bound;
            d.dir = so.req.dir;
            d.depth = static_cast<unsigned>(mine.hasBase ? mine.depth : so.req.depth + 1);
            d.effDepth = static_cast<unsigned>(mine.hasBase ? mine.effDepth : so.req.effDepth);
            std::vector<std::string> path{s.fn + "@" + so.site};  // 函数名（fn 是函数键，SPEC-0027）
            path.insert(path.end(), so.req.path.begin(), so.req.path.end());
            d.chain = allSites.Chain(path);
            // 对象代换不过来（成员的 Req 落到调用方的局部数组上）：用被调者的形参名写成员
            std::string object = d.obj == "?" && out.summaries.count(so.callee) != 0
                                     ? alert::ObjName(so.req.obj, out.summaries.at(so.callee).params)
                                     : alert::ObjName(d.obj, s.params);
            // 调用点上不知道的被调者符号按被调者的形参名写（SPEC-0067）
            std::string shownEnd =
                calleeSummary != out.summaries.end() ? Readable(d.endHi, &calleeSummary->second.params) : d.endHi;
            d.msg = safe ? SafeMessage(object, d.have, d.endHi, d.dir)
                         : SuspectMessage(d.kind, d.depth, object, d.have, shownEnd, d.dir);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, so.req.access.key, d.kind, so.req.access.end,
                                       apply::FingerprintObj(d.obj, d.dir));
            out.decisions.push_back(std::move(d));
        }
        // 入口函数上推不下去的 Req（A-80）
        if (result.entry) {
            // 同一次访问（同读写）只报第一条，其余记 dup_of（SPEC-0036）
            std::map<std::string, std::string> firstFp;
            for (size_t i = 0; i < result.reqs.size(); ++i) {
                const solver::DerivedReq &r = result.reqs[i];
                Decision d =
                    base(Label::Suspect, i < result.entryOpens.size() ? result.entryOpens[i] : "entry_unproven", fn);
                d.site = sitesOf(fn).Of(r.depth == 0 ? r.access.at : s.loc);
                d.kind = r.kind;
                d.obj = r.obj;
                d.have = r.have.Show();
                d.endLo = r.lo.Show();
                d.endHi = r.hi.Show();
                d.bound = r.bound;
                d.dir = r.dir;
                d.depth = static_cast<unsigned>(r.depth);
                d.effDepth = static_cast<unsigned>(r.effDepth);
                d.chain = allSites.Chain(r.path);
                d.msg = SuspectMessage(d.kind, d.depth, alert::ObjName(d.obj, s.params), d.have, d.endHi, d.dir);
                d.fp = DecisionFingerprint(LabelName(d.label), s.key, r.access.key, d.kind, r.access.end,
                                           apply::FingerprintObj(d.obj, d.dir));
                auto first = firstFp.emplace(r.access.key + "@" + r.access.at + "|" + d.kind, d.fp);
                if (!first.second) {
                    d.dupOf = first.first->second;
                }
                out.decisions.push_back(std::move(d));
            }
        }
    }
    std::vector<std::pair<std::string, std::string>> unknownCallees;  // (函数键, 调用点)
    for (const auto &entry : out.summaries) {
        const std::string &fn = entry.first;
        const summary::Summary &s = entry.second;
        const interp::FunctionFacts &facts = out.facts.at(fn);
        // 本函数当场证明安全的访问
        for (const summary::Req &r : facts.safe) {
            Decision d = base(Label::Safe, "local_entailed", fn);
            d.site = sitesOf(fn).Of(r.access.at);
            d.kind = r.kind;
            d.obj = r.obj;
            d.have = r.have;
            d.endLo = r.endLo;
            d.endHi = r.endHi;
            d.bound = r.bound;
            d.dir = r.dir;
            d.chain = allSites.Chain(r.path);
            d.msg = SafeMessage(alert::ObjName(d.obj, s.params), d.have, d.endHi, d.dir);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, r.access.key, d.kind, r.access.end,
                                       apply::FingerprintObj(d.obj, d.dir));
            out.decisions.push_back(std::move(d));
        }
        // 本函数的账本
        for (const summary::Open &o : facts.opens) {
            Decision d = base(Label::Suspect, o.reason, fn);
            d.site = sitesOf(fn).Of(o.at);
            d.chain = {allSites.HopOf(s.fn + "@" + o.at)};
            d.msg = LedgerMessage(o.reason);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, o.at, "", "", o.reason);
            out.decisions.push_back(std::move(d));
        }
        // 没分析完 / 没分析的函数：函数声明处一条（SPEC-0037：skipped 也报）
        llvm::StringRef status = s.status;
        if (status.startswith("partial:") || status.startswith("skipped:")) {
            std::string reason = "unanalyzed:" + status.split(':').second.str();
            Decision d = base(Label::Suspect, reason, fn);
            d.site = sitesOf(fn).Of(s.loc);
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
            // 所有模块都没有它的函数体：linked（solve）时是 unknown_callee，inline 时判不了是不是跨 TU（SPEC-0029 §2.1）
            std::string reason = linked ? "unknown_callee" : "not_in_tu";
            Decision d = base(Label::Suspect, reason, fn);
            d.site = sitesOf(fn).Of(c.site);
            d.chain = {allSites.HopOf(s.fn + "@" + c.site)};
            d.msg = LedgerMessage(reason, c.callee);
            d.fp = DecisionFingerprint(LabelName(d.label), s.key, c.site, "", "", reason + ":" + c.callee);
            out.decisions.push_back(std::move(d));
            if (linked) {
                unknownCallees.push_back({fn, c.site});
            }
        }
    }
    // linked 时"整个程序都没有定义"是确定的事实：也写进调用方摘要的本地账本（同金标准，SPEC-0032）
    for (const auto &u : unknownCallees) {
        std::vector<summary::Open> &opens = out.summaries[u.first].opens;
        bool seen = std::any_of(opens.begin(), opens.end(), [&](const summary::Open &o) {
            return o.reason == "unknown_callee" && o.at == u.second;
        });
        if (!seen) {
            opens.push_back({"unknown_callee", u.second, llvm::None});
        }
    }
    // 告警正文里不露内部链接全局符号的身份后缀（SPEC-0086）
    for (auto &d : out.decisions) {
        d.msg = StripGlobalSuffix(d.msg);
    }
    return out;
}

}  // namespace engine
}  // namespace oobsum
