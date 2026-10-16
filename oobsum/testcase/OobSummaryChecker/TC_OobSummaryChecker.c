/*
 * Author : g00586360
 * Date   : 2026-10-14
 * Desc   : Test cases for rb.enhance.ipa.OobSummary --
 *          verifies registration, options, verdict labels, report policy and the decision log in bxen (SPEC-0001,
 *          decisions from the debug intrinsic oobsum_debug_emit(label, reason, depth), which only test sources declare),
 *          and the engine in the default Phase=inline on a real 3-level call chain (SPEC-0021).
 *
 * Run (single file):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen \
 *     -exec_mode=onlySa \
 *     -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
 *     -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryChecker/TC_OobSummaryChecker.c \
 *     -checker=rb.enhance.ipa.OobSummary \
 *     "-extra_args=-analyzer-max-loop 3 -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=/tmp/oobsum-tc -analyzer-config rb.enhance.ipa.OobSummary:Dump=options+stats -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *   # rb.enhance.ipa 规则：不加 ipa=none；它自己不走 CSA 的 inline，只挂 TU 级回调
 *   # 判定日志：/tmp/oobsum-tc/decisions/<tu-id>.jsonl（报了的、没报的都在）
 *   # 多个调试种类用 '+' 连接（-analyzer-config 把逗号当分隔符）
 *
 * Issue  : DTSXXXX / https:xxxx
 *
 * Cases:
 *   TP1 - definite decision, depth 3            (expect: report, key substr: "[OOB-DEFINITE]")
 *   TP2 - possible decision, depth 3            (expect: report, key substr: "[OOB-POSSIBLE]")
 *   TP3 - suspect decision, entry_unproven      (expect: report, key substr: "[OOB-SUSPECT:entry_unproven]")
 *   TN1 - proven safe decision (guard_false)    (expect: no report; decision log: "label":"safe","emitted":false)
 *   B1  - definite decision at depth 2          (expect: no report with default InlineDepth=2; logged as inline_depth)
 *   E1  - engine, 3-level chain, index 16 into a 16-byte array
 *                                               (expect: report at the call in Tc_Engine_TP, key substr:
 *                                                "[OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain")
 *   E2  - engine, same chain with index 15      (expect: no report; decision log: "label":"safe","reason":"within")
 *   Total with default options: 4 reports, 7 decisions (Dump=stats: "decisions=7 emitted=4").
 */

void oobsum_debug_emit(const char *label, const char *reason, int depth);

/* ---- TP1/TP2/TP3: labelled decisions that must be reported (recall policy) ---- */
void Tp_Labels(void)
{
    oobsum_debug_emit("definite", "", 3);
    oobsum_debug_emit("possible", "", 3);
    oobsum_debug_emit("suspect", "entry_unproven", 0);
}

/* ---- TN1: proven safe -> decision log only ---- */
void Tn_Safe(void)
{
    oobsum_debug_emit("safe", "guard_false", 3);
}

/* ---- B1: shallow definite (depth <= InlineDepth) is left to existing rules, still logged ---- */
void Boundary_Shallow(void)
{
    oobsum_debug_emit("definite", "", 2);
}

/* ---- E1/E2: the engine (Phase=inline, SPEC-0021) on a real 3-level call chain ---- */
typedef unsigned char TC_UINT8;
typedef unsigned int TC_UINT32;

static void Tc_Leaf(TC_UINT8 *pucBuf, TC_UINT32 ulIdx)
{
    pucBuf[ulIdx] = 0;
}

static void Tc_Mid(TC_UINT8 *pucBuf, TC_UINT32 ulIdx)
{
    Tc_Leaf(pucBuf, ulIdx);
}

static void Tc_Up(TC_UINT8 *pucBuf, TC_UINT32 ulIdx)
{
    Tc_Mid(pucBuf, ulIdx);
}

void Tc_Engine_TP(void)
{
    TC_UINT8 aucBuf[16];
    Tc_Up(aucBuf, 16); /* E1：越界 1 字节，三层 → DEFINITE */
}

void Tc_Engine_TN(void)
{
    TC_UINT8 aucBuf[16];
    Tc_Up(aucBuf, 15); /* E2：在调用点证明安全，只进判定日志 */
}
