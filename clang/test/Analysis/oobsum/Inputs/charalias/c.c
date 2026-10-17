/* char-alias.test 的输入（SPEC-0103）：选项 CharAlias。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; } CTX_S;
static void Use(UINT8 *buf, UINT32 n) { buf[n] = 0; }
static void F(UINT8 *p, CTX_S *pst) { UINT8 a[4]; pst->ulLen = 2; p[0] = 200; Use(a, pst->ulLen); }
void T(void) { CTX_S st; F((UINT8 *)&st, &st); }
/* 编码函数：先写输出缓冲区，再读输入结构体的长度 */
static void Enc(UINT8 *pucOut, const CTX_S *pstIn, UINT8 *pucDst) { pucOut[0] = 1; pucDst[pstIn->ulLen] = 0; }
void E_TP(void) { UINT8 o[4]; UINT8 a[4]; CTX_S in; in.ulLen = 100; Enc(o, &in, a); }
void E_TN(void) { UINT8 o[4]; UINT8 a[4]; CTX_S in; in.ulLen = 2; Enc(o, &in, a); }
/* 常见写法：循环界每轮从结构体读，循环体经 char 写（打开后界丢掉） */
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
void L_TP(void) { UINT8 a[16]; CTX_S st; st.ulLen = 64; Fill(a, &st); }
