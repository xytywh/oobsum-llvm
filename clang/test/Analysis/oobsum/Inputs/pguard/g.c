/* path-guard.test 的输入（SPEC-0096）：Req 带上只关于别的形参的路径条件。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void P(UINT8 *p, UINT32 i, UINT32 c) { if (c) { p[i] = 0; } }
void D1_TN(void) { UINT8 a[4]; P(a, 10, 0); }
void D1_TP(void) { UINT8 a[4]; P(a, 10, 1); }
static void Q(UINT8 *p, UINT32 i, UINT32 c) { if (c == 5) { p[i] = 0; } }
void D4_TN(void) { UINT8 a[4]; Q(a, 10, 4); }
void D4_TP(void) { UINT8 a[4]; Q(a, 10, 5); }
static void R(UINT8 *p, UINT32 i, UINT32 c) { if (c != 0) { p[i] = 0; } }
void D5_TN(void) { UINT8 a[4]; R(a, 10, 0); }
/* 对照：指针判空没有守卫，照旧报 */
static void S(UINT8 *p, UINT32 i, UINT8 *q) { if (q != 0) { p[i] = 0; } }
void D6(void) { UINT8 a[4]; S(a, 10, 0); }
