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
}
