// SPEC-0136：诊断来源不影响推理；按函数/指令文件证据而非同名首项定位。
#include "Test.h"
#include "OobSummary/core/SourceMap.h"
#include "OobSummary/core/Key.h"

namespace {
oobsum::fir::Function Fn(const char *key, const char *name, unsigned file)
{
    oobsum::fir::Function fn;
    fn.key = key;
    fn.name = name;
    fn.loc = {file, 1, 1};
    oobsum::fir::Block block;
    oobsum::fir::Inst inst;
    inst.loc = {file, 3, 1};
    block.insts.push_back(inst);
    fn.blocks.push_back(block);
    return fn;
}
}  // namespace
TEST(SourceMap, FunctionKeySeparatesSameBasename)
{
    oobsum::fir::Module m;
    m.files = {"/a/common.h", "/b/common.h"};
    m.funcs = {Fn("F:A", "Same", 0), Fn("F:B", "Same", 1)};
    oobsum::SourceMap map({&m});
    EXPECT_EQ(map.Resolve({"F:A"}, "common.h:3").site.file, "/a/common.h");
    EXPECT_EQ(map.Resolve({"F:B"}, "common.h:3").site.file, "/b/common.h");
    auto ambiguous = map.Resolve(map.KeysForName("Same"), "common.h:3");
    EXPECT_EQ(ambiguous.status, "ambiguous");
    EXPECT_EQ(ambiguous.candidates, size_t(2));
    EXPECT_TRUE(llvm::StringRef(ambiguous.site.file).startswith(oobsum::kUnresolvedSourcePrefix));
}
TEST(SourceMap, FragmentsAreNotAssumedToMatchDeclaration)
{
    oobsum::fir::Module m;
    m.files = {"/a/common.h", "/b/common.h"};
    auto fn = Fn("F:A", "A", 0);
    fn.blocks[0].insts[0].loc = {1, 5, 2};
    m.funcs.push_back(fn);
    oobsum::SourceMap map({&m});
    EXPECT_EQ(map.Resolve({"F:A"}, "common.h:5").site.file, "/b/common.h");
    EXPECT_EQ(map.Resolve({"F:A"}, "common.h:1").site.file, "/a/common.h");
    EXPECT_EQ(map.Resolve({"F:A"}, "common.h:2").status, "missing");
    EXPECT_EQ(map.Resolve({"F:A"}, "/b/common.h:5").site.file, "/b/common.h");
}
TEST(SourceMap, TrueInFunctionAmbiguityDoesNotGuess)
{
    oobsum::fir::Module m;
    m.files = {"/a/common.h", "/b/common.h"};
    auto fn = Fn("F:A", "A", 0);
    auto second = fn.blocks[0].insts[0];
    second.loc.file = 1;
    fn.blocks[0].insts.push_back(second);
    m.funcs.push_back(fn);
    oobsum::SourceMap map({&m});
    EXPECT_EQ(map.Resolve({"F:A"}, "common.h:3").status, "ambiguous");
    EXPECT_EQ(map.Resolve({}, "common.h:3").status, "ambiguous");
    EXPECT_EQ(map.Resolve({}, "absent.h:3").status, "missing");
}
TEST(SourceMap, NormalizedAliasesAndUniqueLegacyFallback)
{
    oobsum::fir::Module m;
    m.files = {"/a/common.h", "/a/../a/common.h"};
    m.funcs = {Fn("F:A", "A", 0)};
    oobsum::SourceMap map({&m});
    auto r = map.Resolve({"F:A"}, "common.h:3");
    EXPECT_EQ(r.status, "unique");
    EXPECT_EQ(r.candidates, size_t(1));
    EXPECT_EQ(oobsum::NormalizePath(r.site.file), "/a/common.h");
    EXPECT_EQ(map.Resolve({}, "common.h:3").status, "unique");
    EXPECT_EQ(map.Resolve({}, "common.h:bad").status, "missing");
}
