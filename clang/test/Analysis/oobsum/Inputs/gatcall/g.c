/* global-at-call.test 的输入（SPEC-0064）：调用前改过的全局，被调者按全局判界。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
UINT32 g_ulLen;

static void Fill(UINT8 *p) { UINT32 i; for (i = 0; i < g_ulLen; i++) { p[i] = 0; } }
void Write_TP(void) { UINT8 a[16]; if (g_ulLen > 16) { return; } g_ulLen = 100; Fill(a); }
void Guard_TN(void) { UINT8 a[16]; if (g_ulLen > 16) { return; } Fill(a); }
static void SetLen(UINT32 n) { g_ulLen = n; }
void Callee_TP(void) { UINT8 a[16]; if (g_ulLen > 16) { return; } SetLen(100); Fill(a); }
