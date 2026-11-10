/*
 * Author : gehui
 * Date   : 2026-11-10
 * Desc   : Allocation-note diagnostics for rb.enhance.core.OobSummary (SPEC-0132).
 *          Multiple visible files with the same basename must not be guessed.
 *
 * Run (single file):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/oobsum-llvm/clang/test/Analysis/oobsum/Inputs/allocation-notes/root.c -checker=rb.enhance.core.OobSummary -extra_args="-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 * Issue  : DTSXXXX / SPEC-0132
 * Cases:
 *   TP1 - Heap overflow stays DEFINITE, independent of the allocation note.
 *   TN1 - Unique basename retains the correct allocation note.
 *   FP1 - Ambiguous basename omits an unproven source note; no new OOB verdict.
 *   FN1 - Missing source omits the note, retaining the original OOB warning.
 *   B1  - A complete source path remains exact even with duplicate basenames.
 *   R1  - Repeated report and Dump on/off preserve warnings and judgments.
 */
void *malloc(unsigned int size);

static void Write(char *p)
{
    p[2] = 0;
}

#include "a/common.h"
/* 同一真实路径的另一种拼写，不能被算作第二个候选。 */
#include "a/../a/common.h"
#if AMBIGUOUS
#include "b/common.h"
#endif

void Root(void)
{
    AllocAndWrite();
}
