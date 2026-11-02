// rb.enhance.core.OobSummary 已注册、选项齐全（oobsum/specs/SPEC-0001 §8）。
// RUN: %clang_cc1 -analyzer-checker-help | FileCheck %s --check-prefix=HELP
// RUN: %clang_cc1 -analyzer-checker-option-help | FileCheck %s --check-prefix=OPT

// HELP: rb.enhance.core.OobSummary

// OPT-DAG: rb.enhance.core.OobSummary:Phase
// OPT-DAG: rb.enhance.core.OobSummary:WorkDir
// OPT-DAG: rb.enhance.core.OobSummary:Policy
// OPT-DAG: rb.enhance.core.OobSummary:InlineDepth
// OPT-DAG: rb.enhance.core.OobSummary:ReportSafe
// OPT-DAG: rb.enhance.core.OobSummary:SuspectReasons
// OPT-DAG: rb.enhance.core.OobSummary:Dump
// OPT-DAG: rb.enhance.core.OobSummary:DumpFilter

void nothing(void) {}
