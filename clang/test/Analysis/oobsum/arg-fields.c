// 局部结构体的字段值带进调用点（oobsum/specs/SPEC-0026）：LV5-01 的写法，链顶填好上下文再传下去。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;

typedef struct {
    UINT8 *pucBuf;
    UINT32 ulBufLen;
    UINT32 ulUsed;
} CTX_S;

static void Put(CTX_S *pstCtx)
{
    pstCtx->pucBuf[pstCtx->ulBufLen - 1] = 0;
}

void Encode(void)
{
    UINT8 aucBuf[128];
    CTX_S stCtx;
    stCtx.pucBuf = aucBuf;
    stCtx.ulBufLen = 256;
    stCtx.ulUsed = 0;
    Put(&stCtx);
}
// 三个字段都在（连续赋值不会互相清掉）
// CHECK: [oobsum:summary] {"calls":[{"args":[{"fields":{"pucBuf":{"have":"128","obj":"local:aucBuf"},"ulBufLen":"256","ulUsed":"0"},"have":"12","obj":"local:stCtx"}],"callee":"Put"
// CHECK-SAME: "fn":"Encode"
