// 选项值不对时报错（而不是悄悄用默认值）。
// RUN: not %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Policy=precise %s 2>&1 | FileCheck %s --check-prefix=POLICY
// RUN: not %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=everything %s 2>&1 | FileCheck %s --check-prefix=DUMP

// POLICY: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:Policy', that expects either 'recall' or 'balanced'
// DUMP: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:Dump'{{.*}}got 'everything'

void nothing(void) {}
