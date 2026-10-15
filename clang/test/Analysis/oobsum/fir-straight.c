// 直线代码的指令（oobsum/specs/SPEC-0008 §2.1–§2.7；06- A-01…A-03、A-05、A-07、D-31、D-37）。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef signed int INT32;

#define BUF_LEN 16
typedef enum { K_A, K_B, K_BUTT } KIND_E;

typedef struct {
    UINT32 ulLen;
    UINT8 aucBuf[BUF_LEN];
    struct { UINT16 usX; UINT16 ausY[2]; } stIn;
    UINT32 bFlag : 1;
    UINT32 bMode : 3;
} CTX_S;

typedef struct {
    UINT32 ulNum;
    UINT32 aulItem[];
} FLEX_S;

UINT32 g_ulCnt;
UINT8 g_aucTbl[8];

UINT32 Consts(void)
{
    return sizeof(CTX_S) + K_BUTT + BUF_LEN * 2 + (UINT8)300;
}

UINT32 Conv(UINT32 ul, INT32 l, UINT8 uc)
{
    UINT8 a = ul;
    INT32 b = ul;
    UINT32 c = uc;
    return a + b + c + l;
}

UINT32 Arith(UINT32 a, UINT32 b, INT32 s)
{
    UINT32 x = a * b - 1;
    UINT32 y = a << 2;
    INT32 z = -s;
    UINT32 w = ~a;
    return x / y % 3 + (UINT32)z + w + !a;
}

INT32 Cmp(UINT32 a, INT32 s, UINT8 *p, UINT8 *q)
{
    INT32 r = a < 4;
    r = s < 4;
    r = p == q;
    return r;
}

void Ptrs(UINT32 *pul, UINT8 *puc, UINT32 i)
{
    UINT32 *p1 = pul + i;
    UINT32 *p2 = pul - i;
    UINT8 *p3 = puc + i;
    UINT8 *p4 = puc - i;
    INT32 d = p2 - p1;
    *p1 = d;
    *p3 = 0;
    p4[i] = 1;
    pul[3] = 2;
}

void Members(CTX_S *pst, FLEX_S *pf, UINT32 i)
{
    CTX_S st;
    pst->aucBuf[i] = 0;
    st.stIn.ausY[i] = 1;
    pst->stIn.usX = 2;
    pf->aulItem[i] = 3;
    pst->bMode = 4;
    pst->ulLen = st.ulLen;
}

UINT32 Globals(UINT32 i)
{
    UINT32 aul[4];
    UINT32 x;
    UINT32 *p = &x;
    g_ulCnt = 1;
    g_aucTbl[i] = 2;
    aul[i] = g_aucTbl[0];
    *p = aul[1];
    return "abc"[i] + x;
}

UINT32 Cond(UINT32 a, UINT32 b)
{
    UINT32 m = a < b ? a : b;
    return m;
}

UINT32 Callee(UINT32 a, UINT8 *p);
void Sink(CTX_S st);

UINT32 Calls(UINT8 *buf, UINT32 n, UINT32 (*pfn)(UINT32, UINT8 *), CTX_S *pst)
{
    UINT32 r = Callee(n, buf);
    Callee(1, &buf[n]);
    r += pfn(n, buf);
    __builtin_memcpy(buf, pst, n);
    if (__builtin_expect(n > 4, 0)) {
        r = 0;
    }
    Sink(*pst);
    return r;
}

void Asm(UINT32 *p)
{
    UINT32 x;
    __asm__("mov %0, #1" : "=r"(x));
    __asm__("str %1, %0" : "=m"(*p) : "r"(x));
}

