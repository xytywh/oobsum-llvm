/* ret-ptr.test 的输入（SPEC-0070）：被调者返回实参指针（或它所指结构体的指针字段）加偏移。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulId; UINT8 *pucBuf; } CTX_S;
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
static UINT8 *Msg_Body(UINT8 *pucMsg) { return pucMsg + 4; }
void Body_TP(void) { UINT8 a[16]; UINT8 *q = Msg_Body(a); Put(q, 12); }
void Body_TN(void) { UINT8 a[16]; UINT8 *q = Msg_Body(a); Put(q, 11); }
static UINT8 *Ctx_Tail(const CTX_S *pstCtx) { return pstCtx->pucBuf + 4; }
void Tail_TP(void) { UINT8 a[16]; CTX_S st; st.ulId = 0; st.pucBuf = a; Put(Ctx_Tail(&st), 12); }
static UINT8 *Wrap(UINT8 *p) { return Msg_Body(p) + 2; }
void Wrap_TP(void) { UINT8 a[16]; Put(Wrap(a), 10); }
/* 对照：两条返回偏移不同 → 没有效果、记账 */
static UINT8 *Pick(UINT8 *p, UINT32 c) { if (c) { return p + 2; } return p + 6; }
void Pick_TP(UINT32 c) { UINT8 a[16]; Put(Pick(a, c), 12); }
