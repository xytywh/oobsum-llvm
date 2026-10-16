/* cond-write.test 的输入（SPEC-0079）：只在某个返回值下成立的写效应。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulId; UINT32 ulLen; } CTX_S;
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
static UINT32 Init(CTX_S *pst, UINT32 ulReq) { if (ulReq == 0) { return 1; } pst->ulLen = 64; return 0; }
void Ok_TP(void) { UINT8 a[16]; CTX_S st; if (Init(&st, 1) != 0) { return; } Fill(a, &st); }
void Ok_TN(void) { UINT8 a[64]; CTX_S st; if (Init(&st, 1) != 0) { return; } Fill(a, &st); }
/* 对照：调用后先改了字段再判断 → 不写回（用改过的值） */
void Changed(UINT32 n) { UINT8 a[16]; CTX_S st; UINT32 r = Init(&st, 1); st.ulLen = n; if (r != 0) { return; } Fill(a, &st); }
/* 对照：不判断返回值 → 记账 */
void NoCheck_TP(void) { UINT8 a[16]; CTX_S st; (void)Init(&st, 1); Fill(a, &st); }
