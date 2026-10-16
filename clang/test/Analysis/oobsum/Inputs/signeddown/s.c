/* signed-down.test 的输入（SPEC-0076）：有符号递减循环。 */
typedef int INT32;
typedef unsigned char UINT8;
static void D3(UINT8 *p, INT32 n) { INT32 i; for (i = n - 1; i >= 0; i--) { p[i] = 0; } }
void D3_TP(void) { UINT8 a[16]; D3(a, 17); }
void D3_TN(void) { UINT8 a[16]; D3(a, 16); }
/* 对照：有符号下标只查了上界 → 负偏移那一侧照旧记账 */
static void P1(UINT8 *p, INT32 i) { if (i < 16) { p[i] = 0; } }
void P1_TP(INT32 k) { UINT8 a[16]; P1(a, k); }
