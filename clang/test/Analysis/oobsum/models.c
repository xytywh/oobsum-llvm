// 模型表（oobsum/specs/SPEC-0018）：memcpy_s 的目的 / 源与 destMax 的回绕守卫、memset_s、分配器的返回值、纯函数、函数指针实参。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef unsigned int size_t;
typedef int errno_t;
errno_t memcpy_s(void *dest, size_t destMax, const void *src, size_t count);
errno_t memset_s(void *dest, size_t destMax, int c, size_t count);
void *VOS_MemAlloc(UINT32 ulPid, UINT32 ulPtNo, UINT32 ulSize);
void LOG_Warn(UINT32 ulLevel, const char *pszFmt, ...);
void OM_RegMsg(UINT32 ulMsgId, void (*pfnEntry)(void));

void Put(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulLen)
{
    (void)memcpy_s(pucPkt + ulOff, 128 - ulOff, pucVal, ulLen);
}
// destMax = 128 − ulOff 可能回绕 → 守卫 p1 <= 128，目的末端 128；源只在 ulLen <= 128 − ulOff 时读
// CHECK: [oobsum:summary] {"calls":[{"args":[{"have":"-p1 + ext(p0)","obj":"*p0"},"wrap32(-p1 + 128)",{"alias":"p2","have":"ext(p2)","obj":"*p2"},"p3"],"callee":"memcpy_s",{{("callee_key":"[^"]*",)?}}"model":"access"
// CHECK-SAME: "fn":"Put",
// CHECK-SAME: "end":["128","128"],"guard":["p1 <= 128"]
// CHECK-SAME: "kind":"W"
// CHECK-SAME: "end":["p3","p3"],"guard":["p1 + p3 <= 128"]
// CHECK-SAME: "kind":"R"
// 第二段（SPEC-0020）：ulOff >= 2^31 + 129 时回绕后不超过 0x7fffffff，照样写；段守卫总挂上
// CHECK-SAME: "end":["4294967424","4294967424"],"guard":["p1 >= 2147483777"]
// CHECK-SAME: "end":["p3","p3"],"guard":["p1 + p3 <= 4294967424","p1 >= 2147483777"]

void Clear(void)
{
    UINT8 *pucBuf = (UINT8 *)VOS_MemAlloc(1, 0, 64);
    (void)memset_s(pucBuf, 80, 0, 80);
    LOG_Warn(1, "cleared %p", pucBuf);
}
// 堆对象 64 字节、memset_s 写 80 字节；日志按纯函数（^LOG_），指针实参为 null
// CHECK: [oobsum:summary] {"calls":[{"args":["1","0","64"],"callee":"VOS_MemAlloc",{{("callee_key":"[^"]*",)?}}"model":"alloc"
// CHECK-SAME: {"args":[{"have":"64","obj":"heap@models.c:32"},"80","0","80"],"callee":"memset_s",{{("callee_key":"[^"]*",)?}}"model":"access"
// CHECK-SAME: {"args":["1",null,null],"callee":"LOG_Warn",{{("callee_key":"[^"]*",)?}}"model":"pure"
// CHECK-SAME: "fn":"Clear",
// CHECK-SAME: "reqs":[{"access":{"at":"models.c:33","end":"80"
// CHECK-SAME: "have":"64","id":"R1","kind":"W","obj":"heap@models.c:32"

static void Entry(void) {}
void Reg(void) { OM_RegMsg(1, Entry); }
// 函数指针实参写 null；未知被调者在链接时判（S8c），这里没有 model
// CHECK: [oobsum:summary] {"calls":[{"args":["1",null],"callee":"OM_RegMsg",{{("callee_key":"[^"]*",)?}}"results":[]
