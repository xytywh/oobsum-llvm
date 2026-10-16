//===-- RangeTest.cpp - Tests for core/Range --------------------*- C++ -*-===//
//
// 取值范围与蕴含（SPEC-0011）：与 replay.py 的 Env / entails_le0 逐项同语义。金标准 data/range-golden.txt 由 gen_range_golden.py 生成。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/LinExpr.h"
#include "OobSummary/core/Range.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"

#include <string>
#include <vector>

using namespace oobsum;

namespace {

LinExpr L(llvm::StringRef text)
{
    LinExpr e;
    std::string error;
    ParseLin(text, e, error);
    return e;
}

std::vector<LinExpr> Atoms(llvm::StringRef conds)
{
    std::vector<LinExpr> out;
    llvm::SmallVector<llvm::StringRef, 4> parts;
    conds.split(parts, ';', -1, false);
    for (llvm::StringRef c : parts) {
        std::vector<LinExpr> le0;
        std::string error;
        ParseCond(c, le0, error);
        out.insert(out.end(), le0.begin(), le0.end());
    }
    return out;
}

// 与 gen_range_golden.py 的 env_of 相同：形参 p<i> 与字段按类型名取范围，再加带标签的守卫
SymEnv EnvOf(llvm::StringRef params, llvm::StringRef fields, llvm::StringRef guards)
{
    SymEnv env;
    llvm::SmallVector<llvm::StringRef, 4> parts;
    params.split(parts, ',', -1, false);
    for (unsigned i = 0; i < parts.size(); ++i) {
        int64_t lo = 0;
        int64_t hi = 0;
        if (TypeRangeOf(parts[i], lo, hi)) {
            env.SetTypeRange("p" + std::to_string(i), lo, hi);
        }
    }
    parts.clear();
    fields.split(parts, ';', -1, false);
    for (llvm::StringRef f : parts) {
        int64_t lo = 0;
        int64_t hi = 0;
        if (TypeRangeOf(f.split(':').second, lo, hi)) {
            env.SetTypeRange(f.split(':').first, lo, hi);
        }
    }
    parts.clear();
    guards.split(parts, ';', -1, false);
    for (llvm::StringRef g : parts) {
        std::pair<llvm::StringRef, llvm::StringRef> condTag = g.rsplit('@');
        llvm::Optional<Tag> tag = ParseTag(condTag.second);
        for (const LinExpr &atom : Atoms(condTag.first)) {
            env.AddGuard(atom, tag ? *tag : Tag::Guard);
        }
    }
    return env;
}

std::string Show(const Limit &l)
{
    if (!l.known) {
        return "none";
    }
    return std::to_string(l.value) + " " + (l.tag == Tag::None ? "-" : TagName(l.tag).str());
}

}  // namespace

TEST(Range, Tags)
{
    EXPECT_EQ(TagName(Tag::Check), "check");
    EXPECT_TRUE(ParseTag("loop").hasValue());
    EXPECT_FALSE(ParseTag("bogus").hasValue());
    // 合并时取 type widen data model check guard mask loop branch ret const exact 里最靠前的
    EXPECT_EQ(TagName(WorseTag(Tag::Loop, Tag::Check)), "check");
    EXPECT_EQ(TagName(WorseTag(Tag::Check, Tag::Model)), "model");  // SPEC-0034
    EXPECT_EQ(TagName(WorseTag(Tag::Type, Tag::Model)), "type");
    EXPECT_TRUE(ParseTag("model") == Tag::Model);
    EXPECT_TRUE(IsCodeTag(Tag::Model));
    EXPECT_EQ(TagName(WorseTag(Tag::Exact, Tag::Type)), "type");
    EXPECT_EQ(TagName(WorseTag(Tag::None, Tag::Mask)), "mask");
    EXPECT_TRUE(IsCodeTag(Tag::Guard));
    EXPECT_FALSE(IsCodeTag(Tag::Type));
    EXPECT_FALSE(IsCodeTag(Tag::Widen));
    EXPECT_FALSE(IsCodeTag(Tag::Data));
}

TEST(Range, GuardsRoundCorrectly)
{
    SymEnv env;
    env.AddGuard(L("2*p0 - 7"), Tag::Check);  // 2·p0 ≤ 7 → p0 ≤ 3
    EXPECT_EQ(Show(env.Upper("p0")), "3 check");
    env.AddGuard(L("7 - 3*p1"), Tag::Loop);  // 3·p1 ≥ 7 → p1 ≥ 3
    EXPECT_EQ(Show(env.Lower("p1")), "3 loop");
    env.AddGuard(L("-2*p2 - 7"), Tag::Check);  // p2 ≥ −3.5 → p2 ≥ −3
    EXPECT_EQ(Show(env.Lower("p2")), "-3 check");
    env.AddGuard(L("2*p3 + 7"), Tag::Check);  // p3 ≤ −3.5 → p3 ≤ −4
    EXPECT_EQ(Show(env.Upper("p3")), "-4 check");
    env.AddGuard(L("p0 + p1 - 1"), Tag::Check);  // 多符号：不记
    EXPECT_EQ(Show(env.Upper("p0")), "3 check");
    env.AddGuard(L("ext(p0) - 4"), Tag::Check);  // ext 上的守卫：不记
    EXPECT_EQ(Show(env.RangeOf(L("ext(p0)"), true)), "none");
}

TEST(Range, GuardReplacesTypeRangeEvenIfLooser)
{
    SymEnv env;
    env.SetTypeRange("p0", 0, 255);
    env.AddGuard(L("p0 - 1000"), Tag::Check);  // 更松，但类型界会被代码给出的界替换（同 replay.py）
    EXPECT_EQ(Show(env.Upper("p0")), "1000 check");
    env.AddGuard(L("p0 - 2000"), Tag::Loop);  // 更松且原来不是类型界：不替换
    EXPECT_EQ(Show(env.Upper("p0")), "1000 check");
}

TEST(Range, OverflowHasNoBound)
{
    SymEnv env;
    env.SetTypeRange("p0", 0, INT64_MAX / 2);
    EXPECT_EQ(Show(env.RangeOf(L("4*p0"), true)), "none");
    EXPECT_EQ(Show(env.RangeOf(LinExpr::Unknown(), true)), "none");
}

TEST(Range, GoldenFromReplay)
{
    auto buffer = llvm::MemoryBuffer::getFile(OOBSUM_TEST_DATA_DIR "/range-golden.txt");
    ASSERT_TRUE(static_cast<bool>(buffer));
    llvm::SmallVector<llvm::StringRef, 64> lines;
    (*buffer)->getBuffer().split(lines, '\n', -1, false);
    unsigned checked = 0;
    for (llvm::StringRef line : lines) {
        if (line.startswith("#")) {
            continue;
        }
        llvm::SmallVector<llvm::StringRef, 8> c;
        line.split(c, '\t');
        SymEnv env = EnvOf(c[1], c[2], c[3]);
        if (c[0] == "R") {
            ASSERT_TRUE(c.size() == 8);
            std::string want = c[6] == "none" ? "none" : (c[6] + " " + c[7]).str();
            std::string got = Show(env.RangeOf(L(c[4]), c[5] == "hi"));
            EXPECT_EQ(line.str() + " -> " + got, line.str() + " -> " + want);
        } else {
            ASSERT_TRUE(c.size() == 7);
            bool got = EntailsLe0(L(c[4]), Atoms(c[5]), env);
            EXPECT_EQ(line.str() + " -> " + (got ? "1" : "0"), line.str() + " -> " + c[6].str());
        }
        ++checked;
    }
    EXPECT_TRUE(checked >= 50);
}
