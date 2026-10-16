/* pointer-compare.test 的输入（SPEC-0081）：同一对象的两个指针比较。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void P1(UINT8 *p, UINT32 n) { UINT8 *e = p + n; while (p < e) { *p = 0; p++; } }
void P1_TP(void) { UINT8 a[16]; P1(a, 17); }
void P1_TN(void) { UINT8 a[16]; P1(a, 16); }
/* 对照：结束指针是另一个形参，两者关系不知道 → 照旧记账 */
static void P3(UINT8 *p, UINT8 *e) { while (p < e) { *p++ = 0; } }
void P3_TP(void) { UINT8 a[16]; P3(a, a + 17); }
