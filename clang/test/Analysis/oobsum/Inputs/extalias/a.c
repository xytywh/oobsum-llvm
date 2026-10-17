/* external-alias.test 的输入（SPEC-0091）：外部内存（形参所指、指针字段所指、全局）按类型互相失效。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 len; UINT8 *buf; } MSG_S;
static void UseIdx(UINT8 *p, UINT8 i) { p[i] = 0; }
static UINT8 g_idx[4];
/* 1 形参 p 可能就是 g_idx */
static void A1(UINT8 *p) { UINT8 a[4]; g_idx[1] = 2; p[1] = 200; UseIdx(a, g_idx[1]); }
void Top1(void) { A1(g_idx); }
/* 2 形参 p 可能就是 m->buf 所指 */
static void A2(UINT8 *p, MSG_S *m) { UINT8 a[4]; m->buf[1] = 2; p[1] = 200; UseIdx(a, m->buf[1]); }
void Top2(void) { UINT8 b[4]; MSG_S m; m.buf = b; A2(b, &m); }
/* 3 全局指针与形参 */
static UINT8 *g_p;
static void A3(UINT8 *p) { UINT8 a[4]; g_p[1] = 2; p[1] = 200; UseIdx(a, g_p[1]); }
void Top3(void) { UINT8 b[4]; g_p = b; A3(b); }
/* 4 两个不同类型的形参指针（UINT8 * 与 UINT32 *）指向同一块 */
static void A4(UINT8 *p, UINT32 *q) { UINT8 a[4]; p[0] = 2; *q = 0xC8C8C8C8; UseIdx(a, p[0]); }
void Top4(void) { UINT32 w; A4((UINT8 *)&w, &w); }
/* 对照：char 输出缓冲区写之后读结构体形参的长度照旧按入口值 */
typedef struct { UINT32 ulLen; } IN_S;
static void Enc(UINT8 *pucOut, const IN_S *pstIn, UINT8 *pucDst) { pucOut[0] = 1; pucDst[pstIn->ulLen] = 0; }
void Top5_TP(void) { UINT8 o[4]; UINT8 a[4]; IN_S in; in.ulLen = 100; Enc(o, &in, a); }
void Top5_TN(void) { UINT8 o[4]; UINT8 a[4]; IN_S in; in.ulLen = 2; Enc(o, &in, a); }
/* 对照：经 UINT32 * 写之后读 UINT8 全局数组照旧（类型不同的全局不算） */
static UINT8 g_tbl[4];
static void A6(UINT32 *q) { UINT8 a[4]; g_tbl[1] = 2; *q = 0xC8C8C8C8; UseIdx(a, g_tbl[1]); }
void Top6(void) { UINT32 w; A6(&w); }
