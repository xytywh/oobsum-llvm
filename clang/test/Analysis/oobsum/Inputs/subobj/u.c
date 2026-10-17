/* subobject-req.test 的输入（SPEC-0104 §2.3 3a）：下标是形参的内层检查记成内层 Req（dir sub），调用方给值后再判。 */
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef struct { UINT16 usX; UINT16 ausY[2]; } IN_S;
typedef struct { UINT32 ulLen; IN_S stIn; UINT8 aucBuf[16]; } CTX_S;
static UINT8 g_aucTbl[4][8];
static void Mark(UINT32 s, UINT32 r) { g_aucTbl[s][r] = 1; }
static void SetY(CTX_S *pst, UINT32 i) { pst->stIn.ausY[i] = 1; }
void Mark_Tp(void) { Mark(1, 8); }
void Mark_Tn(void) { Mark(1, 7); }
void Mark_Outer(void) { Mark(4, 0); }
void SetY_Tp(void) { CTX_S st; SetY(&st, 2); }
void SetY_Tn(void) { CTX_S st; SetY(&st, 1); }
void Mark_Entry(UINT32 r) { g_aucTbl[0][r] = 1; }
