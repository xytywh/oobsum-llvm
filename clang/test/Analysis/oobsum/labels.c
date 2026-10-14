// 标签与报告策略（oobsum/specs/SPEC-0001 §3）：DEFINITE / POSSIBLE / SUSPECT 报，SAFE 只进日志；
// Policy=balanced 不报 SUSPECT；InlineDepth 只过滤 DEFINITE / POSSIBLE；SuspectReasons 选原因；ReportSafe 也报 SAFE。
// 判定来自调试内建函数 oobsum_debug_emit(label, reason, depth)（SPEC-0001 §6.3）。
//
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -verify %s
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Policy=balanced -verify=balanced %s
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:ReportSafe=true -verify=expected,safe %s
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:InlineDepth=0 -verify=expected,depth0 %s
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:SuspectReasons=type_range_only -verify=reasons %s

void oobsum_debug_emit(const char *label, const char *reason, int depth);

void decisions(void)
{
    oobsum_debug_emit("definite", "", 3);              // expected-warning{{[OOB-DEFINITE] debug decision}} balanced-warning{{[OOB-DEFINITE]}} reasons-warning{{[OOB-DEFINITE]}}
    oobsum_debug_emit("possible", "", 3);              // expected-warning{{[OOB-POSSIBLE] debug decision}} balanced-warning{{[OOB-POSSIBLE]}} reasons-warning{{[OOB-POSSIBLE]}}
    oobsum_debug_emit("suspect", "entry_unproven", 0); // expected-warning{{[OOB-SUSPECT:entry_unproven]}}
    oobsum_debug_emit("suspect", "type_range_only", 1); // expected-warning{{[OOB-SUSPECT:type_range_only]}} reasons-warning{{[OOB-SUSPECT:type_range_only]}}
    oobsum_debug_emit("definite", "", 1);              // depth0-warning{{[OOB-DEFINITE]}}
    oobsum_debug_emit("safe", "guard_entails", 3);     // safe-warning{{[OOB-SAFE:guard_entails]}}
}
