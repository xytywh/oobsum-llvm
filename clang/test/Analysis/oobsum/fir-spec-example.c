// 06- §5.2 的 FIR 例子（oobsum/specs/SPEC-0008 §5）：Ue_InitOne 的 pstTbl[ulIdx].ulId = 0 降成
//   { "op": "store", "addr": ["padd", ["l", 0], ["bin", "mul", ["l", 1], ["c", 16]]], "size": 4, "val": ["c", 0] }
// 成员 ulId 偏移 0 所以没有第二层 padd；不是下标访问本身（是下标元素的成员），所以没有 sub。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;

typedef struct {
    UINT32 ulId;
    UINT8 aucData[12];
} UE_TBL_S;

UINT32 g_ulUeNum;

void Ue_InitOne(UE_TBL_S *pstTbl, UINT32 ulIdx)
{
    pstTbl[ulIdx].ulId = 0;
}

// CHECK: [oobsum:fir] record #0 UE_TBL_S size=16 { ulId@0: u32; aucData@4: u8[12] }
// CHECK-NEXT: [oobsum:fir] global G:g_ulUeNum: u32 def @fir-spec-example.c:15:8
// CHECK-NEXT: [oobsum:fir] fn F:Ue_InitOne ext -> void @fir-spec-example.c:17:6 [body_pending]
// CHECK-NEXT: [oobsum:fir]   local l0 pstTbl param0: rec#0*
// CHECK-NEXT: [oobsum:fir]   local l1 ulIdx param1: u32
// CHECK-NEXT: [oobsum:fir]   b2 entry:
// CHECK-NEXT: [oobsum:fir]     -> b1
// CHECK-NEXT: [oobsum:fir]   b1:
// CHECK-NEXT: [oobsum:fir]     store4 [padd(l0, l1 * 16)] = 0  @19:5
// CHECK-NEXT: [oobsum:fir]     -> b0
// CHECK-NEXT: [oobsum:fir]   b0 exit:
// CHECK-NOT: error
