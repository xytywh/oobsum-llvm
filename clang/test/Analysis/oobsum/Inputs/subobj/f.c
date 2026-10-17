/* subobject-field.test 的输入（SPEC-0107 §2.1）：指向成员数组的指针先存进结构体字段，再把结构体交给被调者。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; UINT8 aucPad[12]; } CTX_S;
typedef struct { UINT8 *pucBuf; UINT32 ulMax; } VIEW_S;
static void Proc(VIEW_S *pv, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { pv->pucBuf[i] = 0; } }
void Field_Tp(void) { CTX_S st; VIEW_S v; v.pucBuf = st.aucBuf; Proc(&v, 20); }
void Field_Tn(void) { CTX_S st; VIEW_S v; v.pucBuf = st.aucBuf; Proc(&v, 16); }
void Field_Row(void) { UINT8 a[2][4]; VIEW_S v; v.pucBuf = a[0]; Proc(&v, 5); }
