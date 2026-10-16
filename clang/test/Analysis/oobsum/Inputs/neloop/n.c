/* ne-loop.test 的输入（SPEC-0082）：!= 结束的循环。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void I1(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i != n; i++) { p[i] = 0; } }
void I1_TP(void) { UINT8 a[16]; I1(a, 17); }
void I1_TN(void) { UINT8 a[16]; I1(a, 16); }
static void P2(UINT8 *p, UINT32 n) { UINT8 *q; for (q = p; q != p + n; q++) { *q = 0; } }
void P2_TP(void) { UINT8 a[16]; P2(a, 17); }
void P2_TN(void) { UINT8 a[16]; P2(a, 16); }
/* 对照：步长 2（会跳过 n）→ 照旧记账 */
static void S2(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i != n; i += 2) { p[i] = 0; } }
void S2_TP(void) { UINT8 a[16]; S2(a, 18); }
/* 对照：起点证明不了 <= n（i 会回绕）→ 照旧记账 */
static void T1(UINT8 *p, UINT32 n) { UINT32 i; for (i = 5; i != n; i++) { p[i] = 0; } }
void T1_TP(void) { UINT8 a[16]; T1(a, 3); }
