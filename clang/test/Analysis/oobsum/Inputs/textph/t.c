/* text-placeholders.test 的输入（SPEC-0068）：字符串常量、调用点上不知道的字段值。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; } CTX_S;
UINT32 Ext_Get(void);
static void Rd(const char *s, UINT32 i) { volatile char c = s[i]; (void)c; }
void Str_TP(void) { Rd("abc", 4); }
static void Fill(UINT8 *p, CTX_S *pstCtx) { UINT32 i; for (i = 0; i < pstCtx->ulLen; i++) { p[i] = 0; } }
void Field_TP(CTX_S *pstCtx) { UINT8 a[16]; pstCtx->ulLen = Ext_Get(); Fill(a, pstCtx); }
