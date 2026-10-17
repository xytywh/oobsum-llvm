//===-- ApplyTest.cpp - Tests for core/Apply --------------------*- C++ -*-===//
//
// 调用点判定（SPEC-0012 §2.2–§2.4）：cases/summaries 的 16 个用例逐调用点核对 0 处不一致，
// 输出（Dump=apply 的格式）与 replay.py 在机器版摘要上的输出逐行相同（金标准 data/apply-golden.txt，gen_apply_golden.py 生成）。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Apply.h"
#include "OobSummary/core/Summary.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <map>
#include <string>
#include <vector>

using namespace oobsum;

namespace {

std::map<std::string, std::vector<std::string>> Golden()
{
    std::map<std::string, std::vector<std::string>> out;
    auto buffer = llvm::MemoryBuffer::getFile(OOBSUM_TEST_DATA_DIR "/apply-golden.txt");
    if (!buffer) {
        return out;
    }
    llvm::SmallVector<llvm::StringRef, 256> lines;
    (*buffer)->getBuffer().split(lines, '\n', -1, false);
    std::string cid;
    for (llvm::StringRef line : lines) {
        if (line.startswith("# ")) {
            continue;
        }
        if (line.startswith("## ")) {
            cid = line.drop_front(3).str();
            out[cid];
            continue;
        }
        out[cid].push_back(line.str());
    }
    return out;
}

bool LoadCase(const std::string &cid, std::map<std::string, summary::Summary> &fns, std::string &error)
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
        if (!summary::ReadSummaryFile(f->path(), s, error)) {
            return false;
        }
        fns[s.fn] = std::move(s);
    }
    return !ec;
}

}  // namespace

TEST(Apply, AllCasesMatchReplay)
{
    std::map<std::string, std::vector<std::string>> golden = Golden();
    EXPECT_EQ(golden.size(), size_t(16));
    unsigned lines = 0;
    for (const auto &entry : golden) {
        std::map<std::string, summary::Summary> fns;
        std::string error;
        bool loaded = LoadCase(entry.first, fns, error);
        EXPECT_EQ(entry.first + ": " + (loaded ? "<ok>" : error), entry.first + ": <ok>");
        if (!loaded) {
            continue;
        }
        apply::CheckResult r = apply::CheckCase(entry.first, fns);
        for (const std::string &e : r.errors) {
            EXPECT_EQ(e, std::string("<no error>"));
        }
        size_t n = std::max(r.lines.size(), entry.second.size());
        for (size_t i = 0; i < n; ++i) {
            std::string got = i < r.lines.size() ? r.lines[i] : "<missing>";
            std::string want = i < entry.second.size() ? entry.second[i] : "<missing>";
            EXPECT_EQ(entry.first + " | " + got, entry.first + " | " + want);
        }
        lines += r.lines.size();
    }
    EXPECT_TRUE(lines >= 240);
}

TEST(Apply, SubstitutionAndDecisionBasics)
{
    // 一个最小的调用点：被调者写 4*p0 + 4 字节到 g:tbl（16 字节）；实参 4 → 最小末端 20 > 16 → L1，depth 1
    summary::Req req;
    req.id = "R1";
    req.kind = "W";
    req.have = "16";
    req.obj = "g:tbl";
    req.endLo = "4*p0 + 4";
    req.endHi = "4*p0 + 4";
    req.bound = "exact";
    req.access = {"Leaf", "F:Leaf", "a.c:3", "4*p0 + 4"};
    req.path = {"Leaf@a.c:3"};
    summary::Call call;
    call.site = "a.c:9";
    call.callee = "Leaf";
    summary::ArgValue four;
    four.kind = summary::ArgValue::Kind::Expr;
    four.expr = "4";
    call.args.push_back(four);
    summary::Summary caller;
    caller.fn = "Top";
    caller.key = "F:Top";
    apply::Outcome o = apply::Classify(req, call, caller, {});
    EXPECT_EQ(o.outcome, "L1");
    EXPECT_EQ(o.end2Hi.Show(), "20");
    EXPECT_EQ(o.depth, 1);
    summary::Rep rep = apply::RepOf(o, call, "Top", caller, req);
    EXPECT_FALSE(rep.reported);  // depth 1 ≤ InlineDepth 2：交现有规则
    EXPECT_EQ(rep.chain.size(), size_t(2));
    // 实参 2 → 末端 12 ≤ 16 → safe
    call.args[0].expr = "2";
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "safe");
    // 实参是调用方局部 i ∈ [0, 3]（循环界）→ 末端 [4, 16] ≤ 16 → safe；i ∈ [0, 4] → L2（上界来自 loop）
    call.args[0].expr = "i";
    call.locals = std::map<std::string, summary::Local>{{"i", {"0", "3", "loop"}}};
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "safe");
    (*call.locals)["i"] = {"0", "4", "loop"};
    apply::Outcome l2 = apply::Classify(req, call, caller, {});
    EXPECT_EQ(l2.outcome, "L2");
    EXPECT_EQ(l2.bound, "loop");
    // 实参是调用方形参 → 上传
    call.locals.reset();
    call.args[0].expr = "p1";
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "up");
}

