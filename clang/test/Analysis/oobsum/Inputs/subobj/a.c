/* subobject-arg.test 的输入（SPEC-0104 §2.3 3b）：把指向成员数组 / 二维数组一行的指针交给被调者，内层也查。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
static void Fill(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { p[i] = 0; } }
static void Mid(UINT8 *p, UINT32 n) { Fill(p, n); }
void Arg_Tp(void) { CTX_S st; Fill(st.aucBuf, 20); }
void Arg_Tn(void) { CTX_S st; Fill(st.aucBuf, 16); }
void Arg_Outer(void) { CTX_S st; Fill(st.aucBuf, 40); }
void Row_Tp(void) { UINT8 a[2][4]; Fill(a[0], 5); }
void Mid_Tp(void) { CTX_S st; Mid(st.aucBuf, 20); }
void Mid_Tn(void) { CTX_S st; Mid(st.aucBuf, 16); }
