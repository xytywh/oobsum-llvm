//===-- PropagateTest.cpp - Tests for core/Solver and core/Alert -*- C++ -*-===//
//
// 从局部事实推到底与告警成品（SPEC-0013）：16 个用例推出的 Req / 判定 / 入口账本与 JSON 一致，
// 全部告警与 cases/summaries/alerts.json 逐条相同；统计与 replay.py 相同（上传 70、判定 13、入口账本 12，SPEC-0019 / 0020）。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Alert.h"
#include "OobSummary/core/Apply.h"
#include "OobSummary/core/Solver.h"
#include "OobSummary/core/Summary.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace oobsum;

namespace {

std::string Print(const llvm::json::Value &v)
{
    std::string s;
    llvm::raw_string_ostream os(s);
    os << v;
    return os.str();
}

std::vector<std::string> CaseIds()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator d(OOBSUM_CASES_DIR "/summaries", ec), end; !ec && d != end;
         d.increment(ec)) {
        llvm::SmallString<256> meta(d->path());
        llvm::sys::path::append(meta, "case.json");
        if (llvm::sys::fs::exists(meta)) {
            out.push_back(llvm::sys::path::filename(d->path()).str());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool LoadCase(const std::string &cid, std::map<std::string, summary::Summary> &fns)
{
    llvm::SmallString<256> dir(OOBSUM_CASES_DIR "/summaries");
    llvm::sys::path::append(dir, cid);
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator f(dir, ec), end; !ec && f != end; f.increment(ec)) {
        llvm::StringRef name = llvm::sys::path::filename(f->path());
        if (!name.endswith(".json") || name == "case.json") {
            continue;
        }
        summary::Summary s;
        std::string error;
        if (!summary::ReadSummaryFile(f->path(), s, error)) {
            return false;
        }
        fns[s.fn] = std::move(s);
    }
    return !ec;
}

}  // namespace

TEST(Propagate, DerivedFactsMatchJson)
{
    unsigned up = 0;
    unsigned rep = 0;
    unsigned entry = 0;
    std::vector<std::string> cases = CaseIds();
    EXPECT_EQ(cases.size(), size_t(16));
    for (const std::string &cid : cases) {
        std::map<std::string, summary::Summary> fns;
        ASSERT_TRUE(LoadCase(cid, fns));
        solver::PropagateResult r = solver::Propagate(fns);
        up += r.up;
        rep += r.rep;
        entry += r.entry;
        for (const std::string &fn : r.order) {
            const summary::Summary &s = fns.at(fn);
            const solver::FunctionResult &got = r.fns.at(fn);
            // 推出的 Req 与 JSON 的 reqs（按 req_key 比较）
            std::vector<std::string> mine;
            for (const solver::DerivedReq &d : got.reqs) {
                mine.push_back(solver::ReqKey(d));
            }
            std::vector<std::string> want;
            for (const summary::Req &q : s.reqs) {
                want.push_back(solver::ReqKey(q));
            }
            std::sort(mine.begin(), mine.end());
            std::sort(want.begin(), want.end());
            EXPECT_EQ(cid + " " + fn + " reqs: " + std::to_string(mine.size()),
                      cid + " " + fn + " reqs: " + std::to_string(want.size()));
            for (size_t i = 0; i < std::min(mine.size(), want.size()); ++i) {
                EXPECT_EQ(cid + " " + fn + " " + mine[i], cid + " " + fn + " " + want[i]);
            }
            // 判定记录与 JSON 的 reps[]（按 site + access 对上，逐字段比）
            EXPECT_EQ(got.reps.size(), s.reps.size());
            for (const summary::Rep &g : got.reps) {
                const summary::Rep *w = nullptr;
                for (const summary::Rep &x : s.reps) {
                    if (w == nullptr && x.site == g.site && x.access.fn == g.access.fn &&
                        x.access.key == g.access.key && x.access.at == g.access.at && x.access.end == g.access.end) {
                        w = &x;
                    }
                }
                ASSERT_TRUE(w != nullptr);
                std::vector<std::string> bad = apply::DiffRep(*w, g);
                EXPECT_EQ(
                    cid + " " + fn + " rep diff: " + std::to_string(bad.size()) + (bad.empty() ? "" : " " + bad[0]),
                    cid + " " + fn + " rep diff: 0");
            }
            // 入口账本：每条推出的 Req 的原因要在 JSON 的 opens（带 req 的）里
            for (const std::string &reason : got.entryOpens) {
                bool found = false;
                for (const summary::Open &o : s.opens) {
                    found = found || (o.req && o.reason == reason);
                }
                EXPECT_EQ(cid + " " + fn + " entry " + reason + (found ? " found" : " missing"),
                          cid + " " + fn + " entry " + reason + " found");
            }
        }
    }
    // replay.py：推出上传 Req 72 条、判定 13 条、入口账本 14 条（SPEC-0019 补齐金标准之前是 58 / 13 / 8，SPEC-0020 的第二段 +4，
    // SPEC-0035 成员数组实参的外层对象 +2 / +2）
    EXPECT_EQ(up, 72u);
    EXPECT_EQ(rep, 13u);
    EXPECT_EQ(entry, 14u);
}

