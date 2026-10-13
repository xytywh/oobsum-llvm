/*
 * Author : g00586360
 * Date   : 2026-10-13
 * Desc   : Shared declarations for rb.enhance.core.OobSummaryProbe test cases (integration probe, read-only).
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
 * Cases  : see TC_OobSummaryProbeChecker_a.c / TC_OobSummaryProbeChecker_b.c
 */
#ifndef PROBE_COMMON_H
#define PROBE_COMMON_H

typedef unsigned int UINT32;
typedef unsigned char UINT8;

typedef struct ProbeCtxTag {
    UINT32 ulLen;
    UINT8 aucBuf[16];
} ProbeCtx;

typedef void (*ProbeHandler)(ProbeCtx *pstCtx);

/* 定义在 _b.c：用来看跨 TU 的被调者在本 TU 里是不是"只有声明" */
void Probe_FillBuf(UINT8 *pucBuf, UINT32 ulLen);
UINT32 Probe_Depth(UINT32 ulN);

/* 头文件里的 static inline：每个 TU 一份，函数键应带 TU 前缀 */
static inline UINT32 Probe_Min(UINT32 a, UINT32 b)
{
    return (a < b) ? a : b;
}

#endif
