/* pointer-compare.test 的输入（SPEC-0081 / 0083 / 0084）：同一对象的两个指针比较。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void P1(UINT8 *p, UINT32 n) { UINT8 *e = p + n; while (p < e) { *p = 0; p++; } }
void P1_TP(void) { UINT8 a[16]; P1(a, 17); }
void P1_TN(void) { UINT8 a[16]; P1(a, 16); }
/* 对照：结束指针是另一个形参，两者关系不知道 → 照旧记账 */
static void P3(UINT8 *p, UINT8 *e) { while (p < e) { *p++ = 0; } }
void P3_TP(void) { UINT8 a[16]; P3(a, a + 17); }
/* 元素宽于 1 字节、按元素走：偏移 ≡ 0 mod 4，上界取整到 4n − 4（SPEC-0084）→ 判准 */
static void W1(UINT32 *p, UINT32 n) { UINT32 *e = p + n; while (p < e) { *p = 0; p++; } }
void W1_TP(void) { UINT32 a[4]; W1(a, 5); }
void W1_TN(void) { UINT32 a[4]; W1(a, 4); }
void W1_SAFE(void) { UINT32 a[4]; W1(a, 3); }
/* 元素宽于 1 字节、按 1 字节走（没有同余）：按字节收窄的上界大到 3 字节，不当代码给出的界（SPEC-0083）→ SUSPECT */
static void W2(UINT32 *p, UINT32 n) { UINT32 *e = p + n; while (p < e) { *p = 0; p = (UINT32 *)((UINT8 *)p + 1); } }
void W2_TP(void) { UINT32 a[4]; W2(a, 4); }
