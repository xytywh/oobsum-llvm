/* effect-param.test 的输入（SPEC-0093）：写效应的值就是被调者的形参时按实参的值写（实参是区间也行）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 len; } S_S;
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
static void SetLen(S_S *s, UINT32 n) { s->len = n; }
void E4_TP(UINT32 k) { UINT8 x[4]; S_S s; SetLen(&s, k < 3 ? 2 : 100); Put(x, s.len); }
void E4_TN(UINT32 k) { UINT8 x[4]; S_S s; SetLen(&s, k < 3 ? 1 : 2); Put(x, s.len); }
static UINT32 g_n;
static void SetN(UINT32 n) { g_n = n; }
void G_TP(UINT32 k) { UINT8 x[4]; SetN(k < 3 ? 2 : 100); Put(x, g_n); }
void G_TN(UINT32 k) { UINT8 x[4]; SetN(k < 3 ? 1 : 2); Put(x, g_n); }
static void SetLen4(S_S *s, UINT32 n) { s->len = n + 4; }
void E5(UINT32 k) { UINT8 x[4]; S_S s; SetLen4(&s, k < 3 ? 1 : 2); Put(x, s.len); }
