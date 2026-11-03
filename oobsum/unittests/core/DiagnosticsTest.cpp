//===-- DiagnosticsTest.cpp - Human bounds explanations ---------*- C++ -*-===//
//
// SPEC-0126：用业务可复核的单位描述已记录的界，不重新推理。
//
//===----------------------------------------------------------------------===//
#include "Test.h"

#include "OobSummary/core/Diagnostics.h"

using namespace oobsum;

TEST(Diagnostics, StructArrayElementsAndExclusiveEnd)
{
    Decision d;
    d.label = Label::Possible;
    d.kind = "W";
    d.obj = "local:a";
    d.have = "60";
    d.endLo = "4";
    d.endHi = "64";
    d.bound = "loop";
    AccessInfo info;
    info.category = "array-index";
    info.mode = "W";
    info.expression = "p[i].value";
    info.elementType = "Item";
    info.type = "U32";
    info.elementSize = 20;
    info.width = 4;
    info.index = "i";
    d.accesses = {info};
    d.chain = {{"Top", {"top.c", 12, 1}}, {"Leaf", {"leaf.c", 7, 4}}};
    std::string text = DiagnosticMessage(d);
    EXPECT_TRUE(text.find("20 bytes each, 3 complete elements") != std::string::npos);
    EXPECT_TRUE(text.find("Required exclusive end: [4, 64]") != std::string::npos);
    EXPECT_TRUE(text.find("Extent exceeded by up to 4 bytes") != std::string::npos);
    EXPECT_TRUE(text.find("indices relative to this pointer: [0, 3], valid [0, 2]") != std::string::npos);
    EXPECT_TRUE(text.find("Top (top.c:12) -> Leaf (leaf.c:7)") != std::string::npos);
}

TEST(Diagnostics, UnknownTargetAndCodeConstraint)
{
    Decision d;
    d.reason = "untracked_pointer";
    AccessInfo info;
    info.category = "array-index";
    info.expression = "p[i]";
    info.origin = "global pointer 'g_table'";
    d.accesses = {info};
    std::string text = DiagnosticMessage(d);
    EXPECT_TRUE(text.find("Available extent: unknown") != std::string::npos);
    EXPECT_TRUE(text.find("global pointer 'g_table'") != std::string::npos);
    d.reason = "type_range_only";
    d.have = "4";
    d.endLo = "1";
    d.endHi = "65535";
    text = DiagnosticMessage(d);
    EXPECT_TRUE(text.find("no usable code constraint") != std::string::npos);
    EXPECT_TRUE(text.find("only its C type") != std::string::npos);
}

TEST(Diagnostics, BackwardsRangeAndStableLineGroup)
{
    Decision d;
    d.label = Label::Definite;
    d.dir = "back";
    d.have = "0";
    d.endLo = d.endHi = "4";
    AccessInfo info;
    info.expression = "p[-1]";
    d.accesses = {info};
    EXPECT_TRUE(DiagnosticMessage(d).find("Underflow by up to 4 bytes") != std::string::npos);
    d.sink = {"leaf.c", 9, 4};
    d.target = "arm";
    std::string first = DiagnosticGroup(d);
    d.sink.col = 40;
    EXPECT_EQ(DiagnosticGroup(d), first);
    d.sink.line = 10;
    EXPECT_TRUE(DiagnosticGroup(d) != first);
    d.sink.line = 9;
    d.target = "x86";
    EXPECT_TRUE(DiagnosticGroup(d) != first);
}
