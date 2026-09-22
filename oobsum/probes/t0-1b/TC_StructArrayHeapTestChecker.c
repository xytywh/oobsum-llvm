/*
 * 作者: g00586360    日期: 2026-09-22
 * 用途: T0-1b 调研探针。观察 CSA（LLVM 14）对各类数组访问给出的 extent：
 *       数组本身那一层（ArrayBoundCheckerV2 的比较基准）、最外层对象、dynamic extent、元素个数、内存空间。
 *       只打印，不报告。对应 infer-research-kit/08-summary2-design/03-assumptions-and-test-plan.md 的 T0-1b。
 *
 * 运行命令:
 *   /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa -cc_json=/usr1/BXPlugin/cmake-build-release-release/ -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/StructArrayHeapTestChecker/TC_StructArrayHeapTestChecker.c -checker=rb.enhance.core.StructArrayHeapTest "-extra_args=-analyzer-config ipa=none -analyzer-max-loop 3 -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *   对照组 1（打开 malloc 建模，看 B9 的 dynamic extent）：在 -extra_args 里追加
 *       -analyzer-checker=unix.DynamicMemoryModeling
 *   对照组 2（把单元素末尾数组当柔性数组，看 B5）：在 -extra_args 里追加
 *       -analyzer-config consider-single-element-arrays-as-flexible-array-members=true
 *
 * 问题单: 无（调研探针，非缺陷规则）
 *
 * 用例清单:
 *   A      原 T0-1 的 10 个访问，保持原样，便于前后对照
 *   B1     结构体【非末尾】字段数组：字段层 32B 与对象层 36B 的差别只有在这里才决定能不能检出（结构体内越界）
 *   B2     二维数组字段：比较基准是整个 m（48B）还是某一行（16B）
 *   B3-B5  柔性数组成员的三种写法 d[] / d[0] / d[1]
 *   B6     形参裸指针 int *q：被调函数自己不可能知道 extent —— 这正是摘要要补的那一块
 *   B7     extern 不完整数组：别的 TU 定义，本 TU 看不到大小
 *   B8     long 数组：元素大小随目标 ABI 变（LP64 8B / ILP32 4B）
 *   B9     malloc：大小只在 dynamic extent 里，且依赖 malloc 建模是否开启
 *   B10    p = &a[2]; p[j]    指针已指向数组中间，再用符号下标 —— 上游 LLVM 14 在这里连 location 都给不出
 *   B11    p = a + k; p[j]    同上，基址偏移本身是符号
 *   B12    p = &a[2]; *(p+j)  与 B10 语义完全相同的对照组，只是写法换成指针算术
 */
#include "stdlib.h"

/* ---------------- A：原 T0-1，保持原样 ---------------- */
struct S { int n; int d[8]; };
struct Out { struct S in; };
int g_noinit[10];
int g_init[10] = {0};
struct S gs;

int f(int i, struct S *p) {
    struct S s;  static struct S st;  struct S arr[4];  int a[10];
    struct Out o;
    int *heap = (int*)malloc(4 * sizeof(int));
    return a[i] + s.d[i] + st.d[i] + gs.d[i] + arr[1].d[i] + o.in.d[i]
           + p->d[i] + g_noinit[i] + g_init[i] + heap[i];
}

/* ---------------- B：T0-1b 新增 ---------------- */

/* B1：字段数组不在末尾 —— d[8] 会落到 tail 上，仍在对象 t 内部。
 *     只看对象层（36B）就检不出来；必须用字段层（32B）。 */
struct T { int d[8]; int tail; };
int b1_intra_object(int i)
{
    struct T t = {0};
    return t.d[i];
}

/* B2：二维数组字段 —— x.m[0][5] 越出了第 0 行，但没越出整个 m */
struct M { int m[3][4]; };
int b2_two_dim(int j)
{
    struct M x = {0};
    return x.m[1][j];
}

/* B3-B5：柔性数组成员的三种写法（嵌入式消息结构里很常见） */
struct F0 { int n; int d[]; };
struct Fz { int n; int d[0]; };
struct F1 { int n; int d[1]; };
int b3_b5_fam(int i, struct F0 *f0, struct Fz *fz, struct F1 *f1)
{
    return f0->d[i] + fz->d[i] + f1->d[i];
}

/* B6：形参裸指针 */
int b6_param_ptr(int i, int *q)
{
    return q[i];
}

/* B7：extern 不完整数组 */
extern int ext_arr[];
int b7_extern(int i)
{
    return ext_arr[i];
}

/* B8：long 数组 */
int b8_long(int i)
{
    long lg[4] = {0};
    return (int)lg[i];
}

/* B9：malloc */
int b9_heap(int i)
{
    int *h = (int *)malloc(4 * sizeof(int));
    return h[i];
}

/* B10：指针已指向数组中间，再用符号下标 */
int b10_mid_subscript(int j)
{
    int a[10] = {0};
    int *p = &a[2];
    return p[j];
}

/* B11：基址偏移本身是符号，再用符号下标 */
int b11_sym_base_subscript(int j, int k)
{
    int a[10] = {0};
    int *p = a + k;
    return p[j];
}

/* B12：B10 的对照组 —— 语义相同，写法换成指针算术 */
int b12_mid_arith(int j)
{
    int a[10] = {0};
    int *p = &a[2];
    return *(p + j);
}