TEST(Apply, OutNewTargets)
{
    // out_new 的 target：pK（SPEC-0053）与 pK->字段（SPEC-0061）；大小按实参代换
    summary::Call call;
    summary::ArgValue ptr;
    ptr.kind = summary::ArgValue::Kind::Ptr;
    ptr.ptr = std::make_shared<summary::PtrArg>();
    ptr.ptr->obj = "local:st";
    ptr.ptr->have = "12";
    summary::ArgValue len;
    len.kind = summary::ArgValue::Kind::Expr;
    len.expr = "64";
    call.args = {ptr, len};
    summary::Eff eff;
    eff.kind = "out_new";
    eff.size = "p1";
    eff.maybeNull = true;
    eff.target = "p0";
    llvm::Optional<summary::Apply> whole = apply::ApplyEff(eff, call);
    ASSERT_TRUE(whole.hasValue());
    EXPECT_EQ(whole->arg.getValueOr(-1), int64_t(0));
    EXPECT_FALSE(whole->field.hasValue());
    EXPECT_EQ(whole->size.getValueOr(""), "64");
    eff.target = "p0->pucRes";
    llvm::Optional<summary::Apply> field = apply::ApplyEff(eff, call);
    ASSERT_TRUE(field.hasValue());
    EXPECT_EQ(field->field.getValueOr(""), "pucRes");
    eff.target = "p0->a.b";  // 两层字段以后（SPEC-0061 §9）
    EXPECT_FALSE(apply::ApplyEff(eff, call).hasValue());
}

TEST(Apply, SubObjectReqIsSuspectOnly)
{
    // 内层 Req（SPEC-0104 §2.3 3a）：一行 8 字节，末端 p0 + 1。实参 8 → 末端 9 > 8：原规则是 L1，内层只记 SUSPECT（open subobject_overflow）；
    // 实参 i ∈ [0, 8]（循环界）→ 原规则 L2，同样改判；实参 7 → safe；实参是调用方形参 → 上传
    summary::Req req;
    req.id = "R1";
    req.kind = "W";
    req.dir = "sub";
    req.have = "8";
    req.obj = "g:tbl";
    req.endLo = "p0 + 1";
    req.endHi = "p0 + 1";
    req.bound = "exact";
    req.access = {"Leaf", "F:Leaf", "a.c:3", "p0 + 1"};
    req.path = {"Leaf@a.c:3"};
    summary::Call call;
    call.site = "a.c:9";
    call.callee = "Leaf";
    summary::ArgValue arg;
    arg.kind = summary::ArgValue::Kind::Expr;
    arg.expr = "8";
    call.args.push_back(arg);
    summary::Summary caller;
    caller.fn = "Top";
    caller.key = "F:Top";
    apply::Outcome o = apply::Classify(req, call, caller, {});
    EXPECT_EQ(o.outcome + ":" + o.reason, "open:subobject_overflow");
    EXPECT_EQ(o.end2Hi.Show(), "9");
    call.args[0].expr = "i";
    call.locals = std::map<std::string, summary::Local>{{"i", {"0", "8", "loop"}}};
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "open");
    call.locals.reset();
    call.args[0].expr = "7";
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "safe");
    call.args[0].expr = "p1";
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "up");
    // 正向 Req 照旧判 L1
    req.dir.clear();
    call.args[0].expr = "8";
    EXPECT_EQ(apply::Classify(req, call, caller, {}).outcome, "L1");
    // Subobject=label（SPEC-0105）：内层 Req 的 L1 / L2 照常
    req.dir = "sub";
    EXPECT_EQ(apply::Classify(req, call, caller, {}, true).outcome, "L1");
    call.args[0].expr = "i";
    call.locals = std::map<std::string, summary::Local>{{"i", {"0", "8", "loop"}}};
    EXPECT_EQ(apply::Classify(req, call, caller, {}, true).outcome, "L2");
}

