/* subobject-overflow.test 的输入（SPEC-0104 §2.2）：越出成员数组 / 多维数组的一行、没越出整个对象 → SUSPECT subobject_overflow。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
typedef struct { UINT8 aucName[8]; UINT32 ulId; } ENT_S;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);
void Grid_Tp(void) { UINT8 a[2][4]; a[0][5] = 0; }
void Member_Tp(void) { CTX_S st; st.aucBuf[20] = 0; }
void Ents_Tp(void) { ENT_S astEnt[4]; astEnt[0].aucName[8] = 0; }
void Loop_Tp(void) { UINT8 a[2][4]; UINT32 i; for (i = 0; i < 8; i++) { a[0][i] = 0; } }
void Memset_Tp(void) { CTX_S st; memset_s(st.aucBuf, sizeof(st), 0, sizeof(st)); }
void Under_Tp(void) { CTX_S st; st.aucPad[-1] = 0; }
void Step_Tp(void) { CTX_S st; UINT8 *p = st.aucBuf; UINT32 i; for (i = 0; i < 20; i++) { *p = 0; p++; } }
void Grid_Tn(void) { UINT8 a[2][4]; a[1][3] = 0; }
void Member_Tn(void) { CTX_S st; st.aucBuf[15] = 0; }
void Memset_Tn(void) { CTX_S st; memset_s(st.aucBuf, sizeof(st.aucBuf), 0, sizeof(st.aucBuf)); }
void Ptr_Tn(void) { CTX_S st; UINT8 *p; for (p = st.aucBuf; p < st.aucBuf + 16; p++) { *p = 0; } }
void Loop_Tn(void) { UINT8 a[2][4]; UINT32 i, j; for (i = 0; i < 2; i++) { for (j = 0; j < 4; j++) { a[i][j] = 0; } } }
void Outer_Tp(void) { UINT8 a[2][4]; a[2][0] = 0; }
