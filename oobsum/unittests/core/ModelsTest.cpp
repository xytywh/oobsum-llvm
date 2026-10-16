//===-- ModelsTest.cpp - Tests for core/Models ------------------*- C++ -*-===//
//
// 模型表的默认值与查找（SPEC-0018 §2.1；06- I-40、I-44）。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Models.h"

#include "llvm/ADT/STLExtras.h"

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
    EXPECT_TRUE(t.KindOf("strcpy_s") == Kind::Access);  // F-07（SPEC-0034）
    EXPECT_TRUE(t.Access("snprintf_s")->conds[0].upto);
    EXPECT_EQ(t.Access("strcat_s")->destMax, 1);
    EXPECT_FALSE(t.Access("memcpy_s")->conds[0].upto);
    EXPECT_TRUE(t.KindOf("wcscpy_s") == Kind::None);  // 宽字符版本以后
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

// ModelFile（SPEC-0033 §2.2）：新增、同名替换（跨类别）、坏条目跳过且保留内置条目
TEST(Models, MergeYamlAddsAndReplaces)
{
    ModelTable t = ModelTable::Default();
    std::vector<std::string> warnings;
    std::string error;
    const char *text = R"y(
allocators:
  - { name: VOS_MemAlloc, result: ret, size: "p1", maybe_null: true }
  - { name: Pool_Get, size: "p0*4" }
access:
  - { name: Drv_Copy, dest_max: p1, conds: [ { base: p0, bytes: "p2", kind: W }, { base: p3, bytes: "p2", kind: R, when: "p2 <= p1", upto: true } ] }
  - { name: memset, conds: [ { base: p0, bytes: "p9x", kind: W } ] }
  - { name: Bad_Kind, conds: [ { base: p0, bytes: "p1", kind: X } ] }
pure:
  names: [ memcpy, Stat_Inc ]
  patterns: [ "^Stat_", "(" ]
frees: [ Pool_Put ]
)y";
    ASSERT_TRUE(MergeModelYaml(text, t, warnings, error));
    EXPECT_EQ(t.Alloc("VOS_MemAlloc")->size, "p1");  // 替换（I5 由用户在文件里改）
    EXPECT_EQ(t.Alloc("Pool_Get")->size, "p0*4");
    const AccessModel *m = t.Access("Drv_Copy");
    ASSERT_TRUE(m != nullptr);
    EXPECT_EQ(m->destMax, 1);
    ASSERT_TRUE(m->conds.size() == 2);
    EXPECT_EQ(m->conds[1].base, 3);
    EXPECT_EQ(m->conds[1].when, "p2 <= p1");
    EXPECT_FALSE(m->conds[0].upto);
    EXPECT_TRUE(m->conds[1].upto);
    EXPECT_TRUE(t.KindOf("memcpy") == Kind::Pure);  // 跨类别替换：一个名字只属于一类
    EXPECT_TRUE(t.Access("memcpy") == nullptr);
    EXPECT_TRUE(t.KindOf("memset") == Kind::Access);  // 坏条目跳过，内置条目保留
    EXPECT_EQ(t.Access("memset")->conds[0].bytes, "p2");
    EXPECT_TRUE(t.KindOf("Bad_Kind") == Kind::None);
    EXPECT_TRUE(t.KindOf("Stat_Add") == Kind::Pure);
    EXPECT_TRUE(t.KindOf("Pool_Put") == Kind::Free);
    EXPECT_TRUE(t.KindOf("memcpy_s") == Kind::Access);
    ASSERT_TRUE(warnings.size() == 3);  // memset 的 bytes、Bad_Kind 的 kind、坏正则
    EXPECT_TRUE(warnings[0].find("memset") != std::string::npos && warnings[0].find("p9x") != std::string::npos);
    EXPECT_TRUE(warnings[1].find("kind 'X'") != std::string::npos);
    EXPECT_TRUE(warnings[2].find("pure pattern '('") != std::string::npos);
}

TEST(Models, MergeYamlWithoutDefaults)
{
    ModelTable t = ModelTable::Default();
    std::vector<std::string> warnings;
    std::string error;
    ASSERT_TRUE(
        MergeModelYaml("defaults: false\nfrees: [ Pool_Put ]\nallocators:\n  - { name: A, result: p1, size: p0 }\n", t,
                       warnings, error));
    EXPECT_TRUE(t.KindOf("memcpy_s") == Kind::None);
    EXPECT_TRUE(t.KindOf("Pool_Put") == Kind::Free);
    EXPECT_TRUE(t.KindOf("oobsum_debug_print_oob") == Kind::Pure);  // 调试内建函数总是纯函数
    EXPECT_TRUE(t.KindOf("A") == Kind::None);                       // 出参式分配器以后（I-42）
    ASSERT_TRUE(warnings.size() == 1);
    EXPECT_TRUE(warnings[0].find("result 'p1'") != std::string::npos);
}

TEST(Models, MergeYamlErrors)
{
    ModelTable t = ModelTable::Default();
    std::vector<std::string> warnings;
    std::string error;
    EXPECT_FALSE(MergeModelYaml("access: [ { name: X, conds: [] ", t, warnings, error));  // 语法错
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(MergeModelYaml("alocators: []\n", t, warnings, error));  // 未知的键（拼错）
    EXPECT_TRUE(error.find("unknown key") != std::string::npos);
    EXPECT_TRUE(t.KindOf("memcpy_s") == Kind::Access);  // 失败时 table 不变
    EXPECT_TRUE(warnings.empty());
}

TEST(Models, Describe)
{
    std::vector<std::string> lines = DescribeModels(ModelTable::Default());
    ASSERT_TRUE(!lines.empty());
    EXPECT_EQ(lines[0], "access memcpy_s dest_max=p1 W p0[p1] R p2[p3] when p3 <= p1");
    EXPECT_TRUE(llvm::is_contained(lines, "access strcpy_s dest_max=p1 W p0[<=p1]"));
    EXPECT_TRUE(llvm::is_contained(lines, "alloc calloc size=p0*p1"));
    EXPECT_TRUE(llvm::is_contained(lines, "free VOS_MemFree"));
    EXPECT_TRUE(llvm::is_contained(lines, "pure name LOG_Print"));
    EXPECT_TRUE(llvm::is_contained(lines, "pure pattern ^LOG_"));
}