TEST(Propagate, AlertsMatchAlertsJson)
{
    auto buffer = llvm::MemoryBuffer::getFile(OOBSUM_CASES_DIR "/summaries/alerts.json");
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::Expected<llvm::json::Value> golden = llvm::json::parse((*buffer)->getBuffer());
    ASSERT_TRUE(static_cast<bool>(golden));
    const llvm::json::Array *want = golden->getAsArray();
    ASSERT_TRUE(want != nullptr);
    std::vector<std::string> wanted;
    for (const llvm::json::Value &a : *want) {
        wanted.push_back(Print(a));
    }
    std::vector<std::string> got;
    for (const std::string &cid : CaseIds()) {
        std::map<std::string, summary::Summary> fns;
        ASSERT_TRUE(LoadCase(cid, fns));
        solver::PropagateResult r = solver::Propagate(fns);
        for (const std::string &fn : r.order) {
            for (const summary::Rep &rep : r.fns.at(fn).reps) {
                if (!rep.reported) {
                    continue;
                }
                llvm::json::Value a = alert::AlertToJson(alert::MakeAlert(fn, fns.at(fn), rep));
                a.getAsObject()->insert({"case", cid});
                got.push_back(Print(a));
            }
        }
    }
    // alerts.json 的顺序是 replay.py 的用例顺序；这里按内容排序后逐条比
    std::sort(got.begin(), got.end());
    std::sort(wanted.begin(), wanted.end());
    EXPECT_EQ(got.size(), size_t(13));
    EXPECT_EQ(got.size(), wanted.size());
    for (size_t i = 0; i < std::min(got.size(), wanted.size()); ++i) {
        EXPECT_EQ(got[i], wanted[i]);
    }
}

TEST(Propagate, AbsorbKeepsStrongest)
{
    // A-44：同一 have / 对象 / 守卫，end 上界较小的那条并掉；相等时留前面的
    solver::DerivedReq a;
    a.have = LinExpr::Ext("p0");
    a.obj = "*p0";
    a.hi = LinExpr::Sym("p1") + LinExpr::Const(1);
    solver::DerivedReq b = a;
    b.hi = LinExpr::Sym("p1") + LinExpr::Const(2);
    solver::DerivedReq c = b;
    std::vector<solver::DerivedReq> kept = solver::Absorb({a, b, c});
    ASSERT_TRUE(kept.size() == 1);
    EXPECT_EQ(kept[0].hi.Show(), "p1 + 2");
    solver::DerivedReq other = a;
    other.obj = "g:tbl";
    EXPECT_EQ(solver::Absorb({a, other}).size(), size_t(2));
    // 小结：没有常数差（p1 与 p2）就都留着
    solver::DerivedReq d = a;
    d.hi = LinExpr::Sym("p2");
    EXPECT_EQ(solver::Absorb({a, d}).size(), size_t(2));
}

TEST(Alert, ObjectNamesAndPhrases)
{
    std::vector<summary::Param> params = {{"pstCtx", "CTX_S*", llvm::None}, {"ulLen", "UINT32", llvm::None}};
    EXPECT_EQ(alert::ObjName("g:g_aulTbl", params), "'g_aulTbl'");
    EXPECT_EQ(alert::ObjName("local:aucPkt", params), "'aucPkt'");
    EXPECT_EQ(alert::ObjName("heap@a.c:68", params), "heap object allocated at a.c:68");
    EXPECT_EQ(alert::ObjName("*p0->pucBuf", params), "'*pstCtx->pucBuf'");
    EXPECT_EQ(alert::ObjName("*p7", params), "'*p7'");
    summary::Rep rep;
    rep.level = "L2";
    rep.kind = "R";
    rep.obj = "g:g_x";
    rep.have = "16";
    rep.endHi = "20";
    rep.bound = "mask";
    rep.depth = 3;
    rep.chain = {"Top@a.c:9", "Mid@b.c:4", "Leaf@b.c:2"};
    EXPECT_EQ(
        alert::AlertMessage(rep, params),
        "Possible out-of-bound read through a 3-level call chain: up to byte 20 of 'g_x' (16 bytes) may be accessed; "
        "the bound comes from a bit mask [Top (a.c:9) -> Mid (b.c:4) -> Leaf (b.c:2)]");
}

