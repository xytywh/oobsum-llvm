//===-- ModelsTest.cpp - Tests for core/Models ------------------*- C++ -*-===//
//
// 模型表的默认值与查找（SPEC-0018 §2.1；06- I-40、I-44）。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Models.h"

using namespace oobsum::models;

TEST(Models, DefaultKinds)
{
    const ModelTable &t = ModelTable::Default();
    EXPECT_TRUE(t.KindOf("memcpy_s") == Kind::Access);
    EXPECT_TRUE(t.KindOf("memset") == Kind::Access);
    EXPECT_TRUE(t.KindOf("VOS_MemAlloc") == Kind::Alloc);
    EXPECT_TRUE(t.KindOf("calloc") == Kind::Alloc);
    EXPECT_TRUE(t.KindOf("VOS_MemFree") == Kind::Free);
    EXPECT_TRUE(t.KindOf("LOG_Print") == Kind::Pure);
    EXPECT_TRUE(t.KindOf("LOG_Warn") == Kind::Pure);    // ^LOG_
    EXPECT_TRUE(t.KindOf("DIAG_Count") == Kind::Pure);  // ^DIAG_
    EXPECT_TRUE(t.KindOf("Msg_Trace") == Kind::Pure);   // _Trace$
    EXPECT_TRUE(t.KindOf("Msg_TraceX") == Kind::None);
    EXPECT_TRUE(t.KindOf("strcpy_s") == Kind::None);  // 其余 securec 函数待 I8（S7d-2）
    EXPECT_TRUE(t.KindOf("Drv_ReadReg") == Kind::None);
    EXPECT_EQ(KindName(Kind::Access).str(), "access");
    EXPECT_EQ(KindName(Kind::None).str(), "");
}

TEST(Models, SecurecConditions)
{
    // F-03：目的写 destMax 字节；源只在 count <= destMax 时读 count 字节
    const AccessModel *m = ModelTable::Default().Access("memcpy_s");
    ASSERT_TRUE(m != nullptr);
    EXPECT_EQ(m->destMax, 1);
    ASSERT_TRUE(m->conds.size() == 2);
    EXPECT_EQ(m->conds[0].base, 0);
    EXPECT_EQ(m->conds[0].bytes, "p1");
    EXPECT_EQ(m->conds[0].kind, "W");
    EXPECT_EQ(m->conds[1].base, 2);
    EXPECT_EQ(m->conds[1].bytes, "p3");
    EXPECT_EQ(m->conds[1].kind, "R");
    EXPECT_EQ(m->conds[1].when, "p3 <= p1");
    // F-05：libc 没有 destMax 规则
    EXPECT_EQ(ModelTable::Default().Access("memcpy")->destMax, -1);
    EXPECT_EQ(ModelTable::Default().Alloc("calloc")->size, "p0*p1");
}
