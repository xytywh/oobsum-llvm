// 循环（oobsum/specs/SPEC-0017）：不动点上的状态（Dump=state 只打最后记录的那一遍，循环头带 (head)）、循环界的标签 loop、
// 常数界的循环当场证明安全、指针步进记账 pointer_stepping。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+state %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Fill(UINT8 *pucBuf, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        pucBuf[i] = 0;
    }
}
// CHECK: [oobsum:state] fn Fill b{{[0-9]+}}: pucBuf=*p0+0 ulNum=p1 i=[0, +inf]:loop (head)
// CHECK: [oobsum:state] fn Fill b{{[0-9]+}}: pucBuf=*p0+0 ulNum=p1 i=[0, p1 - 1]:loop guards: p1 >= 1
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Fill",
// CHECK-SAME: "bound":"loop"
// CHECK-SAME: "end":["1","p1"],"guard":["p1 >= 1"]

void Words(UINT8 *pucMap, UINT32 ulBits)
{
    UINT32 *pulMap = (UINT32 *)(void *)pucMap;
    UINT32 i;
    for (i = 0; i < (ulBits + 31) / 32; i++) {
        pulMap[i] = 0;
    }
}
// (ulBits + 31) / 32 可能回绕，但它是"大的一边"（SPEC-0017 §2.4）
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Words",
// CHECK-SAME: "end":["4","4*div(p1 + 31, 32)"],"guard":["div(p1 + 31, 32) >= 1"]

UINT32 Sum(void)
{
    UINT8 aucTmp[4];
    UINT32 i;
    UINT32 ulSum = 0;
    for (i = 0; i < 4; i++) {
        aucTmp[i] = (UINT8)i;
    }
    for (i = 0; i < 4; i++) {
        ulSum += aucTmp[i];
    }
    return ulSum;
}
// CHECK: [oobsum:state] fn Sum safe loops.c:40 W end 4 <= have 4
// CHECK: [oobsum:state] fn Sum safe loops.c:43 R end 4 <= have 4
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Sum",
// CHECK-SAME: "reqs":[]

void Copy(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt)
{
    while (ulCnt-- > 0) {
        *pucOut++ = *pucIn++;
    }
}
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Copy",
// CHECK-SAME: "opens":[{"at":"loops.c:55","reason":"pointer_stepping"}]
// CHECK-SAME: "reqs":[]
