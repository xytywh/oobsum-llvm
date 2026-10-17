//===-- SummaryTest.cpp - Tests for core/Summary ----------------*- C++ -*-===//
//
// 摘要的读写（SPEC-0012 §2.1）：cases/summaries 的 104 份摘要读入再写出，与去掉 doc 的原文完全相同；坏输入逐条报错。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Summary.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace oobsum::summary;

namespace {

std::vector<std::string> SummaryFiles()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator dir(OOBSUM_CASES_DIR "/summaries", ec), end; !ec && dir != end;
         dir.increment(ec)) {
        if (!llvm::sys::fs::is_directory(dir->path())) {
            continue;
        }
        std::error_code ec2;
        for (llvm::sys::fs::directory_iterator f(dir->path(), ec2), fend; !ec2 && f != fend; f.increment(ec2)) {
            llvm::StringRef name = llvm::sys::path::filename(f->path());
            if (name.endswith(".json") && name != "case.json") {
                out.push_back(f->path());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string Print(const llvm::json::Value &v)
{
    std::string s;
    llvm::raw_string_ostream os(s);
    os << v;
    return os.str();
}

// 一份最小的摘要，用来改出各种坏输入
const char *const kMinimal = R"json({"schema": "oobsum-summary/1", "fn": "f", "key": "F:f", "loc": "a.c:1",
 "params": [{"name": "p", "type": "UINT8*"}], "status": "analyzed",
 "reqs": [{"id": "R1", "kind": "W", "have": "ext(p0)", "obj": "*p0", "end": ["1", "p1 + 1"], "bound": "loop",
           "guard": ["p1 >= 0"], "access": {"fn": "f", "key": "F:f", "at": "a.c:2", "end": "p1 + 1"},
           "path": ["f@a.c:2"], "depth": 0, "eff_depth": 0, "blind": false, "doc": {"text": "任意说明"}}],
 "effs": [], "calls": [{"site": "a.c:3", "callee": "g", "args": [{"obj": "*p0", "have": "ext(p0)"}, "4", null],
           "results": [{"req": "R1", "outcome": "up", "as": "R1"}]}], "reps": [], "opens": []})json";

std::string ErrorOf(const std::string &text)
{
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
    if (!v) {
        return "parse: " + llvm::toString(v.takeError());
    }
    Summary s;
    std::string error;
    return ReadSummary(*v, s, error) ? "<ok>" : error;
}

std::string Mutate(const std::string &from, const std::string &to)
{
    std::string text = kMinimal;
    size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
        return "<mutation '" + from + "' does not occur exactly once>";
    }
    return text.replace(at, from.size(), to);
}

}  // namespace

TEST(Summary, GoldenRoundTrip)
{
    // 104 份手写摘要：doc 被忽略，读入再写出与去掉 doc 的原文完全相同（结构没有漏字段）
    std::vector<std::string> files = SummaryFiles();
    EXPECT_EQ(files.size(), size_t(104));
    for (const std::string &path : files) {
        auto buffer = llvm::MemoryBuffer::getFile(path);
        ASSERT_TRUE(static_cast<bool>(buffer));
        llvm::Expected<llvm::json::Value> v = llvm::json::parse((*buffer)->getBuffer());
        ASSERT_TRUE(static_cast<bool>(v));
        Summary s;
        std::string error;
        bool ok = ReadSummary(*v, s, error);
        EXPECT_EQ(path + ": " + (ok ? "<ok>" : error), path + ": <ok>");
        if (ok) {
            EXPECT_EQ(Print(SummaryToJson(s)), Print(StripDoc(*v)));
        }
    }
}

TEST(Summary, DocIsIgnoredEverywhere)
{
    EXPECT_EQ(ErrorOf(kMinimal), "<ok>");
    EXPECT_EQ(ErrorOf(Mutate(R"("status": "analyzed",)", R"("status": "analyzed", "doc": "x",)")), "<ok>");
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(kMinimal);
    ASSERT_TRUE(static_cast<bool>(v));
    EXPECT_FALSE(llvm::StringRef(Print(StripDoc(*v))).contains("doc"));
}

