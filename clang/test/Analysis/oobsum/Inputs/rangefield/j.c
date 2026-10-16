/* range-field.test 的输入（SPEC-0069）：实参所指结构体的字段值是区间。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; } CTX_S;
UINT32 Ext_Get(void);
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
void Br_TP(UINT32 n) { UINT8 a[16]; CTX_S s; s.ulLen = 5; if (n) { s.ulLen = 100; } Fill(a, &s); }
void BrU_TP(UINT32 n) { UINT8 a[16]; CTX_S s; s.ulLen = 5; if (n) { s.ulLen = Ext_Get(); } Fill(a, &s); }
void Loop_TP(UINT32 n) { UINT8 a[16]; CTX_S s; UINT32 k; s.ulLen = 5; for (k = 0; k < n; k++) { if (k == 7) { s.ulLen = 100; } } Fill(a, &s); }
void LoopInc_TP(UINT32 n) { UINT8 a[16]; CTX_S s; UINT32 k; s.ulLen = 5; for (k = 0; k < n; k++) { s.ulLen++; } Fill(a, &s); }
void Sw_TP(UINT32 t) { UINT8 a[16]; CTX_S s; switch (t) { case 1: s.ulLen = 100; break; default: s.ulLen = 4; break; } Fill(a, &s); }
void Ok_TN(UINT32 n) { UINT8 a[16]; CTX_S s; s.ulLen = 5; if (n) { s.ulLen = 16; } Fill(a, &s); }
