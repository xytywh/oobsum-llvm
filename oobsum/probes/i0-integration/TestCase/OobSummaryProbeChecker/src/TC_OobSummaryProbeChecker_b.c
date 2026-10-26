/*
 * Author : g00586360
 * Date   : 2026-10-13
 * Desc   : Test cases for rb.enhance.core.OobSummaryProbe — second TU (callee side of P1, recursion).
 *
 * Run (folder):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen \
 *     -exec_mode=onlySa \
 *     -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
 *     -analysis_folder=/usr1/BXPlugin/xir/llvm_ast/TestCase/OobSummaryProbeChecker/src/ \
 *     -checker=rb.enhance.core.OobSummaryProbe \
 *     -extra_args="-analyzer-max-loop 3 -analyzer-config ipa=none -analyzer-config rb.enhance.core.OobSummaryProbe:EmitProbeReport=true -analyzer-config rb.enhance.core.OobSummaryProbe:WorkDir=/tmp/osp -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *
 * Issue  : DTSXXXX / https:xxxx
 *
 * Cases:
 *   P1 - callee side of the cross-TU call (external linkage key F:Probe_FillBuf)
 *   P6 - direct recursion                     (expect: [OSP] scc ... cycle F:Probe_Depth)
 *   R1 - one probe report on the first function of this TU (expect: 1 report)
 */
#include "probe_common.h"

void Probe_FillBuf(UINT8 *pucBuf, UINT32 ulLen)
{
    UINT32 i;
    for (i = 0; i < ulLen; i++) {
        pucBuf[i] = 0;
    }
}

UINT32 Probe_Depth(UINT32 ulN)
{
    /* P6：递归 */
    if (ulN == 0) {
        return 0;
    }
    return 1 + Probe_Depth(ulN - 1);
}