TEST(Summary, ErrorsCarryLocation)
{
    EXPECT_EQ(ErrorOf(Mutate("oobsum-summary/1", "oobsum-summary/2")), "schema: expected 'oobsum-summary/1'");
    EXPECT_EQ(ErrorOf(Mutate(R"("status": "analyzed",)", R"("status": "analyzed", "note": 1,)")),
              "<root>: unknown key 'note'");
    EXPECT_EQ(ErrorOf(Mutate(R"("kind": "W")", R"("kind": "X")")), "reqs[0].kind: unexpected value 'X'");
    EXPECT_EQ(ErrorOf(Mutate(R"j("have": "ext(p0)", "obj")j", R"j("have": "ext(p0", "obj")j")),
              "reqs[0].have: bad expression 'ext(p0': expected ')' at 6 (end)");
    EXPECT_EQ(ErrorOf(Mutate(R"(["p1 >= 0"])", R"(["p1 != 0"])")),
              "reqs[0].guard[0]: bad condition 'p1 != 0': not a condition");
    EXPECT_EQ(ErrorOf(Mutate(R"("obj": "*p0", "end")", R"("obj": "p0[1]", "end")")), "reqs[0].obj: bad object 'p0[1]'");
    EXPECT_EQ(ErrorOf(Mutate(R"("bound": "loop")", R"("bound": "magic")")), "reqs[0].bound: unknown bound tag 'magic'");
    EXPECT_EQ(ErrorOf(Mutate(R"("depth": 0)", R"("depth": "0")")),
              "reqs[0].depth: expected an integer strictly inside the int64 range");
    EXPECT_EQ(ErrorOf(Mutate(R"(, "blind": false)", "")), "reqs[0]: missing key 'blind'");
    EXPECT_EQ(ErrorOf(Mutate(R"("outcome": "up", "as": "R1")", R"("outcome": "up")")),
              "calls[0].results[0]: missing key 'as' for outcome up");
    EXPECT_EQ(ErrorOf(Mutate(R"("outcome": "up", "as": "R1")", R"("outcome": "safe", "as": "R1")")),
              "calls[0].results[0]: unexpected key 'as' for outcome safe");
    EXPECT_EQ(ErrorOf(Mutate(R"("outcome": "up", "as": "R1")", R"("outcome": "open", "reason": "because")")),
              "calls[0].results[0].reason: unknown ledger reason 'because'");
    EXPECT_EQ(ErrorOf(Mutate(R"j({"obj": "*p0", "have": "ext(p0)"}, "4")j", R"j({"obj": "*p0"}, "4")j")),
              "calls[0].args[0]: missing key 'have'");
    EXPECT_EQ(ErrorOf(Mutate(R"(, "4", null])", R"(, "4 +", null])")),
              "calls[0].args[1]: bad expression '4 +': expected a name at 3 (end)");
}

TEST(Summary, FileRoundTrip)
{
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(kMinimal);
    ASSERT_TRUE(static_cast<bool>(v));
    Summary s;
    std::string error;
    ASSERT_TRUE(ReadSummary(*v, s, error));
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-summary-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "f.json");
    ASSERT_TRUE(WriteSummaryFile(path, s, error));
    EXPECT_FALSE(llvm::sys::fs::exists(path + ".tmp"));
    Summary back;
    ASSERT_TRUE(ReadSummaryFile(path, back, error));
    EXPECT_EQ(Print(SummaryToJson(back)), Print(SummaryToJson(s)));
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}

// 负偏移（SPEC-0039）：Req / Rep 的 dir、指针实参的 back 读入再写出不变；dir 只能是 back
TEST(Summary, BackFieldsRoundTrip)
{
    std::string path;
    for (const std::string &p : SummaryFiles()) {
        if (llvm::StringRef(p).endswith("Om_OnSaveCfg_TP.json")) {
            path = p;
        }
    }
    ASSERT_TRUE(!path.empty());
    auto buffer = llvm::MemoryBuffer::getFile(path);
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::Expected<llvm::json::Value> v = llvm::json::parse((*buffer)->getBuffer());
    ASSERT_TRUE(static_cast<bool>(v));
    llvm::json::Value in = StripDoc(*v);
    llvm::json::Object *o = in.getAsObject();
    (*o->getArray("reqs"))[0].getAsObject()->insert({"dir", "back"});
    llvm::json::Object *arg = (*(*o->getArray("calls"))[0].getAsObject()->getArray("args"))[0].getAsObject();
    ASSERT_TRUE(arg != nullptr);
    arg->insert({"back", "p1 + bext(p0)"});
    Summary s;
    std::string error;
    ASSERT_TRUE(ReadSummary(in, s, error));
    EXPECT_EQ(s.reqs[0].dir, "back");
    ASSERT_TRUE(s.calls[0].args[0].ptr->back.hasValue());
    EXPECT_EQ(*s.calls[0].args[0].ptr->back, "p1 + bext(p0)");
    EXPECT_EQ(Print(SummaryToJson(s)), Print(in));
    (*(*o->getArray("reqs"))[0].getAsObject())["dir"] = "front";
    EXPECT_FALSE(ReadSummary(in, s, error));
}

