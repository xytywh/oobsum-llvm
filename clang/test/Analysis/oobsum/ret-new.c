// 返回新对象的效果（oobsum/specs/SPEC-0024）：封装分配器逐层推出 ret_new（eff_depth 1、2），调用方拿到 heap@<调用点>。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=summary %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;
void *VOS_MemAlloc(UINT32 ulPid, UINT32 ulPtNo, UINT32 ulSize);
void Use(UINT8 *pucBuf);

static void *Pub_Alloc(UINT32 ulSize)
{
    return VOS_MemAlloc(1, 0, ulSize);
}
// CHECK: [oobsum:summary] {"calls":[{"args":["1","0","p0"],"callee":"VOS_MemAlloc",{{("callee_key":"[^"]*",)?}}"model":"alloc"
// CHECK-SAME: "effs":[{"eff_depth":1,"kind":"ret_new","maybe_null":true,"size":"p0"}],"fn":"Pub_Alloc"

static UINT8 *Tbl_Alloc(void)
{
    UINT8 *pucTbl = (UINT8 *)Pub_Alloc(64);
    if (pucTbl == 0) {
        return 0;
    }
    return pucTbl;
}
// 空指针分支不影响：仍是 ret_new，大小代入实参 64
// CHECK: [oobsum:summary] {{.*}}"effs":[{"eff_depth":2,"kind":"ret_new","maybe_null":true,"size":"64"}],"fn":"Tbl_Alloc"

void Init(void)
{
    UINT8 *pucTbl = Tbl_Alloc();
    Use(pucTbl);
}
// 调用方：堆对象 heap@<调用点>、64 字节，实参写 eff_depth
// CHECK: [oobsum:summary] {"calls":[{"args":[],"callee":"Tbl_Alloc"
// CHECK-SAME: {"args":[{"eff_depth":2,"have":"64","obj":"heap@ret-new.c:30"}],"callee":"Use"
// CHECK-SAME: "fn":"Init"
