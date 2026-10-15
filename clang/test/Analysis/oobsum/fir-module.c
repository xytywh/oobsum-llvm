// 模块骨架（oobsum/specs/SPEC-0007 §2.3、§2.4；SPEC-0006 §3 的 Dump=fir）：全局、函数签名、局部（param / obj）、CFG 块与边。
// 块号与 debug.DumpCFG 的 [B<n>] 一致；边上的条件在 fir-branches.c 里测。
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -I %S/Inputs \
// RUN:   -analyzer-checker=rb.enhance.ipa.OobSummary -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t %s 2>&1 | FileCheck %s
// RUN: cat %t/dump/*/fir/*.txt | FileCheck %s --check-prefix=TXT
// RUN: cat %t/dump/*/fir/*.json | FileCheck %s --check-prefix=JSON
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -I %S/Inputs \
// RUN:   -analyzer-checker=rb.enhance.ipa.OobSummary -analyzer-config rb.enhance.ipa.OobSummary:Dump=fir \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=^Mod_Nop$ %s 2>&1 | FileCheck %s --check-prefix=FILTER
// extern int a[]; 之后的 int a[10];（暂定定义）：键表也要认出是定义（SPEC-0007 §2.3）
// RUN: %clang_analyze_cc1 -triple armebv7-linux-gnueabi -fno-signed-char -I %S/Inputs \
// RUN:   -analyzer-checker=rb.enhance.ipa.OobSummary -analyzer-config rb.enhance.ipa.OobSummary:Dump=keys %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=KEYS

#include "keys.h"

extern int g_aiLate[];
int g_aiLate[10];
static int s_iCnt;

int Mod_Sum(int *piBuf, unsigned n)
{
    int sum = 0;
    int aiTmp[4];
    struct { int a; } st;
    int x;
    int *p = &x;
    unsigned i = 0;
    static int s_calls;
    extern int g_ext2;
    while (i < n) {
        sum += piBuf[i];
        i++;
    }
    if (sum > 0) {
        int x = 1;
        sum += x;
    }
    return sum + aiTmp[0] + st.a + *p + s_calls + g_ext2 + hdr_min(1, 2) + s_iCnt;
}

static void Mod_Nop(void) {}

