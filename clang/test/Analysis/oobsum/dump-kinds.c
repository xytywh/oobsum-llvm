// Dump 的多个种类用 '+' 连接（-analyzer-config 把逗号当成键值对分隔，不能用逗号）。
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=options+stats \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:SuspectReasons=entry_unproven+type_range_only %s 2>&1 | FileCheck %s

void oobsum_debug_emit(const char *label, const char *reason, int depth);

void f(void)
{
    oobsum_debug_emit("suspect", "entry_unproven", 0);
    oobsum_debug_emit("suspect", "type_range_only", 0);
    oobsum_debug_emit("suspect", "unknown_callee", 0);
}

// 调试行在 TU 回调里打印，告警在整个 TU 分析完后才发出，所以 stats 在前
// CHECK: [oobsum:options] Phase=inline {{.*}}SuspectReasons="entry_unproven+type_range_only" Dump="options+stats"
// CHECK: [oobsum:stats] tu={{[0-9a-f]+}} decisions=3 emitted=2
// CHECK-NOT: unknown_callee
// CHECK: warning: [OOB-SUSPECT:entry_unproven]
// CHECK-NOT: unknown_callee
// CHECK: warning: [OOB-SUSPECT:type_range_only]
// CHECK-NOT: unknown_callee
