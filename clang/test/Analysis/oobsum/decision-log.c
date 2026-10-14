// 判定日志（oobsum/specs/SPEC-0001 §5）：每条判定一行 JSON，报了的、没报的都在，没报的写明原因。
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=stats -verify %s 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: cat %t/decisions/*.jsonl | FileCheck %s

void oobsum_debug_emit(const char *label, const char *reason, int depth);

void f(void)
{
    oobsum_debug_emit("definite", "", 3);          // expected-warning{{[OOB-DEFINITE]}}
    oobsum_debug_emit("safe", "guard_false", 3);
}

// STATS: [oobsum:stats] tu={{[0-9a-f]+}} decisions=2 emitted=1 log={{.*}}.jsonl

// CHECK: "count":2
// CHECK-SAME: "schema":"oobsum-decisions/1"
// CHECK: "emitted":true,{{.*}}"label":"definite",{{.*}}"origin":"debug-intrinsic"
// CHECK: "emitted":false,{{.*}}"label":"safe",{{.*}}"not_emitted_because":"safe",{{.*}}"reason":"guard_false"
