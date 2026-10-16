/* field-current.test 的输入（SPEC-0040）：形参所指字段改过之后不能再用入口值。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; UINT8 aucBuf[16]; } S;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);
static void Buf_Clear(UINT8 *p, UINT32 n) { (void)memset_s(p, n, 0, n); }

/* A. 被调者改了形参所指的字段，之后再读 */
static void Set100(S *p) { p->ulLen = 100; }
static void MidA(S *p) { UINT8 auc[16]; Set100(p); Buf_Clear(auc, p->ulLen); }
void TopA_TP(void) { S st; st.ulLen = 4; MidA(&st); }

/* B. 被调者改了全局，之后再读（不是入口） */
static UINT32 g_ulLen;
static void GSet100(void) { g_ulLen = 100; }
static void MidB(void) { UINT8 auc[16]; GSet100(); Buf_Clear(auc, g_ulLen); }
void TopB_TP(void) { g_ulLen = 4; MidB(); }

/* C. 本函数经偏移不定的写改了形参所指的结构体 */
static void MidC(S *p, UINT32 i) { UINT8 auc[16]; ((UINT32 *)p)[i] = 100; Buf_Clear(auc, p->ulLen); }
void TopC_TP(void) { S st; st.ulLen = 4; MidC(&st, 0); }

/* D. 两个同类型形参别名 */
static void MidD(S *a, S *b) { a->ulLen = 4; b->ulLen = 100; Buf_Clear(a->aucBuf, a->ulLen); }
void TopD_TP(void) { S st; MidD(&st, &st); }

/* 对照 E：调用之前读的字段仍是入口值，推到调用方判出越界 */
static void MidE(S *p) { UINT8 auc[16]; UINT32 n = p->ulLen; Set100(p); Buf_Clear(auc, n); }
void TopE_TP(void) { S st; st.ulLen = 32; MidE(&st); }

/* 对照 F：只读的形参 */
static void MidF(const S *p) { UINT8 auc[16]; Buf_Clear(auc, p->ulLen); }
void TopF_TP(void) { S st; st.ulLen = 32; MidF(&st); }

/* 对照 G：不同类型的两个形参，写 out 之后读 in 的长度 */
typedef struct { UINT32 ulType; UINT32 ulUsed; } T;
static void MidG(T *out, const S *in) { UINT8 auc[16]; out->ulUsed = 0; Buf_Clear(auc, in->ulLen); }
void TopG_TP(void) { T t; S st; st.ulLen = 32; MidG(&t, &st); }
