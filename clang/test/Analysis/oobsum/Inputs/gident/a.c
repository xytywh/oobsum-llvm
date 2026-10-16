/* global-identity.test 的输入（SPEC-0086）：a.c 的 static g_len 与 b.c 的同名 static 是两个对象。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static UINT32 g_len = 100;
void PutA(UINT8 *p) { p[g_len] = 0; }
/* 对照：同一个 TU 里改的是同一个 g_len → 照旧按值判 */
void TopA_TN(void) { UINT8 a[4]; g_len = 2; PutA(a); }
void TopA_TP(void) { UINT8 a[4]; g_len = 10; PutA(a); }