TEST(Apply, SubObjectArg)
{
    // 带窗口的实参（SPEC-0104 §2.3 3b）：被调者写 p1 字节到 *p0（have ext(p0)）；实参 st.aucBuf：外层剩 28、内层剩 16。
    // n = 20 → 外层 safe、内层越出 → 加一条 open subobject_overflow（dir sub）；n = 16 → 只有外层 safe；n = 40 → 外层 L1，不加内层
    summary::Req req;
    req.id = "R1";
    req.kind = "W";
    req.have = "ext(p0)";
    req.obj = "*p0";
    req.endLo = "p1";
    req.endHi = "p1";
    req.bound = "exact";
    req.access = {"Fill", "F:Fill", "a.c:5", "p1"};
    req.path = {"Fill@a.c:5"};
    summary::Call call;
    call.site = "a.c:7";
    call.callee = "Fill";
    summary::ArgValue ptr;
    ptr.kind = summary::ArgValue::Kind::Ptr;
    ptr.ptr = std::make_shared<summary::PtrArg>();
    ptr.ptr->obj = "local:st";
    ptr.ptr->have = "28";
    ptr.ptr->sub = std::string("16");
    summary::ArgValue n;
    n.kind = summary::ArgValue::Kind::Expr;
    n.expr = "20";
    call.args = {ptr, n};
    summary::Summary caller;
    caller.fn = "Top";
    caller.key = "F:Top";
    std::vector<apply::SiteClass> all = apply::ClassifyAll(req, call, caller, {});
    ASSERT_TRUE(all.size() == 2);
    EXPECT_EQ(all[0].mine.outcome, "safe");
    EXPECT_EQ(all[1].mine.outcome + ":" + all[1].mine.reason + ":" + all[1].mine.dir, "open:subobject_overflow:sub");
    EXPECT_EQ(all[1].mine.have.Show(), "16");
    call.args[1].expr = "16";
    EXPECT_TRUE(apply::ClassifyAll(req, call, caller, {}).size() == 1);
    call.args[1].expr = "40";
    all = apply::ClassifyAll(req, call, caller, {});
    ASSERT_TRUE(all.size() == 1);
    EXPECT_EQ(all[0].mine.outcome, "L1");
    // 被调者的实参是调用方形参 → 外层、内层都上传
    call.args[1].expr = "p1";
    all = apply::ClassifyAll(req, call, caller, {});
    ASSERT_TRUE(all.size() == 2);
    EXPECT_EQ(all[1].mine.outcome + ":" + all[1].mine.dir, "up:sub");
}

TEST(Apply, CheckAssume)
{
    // CharAlias=verify 的不别名假设在调用点核对（SPEC-0106 §2.3）
    auto ptr = [](const std::string &obj) {
        summary::ArgValue a;
        a.kind = summary::ArgValue::Kind::Ptr;
        a.ptr = std::make_shared<summary::PtrArg>();
        a.ptr->obj = obj;
        a.ptr->have = "4";
        return a;
    };
    auto check = [&](const std::string &a0, const std::string &a1, const std::pair<std::string, std::string> &as) {
        summary::Call call;
        call.site = "a.c:9";
        call.callee = "F";
        call.args = {ptr(a0), ptr(a1)};
        apply::AssumeCheck c = apply::CheckAssume(as, call);
        return c.verdict + (c.detail.empty() ? "" : ":" + c.detail);
    };
    std::pair<std::string, std::string> pp{"p0", "p1"};
    EXPECT_EQ(check("local:b", "local:st", pp), "ok");              // 两个不同的局部
    EXPECT_EQ(check("local:st", "local:st", pp), "same:local:st");  // 同一个
    EXPECT_EQ(check("local:b", "*p0", pp), "ok");                   // 调用方的局部对调用方的形参所指
    EXPECT_EQ(check("g:g_x", "*p0", pp), "up:g:g_x&p0");            // 全局对形参所指：往上传
    EXPECT_EQ(check("*p1", "*p0", pp), "up:p0&p1");                 // 两个形参所指
    EXPECT_EQ(check("*p0", "p0->aucBuf", pp), "same:p0");           // 形参所指与它的成员数组
    EXPECT_EQ(check("local:_unknown0", "local:st", pp), "unknown:local:_unknown0");
    EXPECT_EQ(check("local:b", "heap@a.c:3", pp), "ok");
    // 全局对实参：去掉内部链接的后缀再比
    EXPECT_EQ(check("g:g_x__S0123456789abcdef", "local:b", {"g:g_x", "p0"}), "same:g:g_x");
    EXPECT_EQ(check("local:b", "local:st", {"g:g_x", "p1"}), "ok");
    // 指针字段所指：实参是调用方形参所指 → 调用方的 p<j>->f；记着的字段值 → 它的对象；都没有 → 不知道
    EXPECT_EQ(check("local:b", "*p2", {"p0", "p1->pucBuf"}), "ok");
    EXPECT_EQ(check("g:g_x", "*p2", {"p0", "p1->pucBuf"}), "up:g:g_x&p2->pucBuf");
    EXPECT_EQ(check("local:b", "local:st", {"p0", "p1->pucBuf"}), "unknown:p1->pucBuf");
}