// CHECK: [oobsum:fir] module tu=[[TU:[0-9a-f]+]] file={{.*}}/fir-module.c target=armebv7-unknown-linux-gnueabi ptr=4 long=4 big-endian
// CHECK-NEXT: [oobsum:fir] record #0 <anonymous struct/union> size=4 { a@0: i32 }
// 头文件里的全局；extern 声明；extern int a[] + 定义取完整类型；static；函数内 static；块作用域 extern
// CHECK-NEXT: [oobsum:fir] global G@{{[0-9a-f]+}}:s_hdr_tbl: i32[4] def @keys.h:2:12
// CHECK-NEXT: [oobsum:fir] global G:g_ext: i32 decl @keys.h:3:12
// CHECK-NEXT: [oobsum:fir] global G:g_aiLate: i32[10] def @fir-module.c:20:5
// CHECK-NEXT: [oobsum:fir] global G@[[TU]]:s_iCnt: i32 def @fir-module.c:21:12
// CHECK-NEXT: [oobsum:fir] global S@F:Mod_Sum:s_calls: i32 def @fir-module.c:31:16
// CHECK-NEXT: [oobsum:fir] global G:g_ext2: i32 decl @fir-module.c:32:16
// 头文件 static inline 也导出（X-01）
// CHECK-NEXT: [oobsum:fir] fn F@{{[0-9a-f]+}}:hdr_min -> i32 @keys.h:1:19
// CHECK-NEXT: [oobsum:fir]   local l0 a param0: i32
// CHECK-NEXT: [oobsum:fir]   local l1 b param1: i32
// CHECK: [oobsum:fir]   b5 entry:
// 局部：先形参再函数体里的变量；数组、结构体、取过地址的标量是 obj；同名嵌套变量各有各的号；函数内 static / 块作用域 extern 不是局部
// CHECK: [oobsum:fir] fn F:Mod_Sum ext -> i32 @fir-module.c:23:5
// CHECK-NEXT: [oobsum:fir]   local l0 piBuf param0: i32*
// CHECK-NEXT: [oobsum:fir]   local l1 n param1: u32
// CHECK-NEXT: [oobsum:fir]   local l2 sum: i32
// CHECK-NEXT: [oobsum:fir]   local l3 aiTmp obj: i32[4]
// CHECK-NEXT: [oobsum:fir]   local l4 st obj: rec#0
// CHECK-NEXT: [oobsum:fir]   local l5 x obj: i32
// CHECK-NEXT: [oobsum:fir]   local l6 p: i32*
// CHECK-NEXT: [oobsum:fir]   local l7 i: u32
// CHECK-NEXT: [oobsum:fir]   local l8 x: i32
// 块与边与 debug.DumpCFG 一致（B9 的两个后继来自函数内 static 的初始化分支；while 的回边 B4 -> B6）。
// 指令在 fir-straight.c 里测，这里只看骨架：每个块的边紧接在下一个块头之前
// CHECK: [oobsum:fir]   b10 entry:
// CHECK: [oobsum:fir]     -> b9
// CHECK-NEXT: [oobsum:fir]   b9:
// CHECK: [oobsum:fir]     -> b7
// CHECK-NEXT: [oobsum:fir]     -> b8
// CHECK-NEXT: [oobsum:fir]   b8:
// CHECK-NEXT: [oobsum:fir]     -> b7
// CHECK-NEXT: [oobsum:fir]   b7:
// CHECK-NEXT: [oobsum:fir]     -> b6
// CHECK-NEXT: [oobsum:fir]   b6:
// CHECK-NEXT: [oobsum:fir]     -> b5
// CHECK-NEXT: [oobsum:fir]     -> b3
// CHECK-NEXT: [oobsum:fir]   b5:
// CHECK: [oobsum:fir]     -> b4
// CHECK-NEXT: [oobsum:fir]   b4:
// CHECK-NEXT: [oobsum:fir]     -> b6
// CHECK-NEXT: [oobsum:fir]   b3:
// CHECK-NEXT: [oobsum:fir]     -> b2
// CHECK-NEXT: [oobsum:fir]     -> b1
// CHECK-NEXT: [oobsum:fir]   b2:
// CHECK: [oobsum:fir]     -> b1
// CHECK-NEXT: [oobsum:fir]   b1:
// CHECK: [oobsum:fir]     -> b0
// CHECK-NEXT: [oobsum:fir]   b0 exit:
// CHECK-NEXT: [oobsum:fir] fn F@[[TU]]:Mod_Nop -> void @fir-module.c:44:13
// CHECK-NEXT: [oobsum:fir]   b1 entry:
// CHECK-NEXT: [oobsum:fir]     -> b0
// CHECK-NEXT: [oobsum:fir]   b0 exit:
// 读回自检没有报错
// CHECK-NOT: error

// TXT: module tu={{[0-9a-f]+}} file={{.*}}/fir-module.c
// TXT: fn F:Mod_Sum ext -> i32 @fir-module.c:23:5
// TXT:   local l5 x obj: i32

// 对象键按字母排序：funcs 在 schema 前面
// JSON: "key": "F:Mod_Sum"
// JSON: "schema": "oobsum-fir/1"

// FILTER: [oobsum:fir] module
// FILTER-NOT: fn F:Mod_Sum
// FILTER: [oobsum:fir] fn F@{{[0-9a-f]+}}:Mod_Nop -> void
// FILTER-NOT: [oobsum:fir] fn

// KEYS: [oobsum:keys] G:g_aiLate global def {{.*}}/fir-module.c:20:5
