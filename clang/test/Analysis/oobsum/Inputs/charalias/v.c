/* char-alias-verify.test 的输入（SPEC-0106）：CharAlias=verify——照 false 推，记下不别名假设，调用点核对。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; } CTX_S;
static CTX_S g_stCtx;
static void Use(UINT8 *buf, UINT32 n) { buf[n] = 0; }
/* 经 p（char）写之后读 pst->ulLen：假设 p 与 pst 不重叠 */
static void F(UINT8 *p, CTX_S *pst) { UINT8 a[4]; pst->ulLen = 2; p[0] = 200; Use(a, pst->ulLen); }
void T_Same(void) { CTX_S st; F((UINT8 *)&st, &st); }
void T_Diff(void) { CTX_S st; UINT8 b[4]; F(b, &st); }
/* 循环界每轮从结构体读：无别名时界不丢 */
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
void L_TP(void) { UINT8 a[16]; CTX_S st; st.ulLen = 64; Fill(a, &st); }
void L_TN(void) { UINT8 a[16]; CTX_S st; st.ulLen = 16; Fill(a, &st); }
/* 经中间函数：假设换成 Mid 的形参往上传 */
static void Mid(UINT8 *p, CTX_S *pst) { F(p, pst); }
void M_Diff(void) { CTX_S st; UINT8 b[4]; Mid(b, &st); }
void M_Same(void) { CTX_S st; Mid((UINT8 *)&st, &st); }
/* 经全局：经 p 写之后读 g_stCtx.ulLen */
static void H(UINT8 *p) { UINT8 a[4]; g_stCtx.ulLen = 2; p[0] = 200; Use(a, g_stCtx.ulLen); }
void H_Diff(void) { UINT8 b[4]; H(b); }
void H_Same(void) { H((UINT8 *)&g_stCtx); }
/* 入口自己的形参：核对不了 */
void G(UINT8 *p, CTX_S *pst) { UINT8 a[4]; p[0] = 1; Use(a, pst->ulLen); }
