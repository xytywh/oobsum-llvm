/* subobject-back.test 的输入（SPEC-0107 §2.2 第 3 步）：负偏移越到成员数组 / 一行的前面、还在整个对象里。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
static void Prev(UINT8 *p) { p[-1] = 0; }
void Back_Tp(void) { CTX_S st; Prev(st.aucBuf); }
void Back_Tn(void) { CTX_S st; Prev(st.aucBuf + 1); }
void Back_Row(void) { UINT8 a[2][4]; Prev(a[1]); }
static void SetPrev(CTX_S *pst, int i) { pst->aucPad[i - 1] = 0; }
void Sym_Tp(void) { CTX_S st; SetPrev(&st, 0); }
void Sym_Tn(void) { CTX_S st; SetPrev(&st, 1); }
static void SetRow(int i) { UINT8 a[2][4]; a[1][i - 1] = 0; }
void Row_Tp(void) { SetRow(0); }
void Row_Tn(void) { SetRow(1); }
