// 有副作用的构造（oobsum/specs/SPEC-0008 §2.8；06- A-06）：++ / --（先 load 再 store，后缀形的值是旧值）、复合赋值、
// 结构体整体赋值 → __oobsum_copy、初始化列表逐元素；赋值表达式的值取赋值之后的局部（不能再用右边那棵树）。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;

typedef struct {
    UINT32 ulId;
    UINT16 ausVal[3];
    UINT8 *pucBuf;
} REC_S;

UINT32 g_aulStat[4];

void IncDec(UINT32 i, UINT32 *p)
{
    UINT32 x = 0;
    UINT32 y;
    g_aulStat[i]++;
    y = x++;
    y = ++x;
    p++;
    *p-- = y;
    --y;
}

UINT32 Compound(UINT8 uc, UINT32 ul, UINT32 *p, UINT32 n)
{
    uc += ul;
    ul <<= 2;
    p += n;
    p -= 1;
    *p |= 8;
    return uc + ul;
}

UINT32 AssignValue(UINT32 x)
{
    UINT32 y;
    y = (x = x + 1);
    return y;
}

void Copy(REC_S *pDst, REC_S *pSrc)
{
    REC_S st = *pSrc;
    *pDst = st;
}

UINT32 Init(UINT8 *buf)
{
    UINT32 aul[4] = {1, 2};
    REC_S st = {7, {1, 2, 3}, buf};
    UINT8 auc[8] = "abc";
    return aul[1] + st.ulId + auc[0];
}

// ++ / --：内存左值先 load 再 store（一次 R、一次 W）；标量局部的后缀形先把旧值存进临时局部；指针按元素大小走
// CHECK: [oobsum:fir] fn F:IncDec
// CHECK:      l2 = 0  @20:16
// CHECK-NEXT: l4 = load4 [padd(&G:g_aulStat, l0 * 4)] sub  @22:5
// CHECK-NEXT: store4 [padd(&G:g_aulStat, l0 * 4)] = (l4 + 1):u32 sub  @22:5
// CHECK-NEXT: l5 = l2  @23:9
// CHECK-NEXT: l2 = (l5 + 1):u32  @23:9
// CHECK-NEXT: l3 = l5  @23:5
// CHECK-NEXT: l2 = (l2 + 1):u32  @24:9
// CHECK-NEXT: l3 = l2  @24:5
// CHECK-NEXT: l6 = l1  @25:5
// CHECK-NEXT: l1 = padd(l6, 4)  @25:5
// CHECK-NEXT: l7 = l1  @26:6
// CHECK-NEXT: l1 = padd(l7, -4)  @26:6
// CHECK-NEXT: store4 [l7] = l3  @26:5
// CHECK-NEXT: l3 = (l3 - 1):u32  @27:5
// 复合赋值：按计算类型算、转回左边的类型；指针 += / -=；内存左值先 load 再 store
// CHECK: [oobsum:fir] fn F:Compound
// CHECK:      l0 = (u8)(((u32)l0 + l1):u32)  @32:5
// CHECK-NEXT: l1 = (l1 << 2):u32  @33:5
// CHECK-NEXT: l2 = padd(l2, l3 * 4)  @34:5
// CHECK-NEXT: l2 = padd(l2, -4)  @35:5
// CHECK-NEXT: l4 = load4 [l2]  @36:5
// CHECK-NEXT: store4 [l2] = (l4 | 8):u32  @36:5
// 赋值表达式的值取赋值之后的局部：y = (x = x + 1) 的 y 是新的 x，不是"新的 x + 1"
// CHECK: [oobsum:fir] fn F:AssignValue
// CHECK:      l0 = (l0 + 1):u32  @43:10
// CHECK-NEXT: l1 = l0  @43:5
// 结构体整体赋值 / 用结构体初始化 → __oobsum_copy(&目的, &源, sizeof)（A-06）；按值读的 load 也保留
// CHECK: [oobsum:fir] fn F:Copy
// CHECK:      l3 = load16 [l1]  @49:16
// CHECK-NEXT: call F:__oobsum_copy(&l2, l1, 16) #0  @49:16
// CHECK-NEXT: l4 = load16 [&l2]  @50:13
// CHECK-NEXT: call F:__oobsum_copy(l0, &l2, 16) #1  @50:5
// 初始化列表逐元素按偏移 store；隐式补零与字符串初始化不降
// CHECK: [oobsum:fir] fn F:Init
// CHECK:      store4 [&l1] = 1  @55:21
// CHECK-NEXT: store4 [padd(&l1, 4)] = 2  @55:21
// CHECK-NEXT: store4 [&l2] = 7  @56:16
// CHECK-NEXT: store2 [padd(&l2, 4)] = 1  @56:16
// CHECK-NEXT: store2 [padd(&l2, 6)] = 2  @56:16
// CHECK-NEXT: store2 [padd(&l2, 8)] = 3  @56:16
// CHECK-NEXT: store4 [padd(&l2, 12)] = l0  @56:16
// CHECK-NEXT: l4 = load4 [padd(&l1, 4)] sub  @58:12
// 函数不再带 body_pending
// CHECK-NOT: body_pending
// CHECK-NOT: error
