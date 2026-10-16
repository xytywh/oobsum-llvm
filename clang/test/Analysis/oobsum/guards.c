// 条件边与守卫（oobsum/specs/SPEC-0016）：Dump=state 的 guards 与 safe 行、摘要里 Req 的 guard、调用点的 guards。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+state %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;
UINT32 Leaf(UINT32 ulIdx);
UINT8 *Buf_Get(void);

// if (n == 0) return → 守卫 p1 >= 1；守卫下 n − 1 不回绕（LV2-09 Hist_GetPrevSafe）
UINT8 Prev(const UINT8 *pucHist, UINT32 ulPos)
{
    if (ulPos == 0) {
        return 0;
    }
    return pucHist[ulPos - 1];
}
// CHECK: [oobsum:state] fn Prev b{{[0-9]+}}: pucHist=*p0+0 ulPos=p1 guards: p1 >= 1
// CHECK: [oobsum:summary] {"calls":[],"effs":[{"bound":"data","kind":"ret_int","range":["0","255"]}],"fn":"Prev",
// CHECK-SAME: "end":["p1","p1"],"guard":["p1 >= 1"]
// CHECK-SAME: "start":"p1 - 1"

// 局部数组的初始化写当场证明安全（不生成 Req）；if (i >= 32) return → 守卫 p1 <= 31，调用点带守卫
void Set(UINT32 ulPortId)
{
    UINT32 aulMode[32] = {0};
    if (ulPortId >= 32) {
        return;
    }
    aulMode[ulPortId] = Leaf(ulPortId);
}
// CHECK: [oobsum:state] fn Set safe guards.c:26 W end 4 <= have 128
// CHECK: [oobsum:state] fn Set b{{[0-9]+}}: ulPortId=p0 {{.*}}guards: p0 <= 31
// CHECK: [oobsum:state] fn Set safe guards.c:30 W end 4*p0 + 4 <= have 128
// CHECK: [oobsum:summary] {"calls":[{"args":["p0"],"callee":"Leaf",{{("callee_key":"[^"]*",)?}}"guards":[{"bound":"check","cond":"p0 <= 31"}]
// CHECK-SAME: "fn":"Set",
// CHECK-SAME: "reqs":[]

// 从未知内存读到的下标：if (k < 16) 收窄成 [0, 15]（check）；off + len 可能回绕 → 不当守卫
void Idx(UINT8 *pucBuf, UINT32 ulOff, UINT32 ulLen)
{
    UINT8 *pucSrc = Buf_Get();
    UINT32 k = pucSrc[0];
    if (k < 16) {
        pucBuf[k] = 0;
    }
    if (ulOff + ulLen <= 64) {
        pucBuf[ulOff] = 1;
    }
}
// CHECK: [oobsum:state] fn Idx b{{[0-9]+}}: {{.*}}k=[0, 15]:check
// 汇合后：[0, 15]:check 与 [16, 255]:data（假边只收窄了下界）→ [0, 255]:data
// CHECK: [oobsum:state] fn Idx b{{[0-9]+}}: {{.*}}k=[0, 255]:data
// CHECK: [oobsum:summary] {"calls":[{"args":[],"callee":"Buf_Get"
// CHECK-SAME: "fn":"Idx",
// CHECK-SAME: "reqs":[{"access":{"at":"guards.c:45","end":"16"
// CHECK-SAME: "bound":"check"
// CHECK-SAME: "guard":[]
// CHECK-SAME: {"access":{"at":"guards.c:48","end":"p1 + 1"
// CHECK-SAME: "guard":[]
