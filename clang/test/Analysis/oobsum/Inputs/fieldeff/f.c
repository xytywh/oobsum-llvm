/* field-effect.test 的输入（SPEC-0073）：被调者写形参所指对象的整数字段。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulId; UINT32 ulLen; } CTX_S;
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
static void Fill2(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { p[i] = 0; } }
static void Cfg_Init(CTX_S *pst) { pst->ulLen = 64; }
void Init_TP(void) { UINT8 a[16]; CTX_S st; Cfg_Init(&st); Fill(a, &st); }
static void Cfg_Set(CTX_S *pst, UINT32 n) { pst->ulLen = n; }
void Set_TP(void) { UINT8 a[16]; CTX_S st; Cfg_Set(&st, 20); Fill(a, &st); }
void Set_TN(void) { UINT8 a[16]; CTX_S st; Cfg_Set(&st, 16); Fill(a, &st); }
/* 出错路径没写：调用方取旧值与新值的并 */
static UINT32 Get_Len(UINT32 *pulLen, UINT32 ulMax) { if (ulMax < 8) { return 1; } *pulLen = 32; return 0; }
void Len_TP(void) { UINT8 a[16]; UINT32 n = 0; (void)Get_Len(&n, 16); Fill2(a, n); }
void Len_TN(void) { UINT8 a[32]; UINT32 n = 0; (void)Get_Len(&n, 16); Fill2(a, n); }
/* 对照：两条路径写不同的值 → 不记；写了不确切的值 → 不记 */
static void Two(CTX_S *pst, UINT32 c) { if (c) { pst->ulLen = 32; } else { pst->ulLen = 8; } }
void Two_TP(UINT32 c) { UINT8 a[16]; CTX_S st; Two(&st, c); Fill(a, &st); }
UINT32 Ext_Get(void);
static void Ext(CTX_S *pst) { pst->ulLen = Ext_Get(); }
void Ext_TP(void) { UINT8 a[16]; CTX_S st; Ext(&st); Fill(a, &st); }
