/* struct-copy-fields.test 的输入（SPEC-0088）：结构体整体赋值按字段拷值。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 len; UINT8 *buf; UINT32 cap; } MSG_S;
static void UseLen(UINT8 *p, const MSG_S *m) { p[m->len] = 0; }
void C1_TP(void) { UINT8 a[4]; MSG_S x, y; x.len = 100; y = x; UseLen(a, &y); }
void C1_TN(void) { UINT8 a[4]; MSG_S x, y; x.len = 2; y = x; UseLen(a, &y); }
/* 拷完再改源：目标不变 */
void C2_TN(void) { UINT8 a[4]; MSG_S x, y; x.len = 2; y = x; x.len = 100; UseLen(a, &y); }
/* 拷到形参：写效应 */
static void Out(MSG_S *pOut) { MSG_S st; st.len = 100; st.cap = 1; *pOut = st; }
void C3_TP(void) { UINT8 a[4]; MSG_S m; Out(&m); UseLen(a, &m); }
/* 从形参拷：字段是 p0->len */
static void In(UINT8 *p, const MSG_S *pIn) { MSG_S st; st = *pIn; p[st.len] = 0; }
void C4_TP(void) { UINT8 a[4]; MSG_S m; m.len = 100; In(a, &m); }
void C4_TN(void) { UINT8 a[4]; MSG_S m; m.len = 3; In(a, &m); }
/* 指针字段经拷贝 */
static void UseBuf(const MSG_S *m) { m->buf[8] = 0; }
void C5_TP(void) { UINT8 a[4]; MSG_S x, y; x.buf = a; y = x; UseBuf(&y); }
