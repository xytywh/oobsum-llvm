// 经形参访问成员数组：成员边界与外层对象边界都查（oobsum/specs/SPEC-0022）。LV7-03 的写法：结构体按指针套在更短的缓冲区上。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary\
// RUN:   %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;

typedef struct {
    UINT16 usType;
    UINT16 usLen;
    UINT8 aucData[12];
} MSG_S;

void Msg_SetData(MSG_S *pstMsg, UINT32 ulIdx)
{
    pstMsg->aucData[ulIdx] = 1;
}
// 外层对象：4 + ulIdx + 1 <= ext(pstMsg)；成员：ulIdx + 1 <= 12
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Msg_SetData",
// CHECK-SAME: "end":["p1 + 5","p1 + 5"]
// CHECK-SAME: "have":"ext(p0)","id":"R1","kind":"W","obj":"*p0"
// CHECK-SAME: "end":["p1 + 1","p1 + 1"]
// CHECK-SAME: "have":"12","id":"R2","kind":"W","obj":"p0->aucData"
