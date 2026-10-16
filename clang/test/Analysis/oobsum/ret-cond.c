// 校验函数的条件效应（oobsum/specs/SPEC-0025）：?: 的校验 → ret_cond，沿返回值再传一层；调用方 != OK 就返回之后带上守卫。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;

static UINT8 g_aucTbl[16];

static UINT32 IsValid(UINT32 ulIdx)
{
    return (ulIdx <= 16) ? 1 : 0;
}
// CHECK: [oobsum:summary] {"calls":[],"effs":[{"kind":"ret_cond","then":["p0 >= 17"],"when":"ret == 0"},{"kind":"ret_cond","then":["p0 <= 16"],"when":"ret == 1"},{"bound":"const","kind":"ret_int","range":["0","1"]}],"fn":"IsValid"

static UINT32 Check(UINT32 ulIdx)
{
    if (IsValid(ulIdx) == 0) {
        return 1;
    }
    return 0;
}
// 沿返回值再传一层：ret == 0 ⇒ p0 <= 16
// CHECK: [oobsum:summary] {{.*}}"effs":[{"kind":"ret_cond","then":["p0 <= 16"],"when":"ret == 0"}

static void Set(UINT32 ulIdx)
{
    g_aucTbl[ulIdx] = 1;
}

void OnReq(UINT32 ulIdx)
{
    if (Check(ulIdx) != 0) {
        return;
    }
    Set(ulIdx);
}
// 调用方：Check 返回 0 之后，Set 的调用点带守卫 p0 <= 16（校验差了一，Set 需要 p0 <= 15）
// CHECK: [oobsum:summary] {"calls":[{"args":["p0"],"callee":"Check"
// CHECK-SAME: {"args":["p0"],"callee":"Set",{{("callee_key":"[^"]*",)?}}"guards":[{"bound":"check","cond":"p0 <= 16"}]
// CHECK-SAME: "fn":"OnReq"
