/* loop-le.test 的输入（SPEC-0056）：i <= n 的循环。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Fill_Le(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i <= n; i++) { p[i] = 0; } }
void Le_TP(void) { UINT8 a[16]; Fill_Le(a, 16); }
void Le_OK(void) { UINT8 a[16]; Fill_Le(a, 15); }
static void Fill_LeC(UINT8 *p) { UINT32 i; for (i = 0; i <= 15; i++) { p[i] = 0; } }
void LeC_OK(void) { UINT8 a[16]; Fill_LeC(a); }
/* 对照：小的一边 i + 8 下界是 8，可能回绕 → 不收窄、记账 */
static void Fill_Sum(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i + 8 <= n; i++) { p[i] = 0; } }
void Sum_TP(UINT32 n) { UINT8 a[16]; Fill_Sum(a, n); }
