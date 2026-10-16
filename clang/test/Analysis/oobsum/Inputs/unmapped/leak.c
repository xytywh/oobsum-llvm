/* unmapped-symbol.test 的输入（SPEC-0063）：被调者的字段符号没映射时不能借用调用方同名形参的字段（原来 Top 上判成 SAFE）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulId; UINT8 *pucRes; } RES_S;
UINT8 *Ext_GetSmall(void);   /* 外部：返回多大不知道 */

static void Res_Write(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Res_Fill(RES_S *pst, UINT32 i) { Res_Write(pst->pucRes, i); }
static void Mid(RES_S *pstOuter, UINT32 i) { RES_S st; st.ulId = pstOuter->ulId; st.pucRes = Ext_GetSmall(); Res_Fill(&st, i); }
void Top(void) { UINT8 aucBig[128]; RES_S outer; outer.ulId = 1; outer.pucRes = aucBig; Mid(&outer, 100); }