TEST(Propagate, CalleeKeyDisambiguatesStatics)
{
    // 两个文件里各有一个 static Init（同名、键不同）：调用点按 callee_key 接到各自的函数上（SPEC-0027）
    auto parse = [](const std::string &text) {
        summary::Summary s;
        std::string error;
        llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
        EXPECT_TRUE(static_cast<bool>(v));
        EXPECT_TRUE(summary::ReadSummary(*v, s, error));
        return s;
    };
    auto init = [&](const std::string &key, const std::string &end) {
        return parse(
            R"j({"schema":"oobsum-summary/1","fn":"Init","key":")j" + key +
            R"j(","loc":"a.c:1","params":[{"name":"p","type":"UINT8*"}],"status":"analyzed","reqs":[{"id":"R1","kind":"W",)j"
            R"j("have":"ext(p0)","obj":"*p0","end":[")j" +
            end + R"j(",")j" + end + R"j("],"bound":"exact","guard":[],"access":{"fn":"Init","key":")j" + key +
            R"j(","at":"a.c:2","end":")j" + end +
            R"j("},"path":["Init@a.c:2"],"depth":0,"eff_depth":0,"blind":false}],"effs":[],"calls":[],"reps":[],"opens":[]})j");
    };
    auto top = [&](const std::string &name, const std::string &calleeKey) {
        return parse(
            R"j({"schema":"oobsum-summary/1","fn":")j" + name + R"j(","key":"F:)j" + name +
            R"j(","loc":"t.c:1","params":[],"status":"analyzed","reqs":[],"effs":[],"calls":[{"site":"t.c:3","callee":"Init",)j"
            R"j("callee_key":")j" +
            calleeKey + R"j(","args":[{"obj":"local:aucBuf","have":"8"}],"results":[]}],"reps":[],"opens":[]})j");
    };
    std::map<std::string, summary::Summary> fns;
    fns["F@aaaa:Init"] = init("F@aaaa:Init", "4");    // 写到第 4 字节：8 字节的缓冲区装得下
    fns["F@bbbb:Init"] = init("F@bbbb:Init", "100");  // 写到第 100 字节：越界
    fns["F:Top_A"] = top("Top_A", "F@aaaa:Init");
    fns["F:Top_B"] = top("Top_B", "F@bbbb:Init");
    solver::PropagateResult r = solver::Propagate(fns);
    EXPECT_TRUE(r.fns.at("F:Top_A").reps.empty());
    ASSERT_TRUE(r.fns.at("F:Top_B").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:Top_B").reps[0].level, "L1");
    EXPECT_EQ(r.fns.at("F:Top_B").reps[0].chain[0], "Top_B@t.c:3");  // 调用链写函数名
}

