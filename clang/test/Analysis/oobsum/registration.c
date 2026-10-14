// rb.enhance.ipa.OobSummary 已注册、选项齐全（oobsum/specs/SPEC-0001 §8）。
// RUN: %clang_cc1 -analyzer-checker-help | FileCheck %s --check-prefix=HELP
// RUN: %clang_cc1 -analyzer-checker-option-help | FileCheck %s --check-prefix=OPT

// HELP: rb.enhance.ipa.OobSummary

// OPT-DAG: rb.enhance.ipa.OobSummary:Phase
// OPT-DAG: rb.enhance.ipa.OobSummary:WorkDir
// OPT-DAG: rb.enhance.ipa.OobSummary:Policy
// OPT-DAG: rb.enhance.ipa.OobSummary:InlineDepth
// OPT-DAG: rb.enhance.ipa.OobSummary:ReportSafe
// OPT-DAG: rb.enhance.ipa.OobSummary:SuspectReasons
// OPT-DAG: rb.enhance.ipa.OobSummary:Dump
// OPT-DAG: rb.enhance.ipa.OobSummary:DumpFilter

void nothing(void) {}
