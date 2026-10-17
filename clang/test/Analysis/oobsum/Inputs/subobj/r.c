/* subobject-ret.test 的输入（SPEC-0107 §2.2）：返回指向成员数组 / 一行的指针，调用方的访问也查内层界。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
static UINT8 *GetBuf(CTX_S *p) { return p->aucBuf; }
static UINT8 *GetRow(UINT8 (*a)[4], UINT32 r) { return a[r]; }
void Ret_Tp(void) { CTX_S st; UINT8 *q = GetBuf(&st); q[20] = 0; }
void Ret_Tn(void) { CTX_S st; UINT8 *q = GetBuf(&st); q[15] = 0; }
void Row_Tp(void) { UINT8 a[2][4]; UINT8 *q = GetRow(a, 0); q[5] = 0; }
static UINT8 *Next(UINT8 *p) { return p + 1; }
void Next_Tp(void) { CTX_S st; UINT8 *q = Next(st.aucBuf); q[15] = 0; }
void Next_Tn(void) { CTX_S st; UINT8 *q = Next(st.aucBuf); q[14] = 0; }
