// 类型与记录布局（oobsum/specs/SPEC-0007 §2.1、§2.2；06- D-10、D-39）：同一份源码，ILP32 + 大端与 LP64 的大小 / 偏移不同；
// char 的符号取编译选项（driver 对 ARM / AArch64 传 -fno-signed-char，这里照做）；记录名走 typedef 反查（干净名 → tag 名 → 匿名）。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s --check-prefixes=CHECK,ILP32
// RUN: %clang_analyze_cc1 -triple x86_64-pc-linux-gnu -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s --check-prefixes=CHECK,LP64

typedef unsigned char UINT8;
typedef unsigned int UINT32;

typedef struct tagUeCtx {
    UINT32 ulId;
    void *pvPriv;
    long lCnt;
    UINT8 aucName[6];
    char cFlag;
} UE_CTX_S;

struct OnlyTag {
    short sVal;
    struct OnlyTag *pstNext;
};

typedef struct {
    UINT8 ucA;
    union {
        UINT32 ulWord;
        UINT8 aucByte[4];
    } u;
} ANON_S;

struct Flex {
    UINT32 ulNum;
    UINT32 aulItem[];
};

struct Zero {
    UINT32 ulNum;
    UINT8 aucData[0];
};

struct Bits {
    UINT32 a : 3;
    UINT32 : 5;
    int b : 4;
    UINT8 c;
};

typedef enum { E_A, E_B } ENUM_E;
struct Opaque;

typedef struct {
    ENUM_E enKind;
    _Bool bOk;
    struct Opaque *pstOpaque;
    void (*pfnCb)(int);
    double dVal;
    UE_CTX_S astCtx[2];
} MIX_S;

UE_CTX_S g_stCtx;
struct OnlyTag g_stTag;
ANON_S g_stAnon;
struct Flex *g_pstFlex;
struct Zero g_stZero;
struct Bits g_stBits;
MIX_S g_stMix;

// ILP32: [oobsum:fir] module tu={{[0-9a-f]+}} file={{.*}}/fir-layout.c target=armebv7-unknown-linux-gnueabi ptr=4 long=4 big-endian
// LP64: [oobsum:fir] module tu={{[0-9a-f]+}} file={{.*}}/fir-layout.c target=x86_64-pc-linux-gnu ptr=8 long=8 little-endian
// 指针与 long：ILP32 4 字节、LP64 8 字节；char 在 ARM 上无符号
// ILP32-NEXT: [oobsum:fir] record #0 UE_CTX_S size=20 { ulId@0: u32; pvPriv@4: void*; lCnt@8: i32; aucName@12: u8[6]; cFlag@18: u8 }
// LP64-NEXT: [oobsum:fir] record #0 UE_CTX_S size=32 { ulId@0: u32; pvPriv@8: void*; lCnt@16: i64; aucName@24: u8[6]; cFlag@30: i8 }
// 只有 tag 名；自引用
// ILP32-NEXT: [oobsum:fir] record #1 OnlyTag size=8 { sVal@0: i16; pstNext@4: rec#1* }
// LP64-NEXT: [oobsum:fir] record #1 OnlyTag size=16 { sVal@0: i16; pstNext@8: rec#1* }
// 匿名结构体的 typedef 名；匿名联合体的字段偏移都是 0
// CHECK-NEXT: [oobsum:fir] record #2 ANON_S size=8 { ucA@0: u8; u@4: rec#3 }
// CHECK-NEXT: [oobsum:fir] record #3 <anonymous struct/union> size=4 { ulWord@0: u32; aucByte@0: u8[4] }
// 柔性数组 [] 与 [0]：照实写，柔性数组成员不计入大小
// CHECK-NEXT: [oobsum:fir] record #4 Flex size=4 { ulNum@0: u32; aulItem@4: u32[] }
// CHECK-NEXT: [oobsum:fir] record #5 Zero size=4 { ulNum@0: u32; aucData@4: u8[0] }
// 位域：off 是位偏移 / 8，类型是位宽；无名位域不导出
// CHECK-NEXT: [oobsum:fir] record #6 Bits size=4 { a@0: u3; b@1: i4; c@2: u8 }
// 枚举取底层类型、_Bool 是 u8、指向不完整结构体 / 函数的指针是 other0*、double 是 other8
// ILP32-NEXT: [oobsum:fir] record #7 MIX_S size=64 { enKind@0: u32; bOk@4: u8; pstOpaque@8: other0*; pfnCb@12: other0*; dVal@16: other8; astCtx@24: rec#0[2] }
// LP64-NEXT: [oobsum:fir] record #7 MIX_S size=96 { enKind@0: u32; bOk@4: u8; pstOpaque@8: other0*; pfnCb@16: other0*; dVal@24: other8; astCtx@32: rec#0[2] }
// CHECK-NEXT: [oobsum:fir] global G:g_stCtx: rec#0 def @fir-layout.c:61:10
// CHECK: [oobsum:fir] global G:g_stMix: rec#7 def @fir-layout.c:67:7
// CHECK-NOT: error
