/* mod-ref.test 的输入（SPEC-0045）：被调者只写别的字段 / 全局时，调用后仍用入口值；写到了的照旧失效。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);
typedef struct { UINT32 ulLen; UINT32 ulType; UINT8 aucData[16]; } MSG_S;
static void Buf_Clear(UINT8 *p, UINT32 n) { (void)memset_s(p, n, 0, n); }

/* 1. 被调者只改了 ulType，调用方再按 ulLen 写 */
static void Msg_SetType(MSG_S *p) { p->ulType = 1; }
static void Msg_Fill(MSG_S *p, UINT8 *dst) { Msg_SetType(p); Buf_Clear(dst, p->ulLen); }
void Fill_TP(void) { MSG_S st; UINT8 a[16]; st.ulLen = 32; Msg_Fill(&st, a); }

/* 2. 被调者只改了别的全局 */
static UINT32 g_ulCnt;
static UINT32 g_ulLen;
static void Cnt_Inc(void) { g_ulCnt++; }
static void G_Fill(UINT8 *dst) { Cnt_Inc(); Buf_Clear(dst, g_ulLen); }
void GFill_TP(void) { UINT8 a[16]; if (g_ulLen <= 32) { G_Fill(a); } }

/* 3. 被调者经两层只改了数据区 */
static void Data_Zero(UINT8 *p) { p[0] = 0; }
static void Msg_Zero(MSG_S *p) { Data_Zero(p->aucData); }
static void Msg_Fill2(MSG_S *p, UINT8 *dst) { Msg_Zero(p); Buf_Clear(dst, p->ulLen); }
void Fill2_TP(void) { MSG_S st; UINT8 a[16]; st.ulLen = 32; Msg_Fill2(&st, a); }

/* 对照：经两层写到了 ulLen / g_ulLen——仍不能用入口值 */
static void Len_Set(MSG_S *p) { p->ulLen = 100; }
static void Len_SetVia(MSG_S *p) { Len_Set(p); }
static void Msg_Fill3(MSG_S *p, UINT8 *dst) { Len_SetVia(p); Buf_Clear(dst, p->ulLen); }
void Fill3_TP(void) { MSG_S st; UINT8 a[16]; st.ulLen = 4; Msg_Fill3(&st, a); }
static void GLen_Set(void) { g_ulLen = 100; }
static void GLen_SetVia(void) { GLen_Set(); }
static void G_Fill2(UINT8 *dst) { GLen_SetVia(); Buf_Clear(dst, g_ulLen); }
void GFill2_TP(void) { UINT8 a[16]; if (g_ulLen <= 16) { G_Fill2(a); } }
