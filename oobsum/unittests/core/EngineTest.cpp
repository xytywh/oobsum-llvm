//===-- EngineTest.cpp - Tests for core/Engine ------------------*- C++ -*-===//
//
// Phase=inline 的引擎（SPEC-0021）：真实降级出来的 FIR（data/engine-fir.json，由 data/gen_engine_fir.py 从 data/engine.c 生成）
// 上，判定的标签、原因、位置、文本。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "OobSummary/core/DecisionLog.h"
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
    // 摘要表按函数键（SPEC-0027）：按名字找
    auto byName = [&](const std::string &name) -> const summary::Summary & {
        for (const auto &entry : r.summaries) {
            if (entry.second.fn == name) {
                return entry.second;
            }
        }
        return r.summaries.begin()->second;
    };
    // ret_new 逐层推出（SPEC-0024）：Wrap_Alloc 的 eff_depth 1、Tbl_Alloc 2；Heap_TP 的判定 eff_depth 2
    ASSERT_TRUE(byName("Wrap_Alloc").effs.size() == 1);
    EXPECT_EQ(byName("Wrap_Alloc").effs[0].kind, "ret_new");
    EXPECT_EQ(byName("Wrap_Alloc").effs[0].size.getValueOr("-"), "p0");
    EXPECT_EQ(byName("Wrap_Alloc").effs[0].effDepth.getValueOr(0), 1);
    ASSERT_TRUE(byName("Tbl_Alloc").effs.size() == 1);
    EXPECT_EQ(byName("Tbl_Alloc").effs[0].size.getValueOr("-"), "8");
    EXPECT_EQ(byName("Tbl_Alloc").effs[0].effDepth.getValueOr(0), 2);
    // 自底向上：被调者先
    auto pos = [&](const std::string &fn) {
        return std::find(r.order.begin(), r.order.end(), byName(fn).key) - r.order.begin();
    };
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

TEST(Engine, SolveAcrossModules)
{
    // 两个 TU 一起求解（SPEC-0029）：A 调 B 里定义的 Fill（写 17 字节到 16 字节的数组）→ 跨模块的 L1；两个同名 static Init 各算各的；
    // B 里的 Send 哪里都没有定义 → unknown_callee（inline 时是 not_in_tu）
    fir::Module a;
    fir::Module b;
    std::string error;
    ASSERT_TRUE(fir::ReadModuleFile(OOBSUM_TEST_DATA_DIR "/solve-a-fir.json", a, error));
    ASSERT_TRUE(fir::ReadModuleFile(OOBSUM_TEST_DATA_DIR "/solve-b-fir.json", b, error));
    engine::InlineResult r = engine::RunSolve({&a, &b});
    std::vector<std::string> got = Decisions(r);
    std::vector<std::string> want = {
        // B 的 static Init 写 pucBuf[100]、64 字节的数组 → L1；A 的同名 Init 写 pucBuf[0] → 安全（按函数键各算各的）
        "definite Top_B solve_b.c:24 | Definite out-of-bound write through a 1-level call chain: up to byte 101 of "
        "'aucBuf' (64 bytes) is accessed",
        // A 调 B 的 Fill：循环写 17 字节到 16 字节的数组 → L2（前 16 次安全），调用链跨文件
        "possible Top_A solve_a.c:16 | Possible out-of-bound write through a 1-level call chain: up to byte 17 of "
        "'aucBuf' (16 bytes) may be accessed; the bound comes from a loop condition [Top_A (solve_a.c:16) -> Fill "
        "(solve_b.c:16)]",
        "safe:within Top_A solve_a.c:15 | Proven within bounds: up to byte 1 of 'aucBuf' (16 bytes)",
        "suspect:unknown_callee Fill solve_b.c:18 | Cannot prove this access is within bounds: a pointer is passed to "
        "'Send', which is not defined in any analyzed translation unit",
    };
    EXPECT_EQ(got.size(), want.size());
    for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        EXPECT_EQ(got[i], want[i]);
    }
    // 判定所在的模块：Top_A 在 A（下标 0）、Fill 在 B（下标 1）
    for (const auto &entry : r.summaries) {
        if (entry.second.fn == "Top_A") {
            EXPECT_EQ(r.moduleOf.at(entry.first), 0u);
        }
        if (entry.second.fn == "Fill") {
            EXPECT_EQ(r.moduleOf.at(entry.first), 1u);
        }
    }
    // 同样的 B 单独 inline：Send 是 not_in_tu
    engine::InlineResult alone = engine::RunInline(b);
    bool notInTu = false;
    for (const Decision &d : alone.decisions) {
        notInTu = notInTu || d.reason == "not_in_tu";
    }
    EXPECT_TRUE(notInTu);
}

TEST(Engine, DecisionJsonRoundTrip)
{
    // solve 写、report 读（SPEC-0029 §2.3）：判定写成 JSON 再读回，字段相同
    fir::Module m;
    std::string error;
    ASSERT_TRUE(fir::ReadModuleFile(OOBSUM_TEST_DATA_DIR "/engine-fir.json", m, error));
    engine::InlineResult r = engine::RunInline(m);
    ASSERT_TRUE(!r.decisions.empty());
    for (const Decision &d : r.decisions) {
        Decision back;
        ASSERT_TRUE(DecisionFromJson(llvm::json::Value(DecisionToJson(d, Disposition())), back, error));
        EXPECT_TRUE(back.label == d.label);
        EXPECT_EQ(back.reason, d.reason);
        EXPECT_EQ(back.site.file + ":" + std::to_string(back.site.line),
                  d.site.file + ":" + std::to_string(d.site.line));
        EXPECT_EQ(back.fn, d.fn);
        EXPECT_EQ(back.endHi, d.endHi);
        EXPECT_EQ(back.msg, d.msg);
        EXPECT_EQ(back.fp, d.fp);
        EXPECT_EQ(back.dupOf, d.dupOf);
        EXPECT_EQ(back.depth, d.depth);
        ASSERT_TRUE(back.chain.size() == d.chain.size());
        for (size_t i = 0; i < d.chain.size(); ++i) {
            EXPECT_EQ(back.chain[i].fn, d.chain[i].fn);
            EXPECT_EQ(back.chain[i].site.line, d.chain[i].site.line);
        }
    }
    // dup_of（SPEC-0036）
    Decision dup = r.decisions[0];
    dup.dupOf = "0123456789abcdef";
    Decision back;
    ASSERT_TRUE(DecisionFromJson(llvm::json::Value(DecisionToJson(dup, Disposition())), back, error));
    EXPECT_EQ(back.dupOf, "0123456789abcdef");
}
