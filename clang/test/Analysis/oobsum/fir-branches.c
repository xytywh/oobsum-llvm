// 条件边与当值用的 ?: / && / ||（oobsum/specs/SPEC-0009；06- D-14、A-04）。后继 0 是条件为真、后继 1 是条件为假（Clang CFG 的约定），
// 每个函数的块号、后继顺序与 debug.DumpCFG 对照过（Succs (2): B<真> B<假>）。
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir %s 2>&1 | FileCheck %s

typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef signed int INT32;

void Sink(UINT32 x);

void If(UINT32 i, UINT32 n, UINT8 *p, INT32 s, UINT32 f)
{
    if (i < n) Sink(1);
    if (p) Sink(2);
    if (!f) Sink(3);
    if (f) Sink(4);
    if (s >= 0) Sink(5); else Sink(6);
}

void Loops(UINT32 n)
{
    UINT32 i;
    for (i = 0; i < n; i++) Sink(i);
    while (i > 3) i--;
    do { i++; } while (i != 10);
    for (;;) { if (i == 20) break; i++; }
    while (1) { if (i == 30) return; i++; }
}

void Logic(UINT32 a, UINT32 b)
{
    if (a && b) Sink(1);
    if (a || b) Sink(2);
}

void Switch(UINT32 k, INT32 s)
{
    switch (k) {
        case 1: Sink(1); break;
        case 2: Sink(2); break;
        default: Sink(3); break;
    }
    switch (s) {
        case -1: Sink(4); break;
        case 1 ... 5: Sink(5); break;
    }
    switch (k) {
        case 7: Sink(7); break;
    }
}

UINT32 Values(UINT32 a, UINT32 b, UINT32 c)
{
    UINT32 m = a < b ? a : b;
    UINT32 x = a && b;
    UINT32 y = (a && b) || c;
    return m + x + y;
}

void SideEffect(UINT32 i, UINT32 n)
{
    while (i++ < n) Sink(i);
}

// if：比较、指针（!= 0）、!x、整数、有符号比较；后继 0 为真
// CHECK: [oobsum:fir] fn F:If
// CHECK:      b11:
// CHECK-NEXT:   -> b10 if l0 <u l1
// CHECK-NEXT:   -> b9 if l0 >=u l1
// CHECK:      b9:
// CHECK-NEXT:   -> b8 if l2 !=u 0
// CHECK-NEXT:   -> b7 if l2 ==u 0
// CHECK:      b7:
// CHECK-NEXT:   -> b6 if !l4
// CHECK-NEXT:   -> b5 if l4 !=u 0
// CHECK:      b5:
// CHECK-NEXT:   -> b4 if l4 !=u 0
// CHECK-NEXT:   -> b3 if l4 ==u 0
// CHECK:      b3:
// CHECK-NEXT:   -> b2 if l3 >=s 0
// CHECK-NEXT:   -> b1 if l3 <s 0
// 循环：for / while / do（回边为真）/ for (;;) 与 while (1)（不可达后继按原下标、常数条件不带条件）
// CHECK: [oobsum:fir] fn F:Loops
// CHECK:      b19:
// CHECK-NEXT:   -> b18 if l1 <u l0
// CHECK-NEXT:   -> b16 if l1 >=u l0
// CHECK:      b16:
// CHECK-NEXT:   -> b15 if l1 >u 3
// CHECK-NEXT:   -> b12 if l1 <=u 3
// CHECK:      b11:
// CHECK-NEXT:   -> b13 if l1 !=u 10
// CHECK-NEXT:   -> b10 if l1 ==u 10
// CHECK-NEXT: b10:
// CHECK-NEXT:   -> b9{{$}}
// CHECK-NEXT: b9:
// CHECK-NEXT:   -> b8 if l1 ==u 20
// CHECK-NEXT:   -> b7 if l1 !=u 20
// CHECK:      b5:
// CHECK-NEXT:   -> b4{{$}}
// if (a && b) / if (a || b)：第一个块测 a，第二个块测 b（同 ExprEngine 的 ResolveCondition）
// CHECK: [oobsum:fir] fn F:Logic
// CHECK:      b6:
// CHECK-NEXT:   -> b5 if l0 !=u 0
// CHECK-NEXT:   -> b3 if l0 ==u 0
// CHECK-NEXT: b5:
// CHECK-NEXT:   -> b4 if l1 !=u 0
// CHECK-NEXT:   -> b3 if l1 ==u 0
// CHECK:      b3:
// CHECK-NEXT:   -> b1 if l0 !=u 0
// CHECK-NEXT:   -> b2 if l0 ==u 0
// CHECK-NEXT: b2:
// CHECK-NEXT:   -> b1 if l1 !=u 0
// CHECK-NEXT:   -> b0 if l1 ==u 0
// switch：case 按标号；default 是各 != 的按位与；GNU 区间不带条件、有区间时"switch 之后"也不带；只有一个 case
// CHECK: [oobsum:fir] fn F:Switch
// CHECK:      b6:
// CHECK-NEXT:   -> b8 if l0 ==u 2
// CHECK-NEXT:   -> b9 if l0 ==u 1
// CHECK-NEXT:   -> b7 if (l0 !=u 1) & (l0 !=u 2)
// CHECK:      b3:
// CHECK-NEXT:   -> b4{{$}}
// CHECK-NEXT:   -> b5 if l1 ==s -1
// CHECK-NEXT:   -> b1{{$}}
// CHECK:      b1:
// CHECK-NEXT:   -> b2 if l0 ==u 7
// CHECK-NEXT:   -> b0 if l0 !=u 7
// 当值用：?: 两支各 set；&& 短路那一跳写 0、右边写 b != 0；(a && b) || c 下沉到 b 块的 || 写 1
// CHECK: [oobsum:fir] fn F:Values
// CHECK:      b9:
// CHECK-NEXT:   -> b7 if l0 <u l1
// CHECK-NEXT:   -> b8 if l0 >=u l1
// CHECK-NEXT: b8:
// CHECK-NEXT:   l6 = l1  @55:16
// CHECK:      b7:
// CHECK-NEXT:   l6 = l0  @55:16
// CHECK:      b6:
// CHECK-NEXT:   l3 = l6  @55:16
// CHECK-NEXT:   l7 = 0  @56:16
// CHECK-NEXT:   -> b5 if l0 !=u 0
// CHECK-NEXT:   -> b4 if l0 ==u 0
// CHECK-NEXT: b5:
// CHECK-NEXT:   l7 = l1 !=u 0  @56:16
// CHECK:      b4:
// CHECK-NEXT:   l4 = (u32)l7  @56:16
// CHECK:      b3:
// CHECK-NEXT:   l8 = 1  @57:16
// CHECK-NEXT:   -> b1 if l1 !=u 0
// CHECK-NEXT:   -> b2 if l1 ==u 0
// CHECK-NEXT: b2:
// CHECK-NEXT:   l8 = l2 !=u 0  @57:16
// CHECK:      b1:
// CHECK-NEXT:   l5 = (u32)l8  @57:16
// 条件里有副作用：比较的是自增前保存的旧值
// CHECK: [oobsum:fir] fn F:SideEffect
// CHECK:      l2 = l0  @63:12
// CHECK-NEXT: l0 = (l2 + 1):u32  @63:12
// CHECK-NEXT: -> b2 if l2 <u l1
// CHECK-NEXT: -> b0 if l2 >=u l1
// CHECK-NOT: error
