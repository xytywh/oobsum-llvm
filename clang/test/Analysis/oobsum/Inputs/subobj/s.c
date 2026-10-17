/* fir-subobj.test 的输入（SPEC-0104）：数组子对象的地址带 subobj(…, 大小)。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
typedef struct { UINT8 aucName[8]; UINT32 ulId; } ENT_S;
typedef struct { UINT32 ulNum; UINT8 aucData[]; } FLEX_S;
void Grid(UINT32 j) { UINT8 a[2][4]; a[0][j] = 0; a[j][1] = 1; }
void Member(CTX_S *pst, UINT32 k) { CTX_S st; st.aucBuf[k] = 0; pst->aucPad[k] = 0; }
void Ents(UINT32 k) { ENT_S astEnt[4]; astEnt[0].aucName[k] = 0; }
void Whole(FLEX_S *pf, UINT32 k) { UINT8 b[8]; b[k] = 0; pf->aucData[k] = 0; }
