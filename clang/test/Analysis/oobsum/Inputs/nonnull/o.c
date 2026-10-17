/* nonnull-guard.test 的输入（SPEC-0099）：指针形参判空产生守卫 nn:p<k>。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
#define NULL ((void *)0)
extern int memset_s(void *d, UINT32 dm, int c, UINT32 n);
static void Get(UINT8 *pOut, UINT32 n) { if (pOut != NULL) { (void)memset_s(pOut, n, 0, n); } }
void O1(void) { Get(NULL, 100); }
void O2(void) { UINT8 a[4]; Get(a, 100); }
static void Opt(UINT8 *p, UINT32 i, UINT8 *q) { if (q != NULL) { p[i] = 0; } }
void O3(void) { UINT8 a[4]; Opt(a, 10, NULL); }
void O4(void) { UINT8 a[4]; UINT8 b[1]; Opt(a, 10, b); }
static void Early(UINT8 *pOut, UINT32 n) { if (pOut == NULL) { return; } (void)memset_s(pOut, n, 0, n); }
void O5(void) { Early(NULL, 100); }
static void MidO(UINT8 *a, UINT8 *q) { Opt(a, 10, q); }
void O6(void) { UINT8 a[4]; MidO(a, NULL); }
void O7(void) { UINT8 a[4]; UINT8 b[1]; MidO(a, b); }