TEST(Propagate, EntryConstantLevel)
{
    // SPEC-0087：入口函数自己的深度 0 常数 Req：末端 101 > 4 → L1；[1, 10] 来自循环 → L2；含入口形参的照旧记账
    auto entry = [](const std::string &name, const std::string &have, const std::string &lo, const std::string &hi,
                    const std::string &bound) {
        summary::Summary s;
        std::string error;
        std::string text =
            R"j({"schema":"oobsum-summary/1","fn":")j" + name + R"j(","key":"F:)j" + name +
            R"j(","loc":"t.c:1","params":[{"name":"n","type":"UINT32"}],"status":"analyzed","reqs":[{"id":"R1","kind":"W",)j"
            R"j("have":")j" +
            have + R"j(","obj":"local:a","end":[")j" + lo + R"j(",")j" + hi + R"j("],"bound":")j" + bound +
            R"j(","guard":[],"access":{"fn":")j" + name + R"j(","key":"F:)j" + name + R"j(","at":"t.c:2","end":")j" +
            hi + R"j("},"path":[")j" + name +
            R"j(@t.c:2"],"depth":0,"eff_depth":0,"blind":false}],"effs":[],"calls":[],"reps":[],"opens":[]})j";
        llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
        EXPECT_TRUE(static_cast<bool>(v));
        EXPECT_TRUE(summary::ReadSummary(*v, s, error));
        return s;
    };
    std::map<std::string, summary::Summary> fns;
    fns["F:T9"] = entry("T9", "4", "101", "101", "exact");
    fns["F:T12"] = entry("T12", "4", "1", "10", "loop");
    fns["F:T13"] = entry("T13", "4", "p0 + 1", "p0 + 1", "exact");
    fns["F:T14"] = entry("T14", "4", "1", "256", "type");
    solver::PropagateResult r = solver::Propagate(fns);
    ASSERT_TRUE(r.fns.at("F:T9").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:T9").reps[0].level, "L1");
    EXPECT_EQ(r.fns.at("F:T9").reps[0].site, "t.c:2");
    EXPECT_TRUE(r.fns.at("F:T9").reqs.empty());
    ASSERT_TRUE(r.fns.at("F:T12").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:T12").reps[0].level, "L2");
    EXPECT_TRUE(r.fns.at("F:T13").reps.empty());
    EXPECT_EQ(r.fns.at("F:T13").entryOpens.size(), size_t(1));
    EXPECT_TRUE(r.fns.at("F:T14").reps.empty());  // 只有类型范围：不是代码给出的界
    EXPECT_EQ(r.fns.at("F:T14").entryOpens.size(), size_t(1));
}

TEST(Propagate, ArgOffsetRange)
{
    // SPEC-0095：实参偏移 [2, 6]（have 是最小剩余 10）；被调者写到 p0 + 13 → 按对象起点：末端 [15, 19] 对 16 → L2（原来拿 10 判 L1）
    auto parse = [](const std::string &text) {
        summary::Summary s;
        std::string error;
        llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
        EXPECT_TRUE(static_cast<bool>(v));
        EXPECT_TRUE(summary::ReadSummary(*v, s, error));
        return s;
    };
    summary::Summary put = parse(
        R"j({"schema":"oobsum-summary/1","fn":"Put","key":"F@aaaa:Put","loc":"a.c:1","params":[{"name":"p","type":"UINT8*"}],)j"
        R"j("status":"analyzed","reqs":[{"id":"R1","kind":"W","have":"ext(p0)","obj":"*p0","end":["13","13"],"bound":"exact",)j"
        R"j("guard":[],"access":{"fn":"Put","key":"F@aaaa:Put","at":"a.c:2","end":"13"},"path":["Put@a.c:2"],"depth":0,)j"
        R"j("eff_depth":0,"blind":false}],"effs":[],"calls":[],"reps":[],"opens":[]})j");
    auto top = [&](const std::string &name, const std::string &arg) {
        return parse(
            R"j({"schema":"oobsum-summary/1","fn":")j" + name + R"j(","key":"F:)j" + name +
            R"j(","loc":"t.c:1","params":[],"status":"analyzed","reqs":[],"effs":[],"calls":[{"site":"t.c:3",)j"
            R"j("callee":"Put","callee_key":"F@aaaa:Put","args":[)j" +
            arg + R"j(],"results":[]}],"reps":[],"opens":[]})j");
    };
    std::map<std::string, summary::Summary> fns;
    fns["F@aaaa:Put"] = put;
    fns["F:T_Range"] =
        top("T_Range", R"j({"obj":"local:a","have":"10","off_lo":"2","off_hi":"6","off_bound":"branch"})j");
    fns["F:T_Type"] =
        top("T_Type", R"j({"obj":"local:a","have":"-239","off_lo":"0","off_hi":"255","off_bound":"type"})j");
    fns["F:T_Old"] = top("T_Old", R"j({"obj":"local:a","have":"10"})j");  // 老格式：照旧按 have 判
    // 读写往返带 off_*
    llvm::json::Value back = summary::SummaryToJson(fns["F:T_Range"]);
    summary::Summary again;
    std::string error;
    ASSERT_TRUE(summary::ReadSummary(back, again, error));
    ASSERT_TRUE(again.calls[0].args[0].ptr->offLo.hasValue());
    EXPECT_EQ(*again.calls[0].args[0].ptr->offHi, "6");
    EXPECT_EQ(*again.calls[0].args[0].ptr->offBound, "branch");
    solver::PropagateResult r = solver::Propagate(fns);
    ASSERT_TRUE(r.fns.at("F:T_Range").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:T_Range").reps[0].level, "L2");
    EXPECT_EQ(r.fns.at("F:T_Range").reps[0].have, "16");
    EXPECT_EQ(r.fns.at("F:T_Range").reps[0].endHi, "19");
    EXPECT_TRUE(r.fns.at("F:T_Type").reps.empty());  // 偏移上界只来自类型范围 → 记账，不是 L1
    ASSERT_TRUE(r.fns.at("F:T_Old").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:T_Old").reps[0].level, "L1");
}

TEST(Propagate, NonNullMap)
{
    // SPEC-0099：被调者 Req 带守卫 nn:p1 >= 1；实参 0 → 走不到；局部数组 → L1；调用方自己的形参 → 换成调用方的 nn:p1 往上传
    auto parse = [](const std::string &text) {
        summary::Summary s;
        std::string error;
        llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
        EXPECT_TRUE(static_cast<bool>(v));
        EXPECT_TRUE(summary::ReadSummary(*v, s, error));
        return s;
    };
    summary::Summary opt = parse(
        R"j({"schema":"oobsum-summary/1","fn":"Opt","key":"F@aaaa:Opt","loc":"a.c:1","params":[{"name":"p","type":"UINT8*"},)j"
        R"j({"name":"q","type":"UINT8*"}],"status":"analyzed","reqs":[{"id":"R1","kind":"W","have":"ext(p0)","obj":"*p0",)j"
        R"j("end":["11","11"],"bound":"exact","guard":["nn:p1 >= 1"],"access":{"fn":"Opt","key":"F@aaaa:Opt","at":"a.c:2",)j"
        R"j("end":"11"},"path":["Opt@a.c:2"],"depth":0,"eff_depth":0,"blind":false}],"effs":[],"calls":[],"reps":[],"opens":[]})j");
    auto top = [&](const std::string &name, const std::string &params, const std::string &q) {
        return parse(
            R"j({"schema":"oobsum-summary/1","fn":")j" + name + R"j(","key":"F:)j" + name +
            R"j(","loc":"t.c:1",)j"
            R"j("params":[)j" +
            params +
            R"j(],"status":"analyzed","reqs":[],"effs":[],"calls":[{"site":"t.c:3","callee":"Opt","callee_key":"F@aaaa:Opt",)j"
            R"j("args":[{"obj":"local:a","have":"4"},)j" +
            q + R"j(],"results":[]}],"reps":[],"opens":[]})j");
    };
    std::map<std::string, summary::Summary> fns;
    fns["F@aaaa:Opt"] = opt;
    fns["F:T_Null"] = top("T_Null", "", R"j("0")j");
    fns["F:T_Buf"] = top("T_Buf", "", R"j({"obj":"local:b","have":"1"})j");
    // T_Mid 把自己的两个形参传下去：Req 往上传，守卫换成它自己的 nn:p1
    fns["F:T_Mid"] = parse(
        R"j({"schema":"oobsum-summary/1","fn":"T_Mid","key":"F:T_Mid","loc":"t.c:1","params":[{"name":"a","type":"UINT8*"},)j"
        R"j({"name":"q","type":"UINT8*"}],"status":"analyzed","reqs":[],"effs":[],"calls":[{"site":"t.c:3","callee":"Opt",)j"
        R"j("callee_key":"F@aaaa:Opt","args":[{"obj":"*p0","have":"ext(p0)","alias":"p0"},)j"
        R"j({"obj":"*p1","have":"ext(p1)","alias":"p1"}],"results":[]}],"reps":[],"opens":[]})j");
    solver::PropagateResult r = solver::Propagate(fns);
    EXPECT_TRUE(r.fns.at("F:T_Null").reps.empty());
    bool safe = false;
    for (const solver::SiteOutcome &o : r.fns.at("F:T_Null").sites) {
        safe = safe || (o.mine.outcome == "safe" && o.mine.reason == "guard_false");
    }
    EXPECT_TRUE(safe);
    ASSERT_TRUE(r.fns.at("F:T_Buf").reps.size() == 1);
    EXPECT_EQ(r.fns.at("F:T_Buf").reps[0].level, "L1");
    ASSERT_TRUE(r.fns.at("F:T_Mid").reqs.size() == 1);
    ASSERT_TRUE(r.fns.at("F:T_Mid").reqs[0].guards.size() == 1);
    EXPECT_EQ(ShowLe0(r.fns.at("F:T_Mid").reqs[0].guards[0]), "nn:p1 >= 1");
}
