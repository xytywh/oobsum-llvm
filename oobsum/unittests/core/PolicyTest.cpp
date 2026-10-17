//===-- PolicyTest.cpp - Tests for core/Policy and core/Label ---*- C++ -*-===//
//
// 报告策略的全部分支（SPEC-0001 §3 的表）：召回优先——拿不准报 SUSPECT，SUSPECT 不按层数过滤，只有 SAFE 默认不报。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Decision.h"
#include "OobSummary/core/Label.h"
#include "OobSummary/core/Policy.h"

#include <set>
#include <string>

using namespace oobsum;

namespace {

Decision Make(Label label, unsigned depth, unsigned effDepth = 0, const std::string &reason = "")
{
    Decision decision;
    decision.label = label;
    decision.depth = depth;
    decision.effDepth = effDepth;
    decision.reason = reason;
    return decision;
}

std::string Why(const Decision &decision, const PolicyOptions &options)
{
    Disposition d = Classify(decision, options);
    return d.emit ? std::string("emit") : "drop:" + d.why;
}

}  // namespace

TEST(Policy, DefiniteAndPossibleDeepEnoughAreEmitted)
{
    PolicyOptions recall;
    EXPECT_EQ(Why(Make(Label::Definite, 3), recall), "emit");
    EXPECT_EQ(Why(Make(Label::Possible, 3), recall), "emit");
}

TEST(Policy, InlineDepthUsesMaxOfDepthAndEffDepth)
{
    // F-20：max(depth, effDepth) <= InlineDepth 的交给现有规则（仍写日志）
    PolicyOptions recall;
    EXPECT_EQ(Why(Make(Label::Definite, 2), recall), "drop:inline_depth");
    EXPECT_EQ(Why(Make(Label::Possible, 1, 2), recall), "drop:inline_depth");
    EXPECT_EQ(Why(Make(Label::Definite, 1, 3), recall), "emit");
    PolicyOptions all;
    all.inlineDepth = 0;
    EXPECT_EQ(Why(Make(Label::Definite, 0), all), "emit");
}

TEST(Policy, SuspectIsEmittedAtAnyDepth)
{
    PolicyOptions recall;
    EXPECT_EQ(Why(Make(Label::Suspect, 0, 0, "entry_unproven"), recall), "emit");
    EXPECT_EQ(Why(Make(Label::Suspect, 1, 1, "type_range_only"), recall), "emit");
}

TEST(Policy, BalancedDropsSuspect)
{
    PolicyOptions balanced;
    balanced.recall = false;
    EXPECT_EQ(Why(Make(Label::Suspect, 5, 0, "fptr"), balanced), "drop:policy_balanced");
    EXPECT_EQ(Why(Make(Label::Definite, 5), balanced), "emit");
}

TEST(Policy, SuspectReasonsFilter)
{
    PolicyOptions picky;
    picky.suspectReasons = {"type_range_only"};
    EXPECT_EQ(Why(Make(Label::Suspect, 3, 0, "type_range_only"), picky), "emit");
    EXPECT_EQ(Why(Make(Label::Suspect, 3, 0, "unknown_callee"), picky), "drop:reason_filtered");
    EXPECT_EQ(Why(Make(Label::Definite, 3), picky), "emit");
}

// 同一入口同一次访问的重复账本（SPEC-0036）：指向的那条报出了才不报；那条被过滤掉时照常
TEST(Policy, DuplicateAccessIsLoggedNotEmitted)
{
    Decision first = Make(Label::Suspect, 3, 0, "type_range_only");
    first.fp = "aaaaaaaaaaaaaaaa";
    Decision dup = Make(Label::Suspect, 3, 0, "entry_unproven");
    dup.fp = "bbbbbbbbbbbbbbbb";
    dup.dupOf = first.fp;
    PolicyOptions recall;
    std::vector<Disposition> got = ClassifyBatch({first, dup}, recall);
    ASSERT_TRUE(got.size() == 2);
    EXPECT_TRUE(got[0].emit);
    EXPECT_FALSE(got[1].emit);
    EXPECT_EQ(got[1].why, "duplicate_access");
    PolicyOptions picky;  // 第一条的原因被滤掉：第二条照常报
    picky.suspectReasons = {"entry_unproven"};
    got = ClassifyBatch({first, dup}, picky);
    EXPECT_FALSE(got[0].emit);
    EXPECT_TRUE(got[1].emit);
    EXPECT_TRUE(Classify(dup, recall).emit);  // 单条 Classify 不看 dupOf
}

TEST(Policy, SafeIsLoggedNotEmitted)
{
    PolicyOptions recall;
    EXPECT_EQ(Why(Make(Label::Safe, 3, 0, "guard_false"), recall), "drop:safe");
    PolicyOptions audit;
    audit.reportSafe = true;
    EXPECT_EQ(Why(Make(Label::Safe, 3, 0, "guard_false"), audit), "emit");
}

TEST(Policy, SplitList)
{
    std::set<std::string> expected = {"a", "b", "c"};
    EXPECT_TRUE(SplitList("a+b+c") == expected);
    EXPECT_TRUE(SplitList(" a + b,c ") == expected);
    EXPECT_TRUE(SplitList("").empty());
    EXPECT_TRUE(SplitList("++").empty());
}

TEST(Label, NamesRoundTrip)
{
    for (Label label : {Label::Definite, Label::Possible, Label::Suspect, Label::Safe}) {
        llvm::Optional<Label> parsed = ParseLabel(LabelName(label));
        ASSERT_TRUE(parsed.hasValue());
        EXPECT_EQ(LabelName(*parsed), LabelName(label));
    }
    EXPECT_EQ(LabelName(Label::Suspect), "suspect");
    // 告警消息前缀 [OOB-<标签>]（SPEC-0001 §3，F-32）
    EXPECT_EQ(LabelTag(Label::Definite), "OOB-DEFINITE");
    EXPECT_FALSE(ParseLabel("L1").hasValue());
}

TEST(Policy, SubobjectNotFilteredByDepth)
{
    // SPEC-0105：内层的判定（dir sub）不按 InlineDepth 过滤——现有规则不查成员数组 / 多维数组的一行
    PolicyOptions recall;
    Decision inner = Make(Label::Definite, 1);
    inner.dir = "sub";
    EXPECT_EQ(Why(inner, recall), "emit");
    inner.depth = 0;
    EXPECT_EQ(Why(inner, recall), "emit");
    Decision outer = Make(Label::Definite, 1);
    EXPECT_EQ(Why(outer, recall), "drop:inline_depth");
    Decision back = Make(Label::Possible, 1);
    back.dir = "back";
    EXPECT_EQ(Why(back, recall), "drop:inline_depth");
}
