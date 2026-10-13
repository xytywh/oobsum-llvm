/*
 * Author : g00586360
 * Date   : 2026-10-13
 * Desc   : Test cases for rb.enhance.core.OobSummaryProbe — integration probe for CSA Summary 2.0.
 *          Read-only: prints [OSP] lines, writes per-TU JSON when WorkDir is set, and emits one
 *          probe report per TU only when EmitProbeReport=true. It does not detect any defect.
 *
 * Run (folder):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen \
 *     -exec_mode=onlySa \
 *     -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
 *     -analysis_folder=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryProbeChecker/src/ \
 *     -checker=rb.enhance.core.OobSummaryProbe \
 *     "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-config rb.enhance.core.OobSummaryProbe:EmitProbeReport=true -analyzer-config rb.enhance.core.OobSummaryProbe:WorkDir=/tmp/osp -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *
 * Issue  : DTSXXXX / https:xxxx
 *
 * Cases:
 *   P1 - cross-TU direct call (Probe_FillBuf defined in _b.c)   (expect: [OSP] func ... direct=2)
 *   P2 - indirect call through a function pointer               (expect: indirect=1)
 *   P3 - loop                                                   (expect: loops>=1)
 *   P4 - static inline from header                              (expect: key F@<tu>:Probe_Min)
 *   P5 - SCC order: Probe_Leaf before Probe_Mid before Probe_Top (expect: [OSP] scc lines in that order)
 *   R1 - one probe report on the first function of this TU     (expect: 1 report, "OobSummary probe:")
 */
#include "probe_common.h"

static void Probe_Leaf(ProbeCtx *pstCtx, UINT32 ulIdx)
{
    /* P3：循环 */
    UINT32 i;
    for (i = 0; i < ulIdx; i++) {
        pstCtx->aucBuf[i % 16] = (UINT8)i;
    }
}

static void Probe_Mid(ProbeCtx *pstCtx, UINT32 ulIdx)
{
    /* P4：头文件 static inline */
    Probe_Leaf(pstCtx, Probe_Min(ulIdx, 16));
}

void Probe_Top(ProbeCtx *pstCtx, ProbeHandler pfnHook)
{
    /* P1：跨 TU 直接调用；P2：函数指针 */
    Probe_Mid(pstCtx, pstCtx->ulLen);
    Probe_FillBuf(pstCtx->aucBuf, sizeof(pstCtx->aucBuf));
    (void)Probe_Depth(3);
    if (pfnHook != (ProbeHandler)0) {
        pfnHook(pstCtx);
    }
}