// 常量折叠：sizeof、枚举、宏、带截断的转换（A-03）
// CHECK: [oobsum:fir] record #0 CTX_S size=28 { ulLen@0: u32; aucBuf@4: u8[16]; stIn@20: rec#1; bFlag@26: u1; bMode@26: u3 }
// CHECK: [oobsum:fir] fn F:Consts
// CHECK:      ret 106  @31:5
// 整数转换：宽度或符号变了才有 cast；整数提升与常用算术转换来自 AST 的隐式转换
// CHECK: [oobsum:fir] fn F:Conv
// CHECK:      l3 = (u8)l0  @36:15
// CHECK-NEXT: l4 = (i32)l0  @37:15
// CHECK-NEXT: l5 = (u32)l2  @38:16
// CHECK-NEXT: ret ((((u32)(((i32)l3 + l4):i32) + l5):u32) + (u32)l1):u32  @39:5
// C 的整数运算用回绕形（宽度与符号取结果类型）；-x、~x、!x
// CHECK: [oobsum:fir] fn F:Arith
// CHECK:      l3 = (((l0 * l1):u32) - 1):u32  @44:16
// CHECK-NEXT: l4 = (l0 << 2):u32  @45:16
// CHECK-NEXT: l5 = (0 - l2):i32  @46:15
// CHECK-NEXT: l6 = (u32)~l0  @47:16
// CHECK-NEXT: ret (((((((((l3 / l4):u32) % 3):u32) + (u32)l5):u32) + l6):u32) + (u32)!l0):u32  @48:5
// 比较的有无符号取操作数类型；指针按无符号
// CHECK: [oobsum:fir] fn F:Cmp
// CHECK:      l4 = l0 <u 4  @53:15
// CHECK-NEXT: l4 = l1 <s 4  @54:5
// CHECK-NEXT: l4 = l2 ==u l3  @55:5
// 指针 ± 整数按字节（元素大小 1 时不乘）；指针相减；*p；下标带 sub、常数下标直接算出偏移
// CHECK: [oobsum:fir] fn F:Ptrs
// CHECK:      l3 = padd(l0, l2 * 4)  @61:18
// CHECK-NEXT: l4 = padd(l0, l2 * -4)  @62:18
// CHECK-NEXT: l5 = padd(l1, l2)  @63:17
// CHECK-NEXT: l6 = padd(l1, -l2)  @64:17
// CHECK-NEXT: l7 = psub(l4, l3, 4)  @65:15
// CHECK-NEXT: store4 [l3] = (u32)l7  @66:5
// CHECK-NEXT: store1 [l5] = 0  @67:5
// CHECK-NEXT: store1 [padd(l6, l2)] = 1 sub  @68:5
// CHECK-NEXT: store4 [padd(l0, 12)] = 2 sub  @69:5
// 数组成员以成员为界（fld，D-31）；嵌套成员；柔性数组成员不收窄（D-37）；位域写它跨的字节数
// CHECK: [oobsum:fir] fn F:Members
// CHECK:      store1 [padd(fld(l0, aucBuf@4:16), l2)] = 0 sub  @75:5
// CHECK-NEXT: store2 [padd(fld(padd(&l3, 20), ausY@2:4), l2 * 2)] = 1 sub  @76:5
// CHECK-NEXT: store2 [padd(l0, 20)] = 2  @77:5
// CHECK-NEXT: store4 [padd(padd(l1, 4), l2 * 4)] = 3 sub  @78:5
// CHECK-NEXT: store1 [padd(l0, 26)] = 4  @79:5
// CHECK-NEXT: l4 = load4 [&l3]  @80:18
// CHECK-NEXT: store4 [l0] = l4  @80:5
// 全局、局部对象（数组、取过地址的标量）走 load / store；标量局部走 set；字符串字面量
// CHECK: [oobsum:fir] fn F:Globals
// CHECK:      l3 = &l2  @87:17
// CHECK-NEXT: store4 [&G:g_ulCnt] = 1  @88:5
// CHECK-NEXT: store1 [padd(&G:g_aucTbl, l0)] = 2 sub  @89:5
// CHECK-NEXT: l4 = load1 [&G:g_aucTbl] sub  @90:14
// CHECK-NEXT: store4 [padd(&l1, l0 * 4)] = (u32)l4 sub  @90:5
// CHECK-NEXT: l5 = load4 [padd(&l1, 4)] sub  @91:10
// CHECK-NEXT: store4 [l3] = l5  @91:5
// CHECK-NEXT: l6 = load1 [padd(str3, l0)] sub  @92:12
// CHECK-NEXT: l7 = load4 [&l2]  @92:23
// CHECK-NEXT: ret ((u32)l6 + l7):u32  @92:5
// ?: 当值用：S5 之前是 unk(cond_value)
// CHECK: [oobsum:fir] fn F:Cond
// CHECK:      l2 = unk(cond_value)  @97:16
// 调用：直接 / 间接；__builtin_memcpy → F:memcpy；__builtin_expect 不生成调用；结构体按值：读整个结构体（load28），实参 unk；ord 递增
// CHECK: [oobsum:fir] fn F:Calls
// CHECK:      l5 = call F:Callee(l1, l0) #0  @106:16
// CHECK-NEXT: l4 = l5  @106:16
// CHECK-NEXT: l6 = call F:Callee(1, padd(l0, l1)) #1  @107:5
// CHECK-NEXT: l7 = call *(l2)(l1, l0) #2  @108:10
// CHECK-NEXT: l4 = (l4 + l7):u32  @108:5
// CHECK-NEXT: l8 = call F:memcpy(l0, l3, l1) #3  @109:5
// CHECK-NOT: __builtin_expect
// CHECK:      l9 = load28 [l3]  @113:10
// CHECK-NEXT: call F:Sink(unk(struct_by_value)) #4  @113:5
// 内联汇编的输出：标量局部 havoc、内存 store ?
// CHECK: [oobsum:fir] fn F:Asm
// CHECK:      l1 = havoc(inline_asm)  @120:5
// CHECK-NEXT: store4 [l0] = ?  @121:5
// CHECK-NOT: error
