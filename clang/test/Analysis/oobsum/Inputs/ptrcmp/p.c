/* pointer-compare.test 的输入（SPEC-0081 / 0083）：同一对象的两个指针比较。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void P1(UINT8 *p, UINT32 n) { UINT8 *e = p + n; while (p < e) { *p = 0; p++; } }
void P1_TP(void) { UINT8 a[16]; P1(a, 17); }
void P1_TN(void) { UINT8 a[16]; P1(a, 16); }
/* 对照：结束指针是另一个形参，两者关系不知道 → 照旧记账 */
static void P3(UINT8 *p, UINT8 *e) { while (p < e) { *p++ = 0; } }
void P3_TP(void) { UINT8 a[16]; P3(a, a + 17); }
/* 元素宽于 1 字节：按字节收窄的上界大到 3 字节，不当代码给出的界（SPEC-0083）→ 超出时 SUSPECT，没超出时 SAFE */
static void W1(UINT32 *p, UINT32 n) { UINT32 *e = p + n; while (p < e) { *p = 0; p++; } }
void W1_TP(void) { UINT32 a[4]; W1(a, 5); }
void W1_TN(void) { UINT32 a[4]; W1(a, 4); }
void W1_SAFE(void) { UINT32 a[4]; W1(a, 3); }