TEST(Apply, SubObjectFieldArg)
{
    // SPEC-0107 §2.1：被调者经 p0->pucBuf 写 p1 字节（have ext(p0->pucBuf)）；实参结构体里记着的字段值指向 st.aucBuf：外层剩 28、内层剩 16
    summary::Req req;
    req.id = "R1";
    req.kind = "W";
    req.have = "ext(p0->pucBuf)";
    req.obj = "*p0->pucBuf";
    req.endLo = "p1";
    req.endHi = "p1";
    req.bound = "exact";
    req.access = {"Proc", "F:Proc", "f.c:6", "p1"};
    req.path = {"Proc@f.c:6"};
    summary::ArgValue field;
    field.kind = summary::ArgValue::Kind::Ptr;
    field.ptr = std::make_shared<summary::PtrArg>();
    field.ptr->obj = "local:st";
    field.ptr->have = "28";
    field.ptr->sub = std::string("16");
    summary::ArgValue view;
    view.kind = summary::ArgValue::Kind::Ptr;
    view.ptr = std::make_shared<summary::PtrArg>();
    view.ptr->obj = "local:v";
    view.ptr->have = "8";
    view.ptr->fields = std::map<std::string, summary::ArgValue>{{"pucBuf", field}};
    summary::ArgValue n;
    n.kind = summary::ArgValue::Kind::Expr;
    n.expr = "20";
    summary::Call call;
    call.site = "f.c:7";
    call.callee = "Proc";
    call.args = {view, n};
    summary::Summary caller;
    caller.fn = "Top";
    caller.key = "F:Top";
    std::vector<apply::SiteClass> all = apply::ClassifyAll(req, call, caller, {});
    ASSERT_TRUE(all.size() == 2);
    EXPECT_EQ(all[0].mine.outcome, "safe");
    EXPECT_EQ(all[1].mine.outcome + ":" + all[1].mine.reason + ":" + all[1].mine.dir, "open:subobject_overflow:sub");
    EXPECT_EQ(view.ptr->fields->at("pucBuf").ptr->have, "28");  // 原来的实参不动
    call.args[1].expr = "16";
    EXPECT_TRUE(apply::ClassifyAll(req, call, caller, {}).size() == 1);
}

TEST(Apply, SubObjectBackArg)
{
    // SPEC-0107 §2.2 第 3 步：被调者写 p[-1]（反向 Req：越到指针前面 1 字节 ≤ bext(p0)）；实参 st.aucBuf：外层前面 4 字节、子对象前面 0 字节
    summary::Req req;
    req.id = "R1";
    req.kind = "W";
    req.dir = "back";
    req.have = "bext(p0)";
    req.obj = "*p0";
    req.endLo = "1";
    req.endHi = "1";
    req.bound = "exact";
    req.access = {"Prev", "F:Prev", "b.c:5", "1"};
    req.path = {"Prev@b.c:5"};
    summary::ArgValue ptr;
    ptr.kind = summary::ArgValue::Kind::Ptr;
    ptr.ptr = std::make_shared<summary::PtrArg>();
    ptr.ptr->obj = "local:st";
    ptr.ptr->have = "28";
    ptr.ptr->back = std::string("4");
    ptr.ptr->sub = std::string("16");
    ptr.ptr->subBack = std::string("0");
    summary::Call call;
    call.site = "b.c:6";
    call.callee = "Prev";
    call.args = {ptr};
    summary::Summary caller;
    caller.fn = "Top";
    caller.key = "F:Top";
    std::vector<apply::SiteClass> all = apply::ClassifyAll(req, call, caller, {});
    ASSERT_TRUE(all.size() == 2);
    EXPECT_EQ(all[0].mine.outcome, "safe");
    EXPECT_EQ(all[1].mine.outcome + ":" + all[1].mine.reason + ":" + all[1].mine.dir,
              "open:subobject_overflow:subback");
    // 指针在子对象里第 1 个字节之后：前面还有 1 字节 → 内层也安全
    call.args[0].ptr->subBack = std::string("1");
    EXPECT_TRUE(apply::ClassifyAll(req, call, caller, {}).size() == 1);
}
