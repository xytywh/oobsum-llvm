// 引擎接进 checker（oobsum/specs/SPEC-0021）：Phase=inline 时单个 TU 内从摘要推到判定，按标签报出，SAFE 只进日志。
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=apply+stats %s 2>&1 | FileCheck %s
// RUN: cat %t/decisions/*.jsonl | FileCheck %s --check-prefix=LOG
// Phase=export 不跑引擎、不报（SPEC-0028；没设 WorkDir 时只提示一行）
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Phase=export %s 2>&1 | FileCheck %s --check-prefix=EXPORT --allow-empty

typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Ext_Send(UINT8 *pucBuf, UINT32 ulLen);

static void Leaf(UINT8 *pucBuf, UINT32 ulIdx)
{
    pucBuf[ulIdx] = 0;
}

static void Mid(UINT8 *pucBuf, UINT32 ulIdx)
{
    Leaf(pucBuf, ulIdx);
}

static void Up(UINT8 *pucBuf, UINT32 ulIdx)
{
    Mid(pucBuf, ulIdx);
}

void Top_TP(void)
{
    UINT8 aucBuf[16];
    Up(aucBuf, 16);
}

void Top_TN(void)
{
    UINT8 aucBuf[16];
    Up(aucBuf, 15);
}

void Entry(UINT8 *pucIn)
{
    Ext_Send(pucIn, 4);
}

// Dump=apply：每个调用点上被调者每条 Req 的结局
// CHECK-DAG: [oobsum:apply] Mid @ engine.c:23 Leaf.engine.c:18 → up
// CHECK-DAG: [oobsum:apply] Top_TP @ engine.c:34 Up.engine.c:18 → L1
// CHECK-DAG: [oobsum:apply] Top_TN @ engine.c:40 Up.engine.c:18 → safe (within)

// 判定 3 条（DEFINITE、SAFE、SUSPECT），报出 2 条（stats 先于告警打印）
// CHECK: [oobsum:stats] tu={{[0-9a-f]+}} decisions=3 emitted=2
// 汇合点（Top_TP 里那次调用）报 DEFINITE；总链 note + 链上每一跳一条 note（F-33，SPEC-0078）；TN 不报
// CHECK: engine.c:34:1: warning: [OOB-DEFINITE] Definite out-of-bound write through a 3-level call chain: up to byte 17 of 'aucBuf' (16 bytes) is accessed
// CHECK: engine.c:34:1: note: Call chain: Top_TP (engine.c:34) -> Up (engine.c:28) -> Mid (engine.c:23) -> Leaf (engine.c:18)
// CHECK: engine.c:34:1: note: Top_TP calls Up here
// CHECK: engine.c:28:1: note: Up calls Mid here
// CHECK: engine.c:23:1: note: Mid calls Leaf here
// CHECK: engine.c:18:1: note: Leaf accesses the object here
// CHECK-NOT: engine.c:40:{{.*}}warning
// 指针流进本 TU 没有函数体的被调者（D-27 not_in_tu）
// CHECK: engine.c:45:1: warning: [OOB-SUSPECT:not_in_tu] Cannot prove this access is within bounds: a pointer is passed to 'Ext_Send', whose body is not in this translation unit

// 判定日志：报了的、没报的都在（SAFE 也在）
// LOG-DAG: "emitted":true,{{.*}}"label":"definite"
// LOG-DAG: "emitted":false,{{.*}}"fn":"F:Top_TN",{{.*}}"label":"safe",{{.*}}"not_emitted_because":"safe",{{.*}}"reason":"within"
// LOG-DAG: "emitted":true,{{.*}}"label":"suspect",{{.*}}"reason":"not_in_tu"

// EXPORT: [oobsum] warning: Phase=export needs WorkDir; nothing written
// EXPORT-NOT: [OOB-
