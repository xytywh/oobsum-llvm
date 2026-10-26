// Phase=export（oobsum/specs/SPEC-0028）：写 WorkDir/fir/<tu>.json，不求解、不报（连调试内建函数也不处理）。
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Phase=export -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t/w \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=stats -verify %s 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: ls %t/w/fir/*.json | count 1
// RUN: %python %S/../../../../oobsum/tools/json_compact.py %t/w/fir/*.json | FileCheck %s --check-prefix=FIR
// RUN: not ls %t/w/decisions
// inline + WorkDir：判定照常，另写一份 fir/（X-06）
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t/i %s 2>&1 | FileCheck %s --check-prefix=INLINE
// RUN: ls %t/i/fir/*.json %t/i/decisions/*.jsonl | count 2
// expected-no-diagnostics

typedef unsigned char UINT8;
typedef unsigned int UINT32;
void oobsum_debug_emit(const char *label, const char *reason, int depth);

static void Leaf(UINT8 *pucBuf, UINT32 ulIdx)
{
    pucBuf[ulIdx] = 0;
}

void Top(void)
{
    UINT8 aucBuf[16];
    Leaf(aucBuf, 16);
    oobsum_debug_emit("definite", "", 3);
}

// STATS: [oobsum:stats] export tu={{[0-9a-f]+}} functions=2 fir={{.*}}/fir/{{[0-9a-f]+}}.json
// FIR: "funcs":[
// FIR-SAME: "name":"Leaf"
// 引擎那条只有 1 层（InlineDepth=2 交现有规则，不报），报出的是调试内建函数那条
// INLINE: warning: [OOB-DEFINITE] debug decision
