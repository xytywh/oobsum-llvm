//===-- EngineTest.cpp - Tests for core/Engine ------------------*- C++ -*-===//
//
// Phase=inline 的引擎（SPEC-0021）：真实降级出来的 FIR（data/engine-fir.json，由 data/gen_engine_fir.py 从 data/engine.c 生成）
// 上，判定的标签、原因、位置、文本。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/Engine.h"
#include "OobSummary/core/FirJson.h"
#include "OobSummary/core/Label.h"

#include "llvm/Support/Path.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace oobsum;

namespace {

std::vector<std::string> Decisions(const engine::InlineResult &r)
{
    std::vector<std::string> out;
    for (const Decision &d : r.decisions) {
        std::string fn = d.fn.substr(d.fn.rfind(':') + 1);
        out.push_back(LabelName(d.label).str() + (d.reason.empty() ? "" : ":" + d.reason) + " " + fn + " " +
                      llvm::sys::path::filename(d.site.file).str() + ":" + std::to_string(d.site.line) + " | " + d.msg);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST(Engine, InlineDecisions)
{
    fir::Module m;
    std::string error;
    ASSERT_TRUE(fir::ReadModuleFile(OOBSUM_TEST_DATA_DIR "/engine-fir.json", m, error));
    engine::InlineResult r = engine::RunInline(m);
    std::vector<std::string> got = Decisions(r);
    std::vector<std::string> want = {
        // 两层封装的分配器（SPEC-0024）：Tbl_Alloc 的返回值是 8 字节的堆对象，Leaf 写下标 8 → L1
        "definite Heap_TP engine.c:76 | Definite out-of-bound write through a 1-level call chain: up to byte 9 of heap "
        "object allocated at engine.c:72 (8 bytes) is accessed",
        // 三层链：TP 越界（L1）、循环界给出的可能越界（L2）、TN 在调用点证明安全
        "definite Top_TP engine.c:25 | Definite out-of-bound write through a 3-level call chain: up to byte 17 of "
        "'aucBuf' (16 bytes) is accessed",
        "possible Top_L2 engine.c:39 | Possible out-of-bound write through a 3-level call chain: up to byte 17 of "
        "'aucBuf' (16 bytes) may be accessed; the bound comes from a loop condition",
        // 本函数当场证明安全（局部数组 4 字节，循环 i < 4）
        "safe:local_entailed Entry engine.c:48 | Proven within bounds: up to byte 4 of 'aucTmp' (4 bytes)",
        "safe:within Top_TN engine.c:31 | Proven within bounds: up to byte 16 of 'aucBuf' (16 bytes)",
        // 入口形参：u8 * 读 4 字节，超出声明类型（1 字节）→ entry_unproven，不是 typed_field（SPEC-0019）
        "suspect:entry_unproven Entry engine.c:48 | Cannot prove the read stays within '*pucIn' (size ext(p0)): up to "
        "byte 4 may be accessed",
        // 指针流进本 TU 没有函数体的被调者
        "suspect:not_in_tu Entry engine.c:50 | Cannot prove this access is within bounds: a pointer is passed to "
        "'Ext_Send', whose body is not in this translation unit",
    };
    EXPECT_EQ(got.size(), want.size());
    for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        EXPECT_EQ(got[i], want[i]);
    }
    // 判定的调用链：汇合点 → … → 访问点（完整路径换回来了）
    // ret_new 逐层推出（SPEC-0024）：Wrap_Alloc 的 eff_depth 1、Tbl_Alloc 2；Heap_TP 的判定 eff_depth 2
    ASSERT_TRUE(r.summaries.at("Wrap_Alloc").effs.size() == 1);
    EXPECT_EQ(r.summaries.at("Wrap_Alloc").effs[0].kind, "ret_new");
    EXPECT_EQ(r.summaries.at("Wrap_Alloc").effs[0].size.getValueOr("-"), "p0");
    EXPECT_EQ(r.summaries.at("Wrap_Alloc").effs[0].effDepth.getValueOr(0), 1);
    ASSERT_TRUE(r.summaries.at("Tbl_Alloc").effs.size() == 1);
    EXPECT_EQ(r.summaries.at("Tbl_Alloc").effs[0].size.getValueOr("-"), "8");
    EXPECT_EQ(r.summaries.at("Tbl_Alloc").effs[0].effDepth.getValueOr(0), 2);
    // 自底向上：被调者先
    auto pos = [&](const std::string &fn) { return std::find(r.order.begin(), r.order.end(), fn) - r.order.begin(); };
    EXPECT_TRUE(pos("Wrap_Alloc") < pos("Tbl_Alloc"));
    EXPECT_TRUE(pos("Tbl_Alloc") < pos("Heap_TP"));
    for (const Decision &d : r.decisions) {
        if (d.label == Label::Definite && d.fn.find("Heap_TP") != std::string::npos) {
            EXPECT_EQ(d.effDepth, 2u);
        }
        if (d.label == Label::Definite && d.fn.find("Top_TP") != std::string::npos) {
            ASSERT_TRUE(d.chain.size() == 4);
            EXPECT_EQ(d.chain[0].fn, "Top_TP");
            EXPECT_EQ(d.chain[3].fn, "Leaf");
            EXPECT_EQ(d.chain[3].site.file, "/x/engine.c");
            EXPECT_EQ(d.chain[3].site.line, 9u);
            EXPECT_EQ(d.depth, 3u);
            EXPECT_EQ(d.origin, "engine");
        }
    }
    // Dump=apply：每个调用点上被调者每条 Req 的结局
    EXPECT_TRUE(std::find(r.applyLines.begin(), r.applyLines.end(),
                          "Top_TN @ engine.c:31 Up.engine.c:9 → safe (within)") != r.applyLines.end());
    EXPECT_TRUE(std::find(r.applyLines.begin(), r.applyLines.end(), "Mid @ engine.c:14 Leaf.engine.c:9 → up") !=
                r.applyLines.end());
}

TEST(Engine, Messages)
{
    // SPEC-0021 §2.3：大小是常数写 N bytes、是符号写 size …；depth 0 不写调用链
    EXPECT_EQ(engine::SuspectMessage("W", 2, "'aucBuf'", "16", "20"),
              "Cannot prove the write through a 2-level call chain stays within 'aucBuf' (16 bytes): up to byte 20 "
              "may be accessed");
    EXPECT_EQ(engine::SuspectMessage("R", 0, "'*p'", "ext(p0)", "4"),
              "Cannot prove the read stays within '*p' (size ext(p0)): up to byte 4 may be accessed");
    EXPECT_EQ(engine::SafeMessage("'g_x'", "64", "8"), "Proven within bounds: up to byte 8 of 'g_x' (64 bytes)");
    EXPECT_EQ(engine::LedgerMessage("pointer_stepping"),
              "Cannot prove this access is within bounds: the pointer is advanced in a loop");
    EXPECT_EQ(engine::LedgerMessage("unanalyzed:iter_limit"),
              "Cannot prove this access is within bounds: the function was not fully analyzed (iter_limit)");
}