// mod-ref（SPEC-0045）：mods 读入再写出不变；空的不写
TEST(Summary, ModsRoundTrip)
{
    std::vector<std::string> files = SummaryFiles();
    ASSERT_TRUE(!files.empty());
    auto buffer = llvm::MemoryBuffer::getFile(files[0]);
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::Expected<llvm::json::Value> v = llvm::json::parse((*buffer)->getBuffer());
    ASSERT_TRUE(static_cast<bool>(v));
    llvm::json::Value in = StripDoc(*v);
    Summary s;
    std::string error;
    ASSERT_TRUE(ReadSummary(in, s, error));
    EXPECT_TRUE(s.mods.empty());
    EXPECT_TRUE(SummaryToJson(s).getAsObject()->get("mods") == nullptr);
    in.getAsObject()->insert({"mods", llvm::json::Array{"*p0->pucBuf", "g:g_x", "p0+4:4"}});
    ASSERT_TRUE(ReadSummary(in, s, error));
    ASSERT_TRUE(s.mods.size() == 3);
    EXPECT_EQ(s.mods[2], "p0+4:4");
    EXPECT_EQ(Print(SummaryToJson(s)), Print(in));
}

TEST(Summary, GvalsRoundTrip)
{
    // 调用点记录的 gvals（SPEC-0064）：值是表达式或 "?"；读写往返；坏表达式报错
    Summary s;
    s.fn = "Top";
    s.key = "F:Top";
    s.loc = "t.c:1";
    Call c;
    c.site = "t.c:2";
    c.callee = "Fill";
    c.gvals = std::map<std::string, std::string>{{"g:*", "?"}, {"g:g_ulLen", "p0 + 4"}, {"g:g_ulCnt", "?"}};
    s.calls.push_back(c);
    llvm::json::Value out = SummaryToJson(s);
    Summary back;
    std::string error;
    ASSERT_TRUE(ReadSummary(out, back, error));
    ASSERT_TRUE(back.calls.size() == 1 && back.calls[0].gvals.hasValue());
    EXPECT_EQ(back.calls[0].gvals->at("g:g_ulLen"), "p0 + 4");
    EXPECT_EQ(back.calls[0].gvals->at("g:*"), "?");
    EXPECT_EQ(Print(SummaryToJson(back)), Print(out));
    out.getAsObject()->getArray("calls")->front().getAsObject()->getObject("gvals")->insert({"g:bad", "p0 +"});
    EXPECT_FALSE(ReadSummary(out, back, error));
}

TEST(Summary, FieldMayRoundTrip)
{
    // 写效应 field 的 may（SPEC-0073）：读写往返
    Summary s;
    s.fn = "Get";
    s.key = "F:Get";
    s.loc = "t.c:1";
    Eff e;
    e.kind = "field";
    e.target = "p0->_0";
    e.value = "32";
    e.may = true;
    s.effs.push_back(e);
    llvm::json::Value out = SummaryToJson(s);
    Summary back;
    std::string error;
    ASSERT_TRUE(ReadSummary(out, back, error));
    ASSERT_TRUE(back.effs.size() == 1);
    EXPECT_TRUE(back.effs[0].may.getValueOr(false));
    EXPECT_EQ(Print(SummaryToJson(back)), Print(out));
}

// CharAlias=verify 的不别名假设（SPEC-0106）：assumes 读入再写出不变；空的不写；不是两个字符串 → 报错
TEST(Summary, AssumesRoundTrip)
{
    std::vector<std::string> files = SummaryFiles();
    ASSERT_TRUE(!files.empty());
    auto buffer = llvm::MemoryBuffer::getFile(files[0]);
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::Expected<llvm::json::Value> v = llvm::json::parse((*buffer)->getBuffer());
    ASSERT_TRUE(static_cast<bool>(v));
    llvm::json::Value in = StripDoc(*v);
    Summary s;
    std::string error;
    ASSERT_TRUE(ReadSummary(in, s, error));
    EXPECT_TRUE(SummaryToJson(s).getAsObject()->get("assumes") == nullptr);
    in.getAsObject()->insert(
        {"assumes", llvm::json::Array{llvm::json::Array{"p0", "p1"}, llvm::json::Array{"g:g_stCtx", "p0"}}});
    ASSERT_TRUE(ReadSummary(in, s, error));
    ASSERT_TRUE(s.assumes.size() == 2);
    EXPECT_EQ(s.assumes[1].first + "&" + s.assumes[1].second, "g:g_stCtx&p0");
    EXPECT_EQ(Print(SummaryToJson(s)), Print(in));
    (*in.getAsObject())["assumes"] = llvm::json::Array{llvm::json::Array{"p0"}};
    EXPECT_FALSE(ReadSummary(in, s, error));
}
