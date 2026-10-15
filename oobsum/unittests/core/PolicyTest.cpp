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
