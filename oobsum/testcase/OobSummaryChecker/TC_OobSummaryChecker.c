/*
 * Author : g00586360
 * Date   : 2026-10-14
 * Desc   : Test cases for rb.enhance.ipa.OobSummary (skeleton, SPEC-0001) --
 *          verifies registration, options, verdict labels, report policy and the decision log in bxen.
 *          The engine (FIR, summaries, call-site decisions) is not wired in yet; decisions here come from the
 *          debug intrinsic oobsum_debug_emit(label, reason, depth), which only test sources declare.
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
 *   Total with default options: 3 reports.
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
