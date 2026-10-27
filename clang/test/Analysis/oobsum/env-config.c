// oobsum/specs/SPEC-0120：环境变量 OOBSUM_CONFIG 传选项（公司 clang_check 按 cdb 每个文件起一个 bxen 进程，不改它就靠这个传）。
// 优先于 -analyzer-config；Dump=options 显示原文；键名不是本规则的选项、没有 '='、取值不合法 → 与命令行一样报选项错误。
//
// RUN: rm -rf %t && mkdir -p %t
// RUN: env OOBSUM_CONFIG="WorkDir=%t/w Dump=options+stats" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ENV
// RUN: ls %t/w/decisions/*.jsonl | count 1
// 环境变量优先于 -analyzer-config；分号也能分隔
// RUN: env OOBSUM_CONFIG="Phase=export;WorkDir=%t/x Dump=stats" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Phase=inline %s 2>&1 | FileCheck %s --check-prefix=OVR
// RUN: ls %t/x/fir/*.json | count 1
// 写错了：报选项错误
// RUN: not env OOBSUM_CONFIG="NoSuch=1" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BADKEY
// RUN: not env OOBSUM_CONFIG="WorkDir=%t/y export" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOEQ
// RUN: not env OOBSUM_CONFIG="Phase=bogus" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BADPHASE
// RUN: not env OOBSUM_CONFIG="MaxBlocks=abc ReportSafe=yes" %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BADTYPE
// 没设环境变量：与原来相同
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -analyzer-config rb.enhance.ipa.OobSummary:Dump=options %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOENV

// ENV: [oobsum:options] Phase=inline WorkDir="{{.*}}/w" {{.*}} OOBSUM_CONFIG="WorkDir={{.*}}/w Dump=options+stats"
// ENV: [oobsum:stats] tu={{[0-9a-f]+}} decisions={{[0-9]+}} emitted=1 log={{.*}}/w/decisions/
// ENV: warning: [OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain

// OVR-NOT: warning: [OOB-
// OVR: [oobsum:stats] export tu=
// OVR-NOT: warning: [OOB-

// BADKEY: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:OOBSUM_CONFIG', that expects Key=Value pairs whose keys are options of this checker; got 'NoSuch'
// NOEQ: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:OOBSUM_CONFIG', that expects space-separated Key=Value pairs; got 'export'
// BADPHASE: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:Phase', that expects one of 'inline', 'export', 'solve', 'report'
// BADTYPE-DAG: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:MaxBlocks', that expects an integer (from OOBSUM_CONFIG)
// BADTYPE-DAG: error: invalid input for checker option 'rb.enhance.ipa.OobSummary:ReportSafe', that expects either 'true' or 'false' (from OOBSUM_CONFIG)

// NOENV: [oobsum:options] Phase=inline
// NOENV-NOT: OOBSUM_CONFIG=

typedef unsigned char UINT8;
typedef unsigned int UINT32;

static void Leaf(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Mid(UINT8 *p, UINT32 i) { Leaf(p, i); }
static void Up(UINT8 *p, UINT32 i) { Mid(p, i); }

void Top_TP(void)
{
    UINT8 a[16];
    Up(a, 16); /* TP：越界 1 字节，三层 → DEFINITE */
}
