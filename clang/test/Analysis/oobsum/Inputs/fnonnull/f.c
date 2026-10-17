/* field-nonnull.test 的输入（SPEC-0101）：指针字段判空产生守卫 nn:p<k>->f。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
#define NULL ((void *)0)
extern int memset_s(void *d, UINT32 dm, int c, UINT32 n);
typedef struct { UINT32 ulLen; UINT8 *pucBuf; } CTX_S;
static void Clr(CTX_S *pst) { if (pst->pucBuf != NULL) { (void)memset_s(pst->pucBuf, 100, 0, 100); } }
void F1(void) { CTX_S st; st.ulLen = 0; st.pucBuf = NULL; Clr(&st); }
void F2(void) { UINT8 a[4]; CTX_S st; st.ulLen = 0; st.pucBuf = a; Clr(&st); }
static void MidC(CTX_S *pst) { Clr(pst); }
void F3(void) { CTX_S st; st.ulLen = 0; st.pucBuf = NULL; MidC(&st); }
void F4(void) { UINT8 a[4]; CTX_S st; st.ulLen = 0; st.pucBuf = a; MidC(&st); }
