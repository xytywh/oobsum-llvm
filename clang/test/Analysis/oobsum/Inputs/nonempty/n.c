/* range-nonempty.test 的输入（SPEC-0098）：条件边收窄出的区间非空记成守卫。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Wn(UINT8 *p, UINT32 n) { while (n--) { p[n + 10] = 0; } }
void M3_TN(void) { UINT8 a[4]; Wn(a, 0); }
void M3_TP(void) { UINT8 a[4]; Wn(a, 1); }
static void Dn(UINT8 *p, UINT32 n) { UINT32 i; for (i = n; i != 0; i--) { p[i + 10] = 0; } }
void M6_TN(void) { UINT8 a[4]; Dn(a, 0); }
void M6_TP(void) { UINT8 a[4]; Dn(a, 2); }
