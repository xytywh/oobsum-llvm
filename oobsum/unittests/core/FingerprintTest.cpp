//===-- FingerprintTest.cpp - Tests for core/Fingerprint --------*- C++ -*-===//
//
// 06- F-34 / A-92：指纹与 replay.py 的 fingerprint() 逐位相同。期望值由 replay.py 算出。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Key.h"

using namespace oobsum;

TEST(Fingerprint, HashHexMatchesPython)
{
    EXPECT_EQ(HashHex(""), "ef46db3751d8e999");
    EXPECT_EQ(HashHex("abc"), "44bc2cf5ad770999");
}

TEST(Fingerprint, TuIdIsFileId)
{
    // SPEC-0003 §2.1：TU id 与文件 id 同一规则
    EXPECT_EQ(TuId("/a/./c.c"), FileId("/a/c.c"));
    EXPECT_EQ(TuId("rel/x.c"), FileId("rel/x.c"));
}

TEST(Fingerprint, DecisionMatchesReplay)
{
    // replay.fingerprint('definite', 'F:Cell_OnSetupTimeout_TP',
    //                    {'key': 'F@3fa1c2d4e5f60718:Stat_Inc', 'end': '4*p0 + 4'}, 'W', 'g:g_aulCellStat')
    EXPECT_EQ(DecisionFingerprint("definite", "F:Cell_OnSetupTimeout_TP", "F@3fa1c2d4e5f60718:Stat_Inc", "W",
                                  "4*p0 + 4", "g:g_aulCellStat"),
              "1a0411cf5870c151");
}

TEST(Fingerprint, LabelIsPartOfIt)
{
    // SPEC-0001 §3：指纹的第一项是标签名，SUSPECT 升为 DEFINITE 时指纹会变
    EXPECT_FALSE(DecisionFingerprint("definite", "F:a", "F:b", "W", "4", "g:x") ==
                 DecisionFingerprint("suspect", "F:a", "F:b", "W", "4", "g:x"));
}
