/* callee-field-ret.test 的输入（SPEC-0072）：被调者返回字段值。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; } CTX_S;
static UINT32 GetLen(const CTX_S *c) { return c->ulLen; }
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
void Top_TP(CTX_S *pstMine) { UINT8 a[16]; CTX_S st; st.ulLen = 100; if (pstMine->ulLen > 10) { return; } Put(a, GetLen(&st)); }
void Own_TN(CTX_S *pstMine) { UINT8 a[16]; if (pstMine->ulLen > 10) { return; } Put(a, GetLen(pstMine)); }
