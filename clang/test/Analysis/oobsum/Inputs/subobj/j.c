/* subobject-join.test 的输入（SPEC-0107 §2.2 第 4 步）：两条路上的窗口对不上，汇合时都留下。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
void Mix_Tp(int c) { CTX_S st; UINT8 *p = c ? st.aucBuf : (UINT8 *)&st; p[20] = 0; }
void Mix_Tn(int c) { CTX_S st; UINT8 *p = c ? st.aucBuf : (UINT8 *)&st; p[15] = 0; }
void Two_Tp(int c) { CTX_S st; UINT8 *p = c ? st.aucBuf : st.aucPad; p[13] = 0; }
void Two_Tn(int c) { CTX_S st; UINT8 *p = c ? st.aucBuf : st.aucPad; p[11] = 0; }
