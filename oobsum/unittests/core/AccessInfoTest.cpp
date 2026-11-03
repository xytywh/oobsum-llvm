//===-- AccessInfoTest.cpp - Diagnostic metadata round trips ----*- C++ -*-===//
//
// SPEC-0125：展示字段往返、坏类型有错误、旧产物缺字段时不会沿用上一次的访问。
//
//===----------------------------------------------------------------------===//
#include "Test.h"
#include "FirExamples.h"

#include "OobSummary/core/AccessInfo.h"
#include "OobSummary/core/DecisionLog.h"
#include "OobSummary/core/FirJson.h"
#include "OobSummary/core/Summary.h"

#include "llvm/Support/JSON.h"

using namespace oobsum;

namespace {
AccessInfo Example()
{
    AccessInfo info;
    info.category = "array-index";
    info.expression = "p[i].value";
    info.type = "U32";
    info.elementType = "Packet";
    info.elementSize = 20;
    info.width = 4;
    info.fieldOffset = 16;
    info.index = "i";
    info.origin = "global pointer 'g_table'";
    return info;
}
}  // namespace

TEST(AccessInfo, JsonRoundTripAndBadTypes)
{
    llvm::json::Value json(AccessInfoToJson(Example()));
    AccessInfo back;
    std::string error;
    EXPECT_TRUE(AccessInfoFromJson(json, back, error));
    EXPECT_EQ(back.expression, "p[i].value");
    EXPECT_EQ(back.elementSize, 20);
    EXPECT_EQ(back.fieldOffset, 16);
    EXPECT_EQ(back.origin, "global pointer 'g_table'");
    (*json.getAsObject())["width"] = "four";
    EXPECT_FALSE(AccessInfoFromJson(json, back, error));
    EXPECT_TRUE(error.find("width") != std::string::npos);
}

TEST(AccessInfo, FirRoundTripRejectsInvalidDiagnostic)
{
    auto module = fir::examples::BuildEverything();
    auto &inst = module.funcs[0].blocks[0].insts[0];
    inst.diag = 7;
    inst.accesses.push_back(Example());
    llvm::json::Value json = fir::ModuleToJson(module);
    fir::Module back;
    std::string error;
    EXPECT_TRUE(fir::ModuleFromJson(json, back, error));
    EXPECT_EQ(back.funcs[0].blocks[0].insts[0].diag, 7);
    EXPECT_EQ(back.funcs[0].blocks[0].insts[0].accesses[0].elementType, "Packet");
    auto *function = (*json.getAsObject()->getArray("funcs"))[0].getAsObject();
    auto *block = (*function->getArray("blocks"))[0].getAsObject();
    auto *stored = (*block->getArray("insts"))[0].getAsObject();
    auto *access = (*stored->getArray("accesses"))[0].getAsObject();
    (*access)["element_size"] = -1;
    fir::Module invalid;
    EXPECT_FALSE(fir::ModuleFromJson(json, invalid, error));
    EXPECT_TRUE(error.find("accesses[0]") != std::string::npos);
}

TEST(AccessInfo, SummaryIdsRoundTrip)
{
    summary::Summary input;
    input.fn = "f";
    input.key = "F:f";
    input.loc = "m.c:1";
    input.status = "analyzed";
    summary::Call call;
    call.site = "m.c:1";
    call.callee = "Other";
    call.diag = 4;
    input.calls.push_back(call);
    input.opens.push_back({"untracked_pointer", "m.c:1", llvm::None, {1, 2}});
    summary::Rep rep;
    rep.id = "P1";
    rep.level = "L1";
    rep.site = "m.c:1";
    rep.access = {"f", "F:f", "m.c:1", "5", 3};
    rep.kind = "W";
    rep.obj = "local:a";
    rep.have = "4";
    rep.endLo = rep.endHi = "5";
    rep.bound = "exact";
    rep.chain = {"f@m.c:1"};
    rep.fp = "0123456789abcdef";
    rep.flex = 0;
    input.reps.push_back(rep);
    summary::Summary back;
    std::string error;
    EXPECT_TRUE(summary::ReadSummary(summary::SummaryToJson(input), back, error));
    EXPECT_EQ(back.calls[0].diag, 4);
    EXPECT_EQ(back.reps[0].access.diag, 3);
    EXPECT_EQ(back.reps[0].flex, 0);
    EXPECT_EQ(back.opens[0].diags.size(), static_cast<size_t>(2));
}

TEST(AccessInfo, DecisionRoundTripAndOldDataClearsMetadata)
{
    Decision input;
    input.label = Label::Definite;
    input.site = {"m.c", 1, 2};
    input.sink = {"leaf.c", 42, 3};
    input.fn = "F:f";
    input.kind = "W";
    input.obj = "local:a";
    input.have = "4";
    input.endLo = input.endHi = "5";
    input.accessKey = "F:leaf";
    input.accessIds = {7};
    input.target = "arm ptr=4";
    input.accesses = {Example()};
    llvm::json::Value json(DecisionToJson(input, Disposition()));
    Decision back;
    std::string error;
    EXPECT_TRUE(DecisionFromJson(json, back, error));
    EXPECT_EQ(back.sink.file, "leaf.c");
    EXPECT_EQ(back.sink.line, 42u);
    EXPECT_EQ(back.accesses[0].fieldOffset, 16);
    for (const char *key : {"sink", "access_key", "access_ids", "target", "accesses"}) {
        json.getAsObject()->erase(key);
    }
    EXPECT_TRUE(DecisionFromJson(json, back, error));
    EXPECT_TRUE(back.sink.file.empty());
    EXPECT_TRUE(back.accesses.empty());
    EXPECT_TRUE(back.accessIds.empty());
}
