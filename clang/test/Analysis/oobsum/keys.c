// 函数 / 全局的键（oobsum/specs/SPEC-0003；06- D-01…D-05、D-08）与 Dump=keys。
// RUN: rm -rf %t && mkdir -p %t/a %t/b %t/r
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -I %S/Inputs \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=keys \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t/a %s 2>&1 | FileCheck %s
// DumpFilter：函数按名字、函数内 static 按所在函数、全局按自己的名字
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -I %S/Inputs \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=keys \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=^helper$ %s 2>&1 | FileCheck %s --check-prefix=FILTER
// 两个 TU 里同一个头文件函数键相同、各自的同名 static 键不同；相对路径调用与绝对路径调用的文件 id 相同（D-05）
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -I %S/Inputs \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=keys \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t/b %S/Inputs/keys-b.c 2>/dev/null
// RUN: cd %S && %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary -I Inputs \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=keys \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:WorkDir=%t/r keys.c 2>/dev/null
// RUN: cat %t/a/dump/*/keys/keys.txt %t/b/dump/*/keys/keys.txt %t/r/dump/*/keys/keys.txt | FileCheck %s --check-prefix=XTU

#include "keys.h"

int g_pub;
static int s_priv;
extern int g_ext;

static int helper(int x) { return x + s_priv; }

int pub_fn(int x)
{
    static int s_cnt;
    extern int g_late;
    s_cnt++;
    return helper(x) + hdr_min(x, g_pub) + g_ext + g_late + hdr_count();
}

// 头文件里的 static inline 标 header，键带头文件 id（#<FIR 哈希> 在 S4 补）；只有声明的 hdr_count 不在表里
// CHECK: [oobsum:keys] file [[HDR:[0-9a-f]{16}]] {{.*}}/Inputs/keys.h
// CHECK-NEXT: [oobsum:keys] F@[[HDR]]:hdr_min func def header {{.*}}/Inputs/keys.h:1:19
// CHECK-NEXT: [oobsum:keys] G@[[HDR]]:s_hdr_tbl global def {{.*}}/Inputs/keys.h:2:12
// CHECK-NEXT: [oobsum:keys] G:g_ext global decl {{.*}}/Inputs/keys.h:3:12
// CHECK-NEXT: [oobsum:keys] file [[MAIN:[0-9a-f]{16}]] {{.*}}/oobsum/keys.c
// CHECK-NEXT: [oobsum:keys] G:g_pub global def {{.*}}/keys.c:21:5
// CHECK-NEXT: [oobsum:keys] G@[[MAIN]]:s_priv global def {{.*}}/keys.c:22:12
// CHECK-NEXT: [oobsum:keys] F@[[MAIN]]:helper func def {{.*}}/keys.c:25:12
// CHECK-NEXT: [oobsum:keys] F:pub_fn func def {{.*}}/keys.c:27:5
// CHECK-NEXT: [oobsum:keys] S@F:pub_fn:s_cnt static def {{.*}}/keys.c:29:16
// CHECK-NEXT: [oobsum:keys] G:g_late global decl {{.*}}/keys.c:30:16
// CHECK-NOT: [oobsum:keys]

// FILTER-NOT: hdr_min
// FILTER: [oobsum:keys] file [[MAIN:[0-9a-f]{16}]] {{.*}}/keys.c
// FILTER-NEXT: [oobsum:keys] F@[[MAIN]]:helper func def
// FILTER-NOT: [oobsum:keys]

// keys.txt：a（绝对路径）、b（另一个 TU）、r（相对路径调用 keys.c）
// XTU: file [[HDR:[0-9a-f]{16}]] {{.*}}/Inputs/keys.h
// XTU-NEXT: F@[[HDR]]:hdr_min func def header
// XTU: file [[A:[0-9a-f]{16}]] {{.*}}/oobsum/keys.c
// XTU: F@[[A]]:helper func def
// XTU: file [[HDR]] {{.*}}/Inputs/keys.h
// XTU-NEXT: F@[[HDR]]:hdr_min func def header
// XTU: file [[B:[0-9a-f]{16}]] {{.*}}/Inputs/keys-b.c
// XTU-NEXT: F@[[B]]:helper func def
// XTU-NEXT: F:other_fn func def
// XTU: file [[HDR]] {{.*}}/Inputs/keys.h
// XTU: file [[A]] {{.*}}/oobsum/keys.c
// XTU: F@[[A]]:helper func def
