// Dump=summary / Dump=state（oobsum/specs/SPEC-0015 §2.3）：每个函数的局部事实写成摘要；每个块入口的状态。
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary+state \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t %s 2>&1 | FileCheck %s
// RUN: cat %t/dump/*/summary/Top.json | FileCheck %s --check-prefix=FILE

typedef unsigned char UINT8;
typedef unsigned int UINT32;
void LOG_Print(UINT32 ulLevel, const char *pszFmt, ...);

static void Leaf(UINT8 *pucBuf, UINT32 ulIdx)
{
    pucBuf[ulIdx - 1] = 0;
}

void Top(UINT32 ulIdx)
{
    UINT8 aucBuf[8];
    Leaf(aucBuf, ulIdx);
    LOG_Print(1, "idx %u", ulIdx);
}

// 被调者：depth 0 的 Req（无符号减法回绕）
// CHECK: [oobsum:summary] {"calls":[],"effs":[],"fn":"Leaf",
// CHECK-SAME: "params":[{"name":"pucBuf","size":1,"type":"u8*"},{"name":"ulIdx","type":"u32"}]
// CHECK-SAME: "reqs":[{"access":{"at":"summary-dump.c:14","end":"wrap32(p1 - 1) + 1"
// CHECK-SAME: "have":"ext(p0)"
// CHECK-SAME: "kind":"W","obj":"*p0"
// 调用方：指针实参 {obj, have}、整数实参、纯函数模型的指针实参为 null
// CHECK: [oobsum:state] fn Top b{{[0-9]+}}: ulIdx=p0
// CHECK: [oobsum:summary] {{.*}}"calls":[{"args":[{"have":"8","obj":"local:aucBuf"},"p0"],"callee":"Leaf"
// CHECK-SAME: {"args":["1",null,"p0"],"callee":"LOG_Print",{{("callee_key":"[^"]*",)?}}"model":"pure"
// CHECK-SAME: "fn":"Top"

// FILE: "fn": "Top"
