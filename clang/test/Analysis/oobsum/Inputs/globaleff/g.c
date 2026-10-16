/* global-effect.test 的输入（SPEC-0077）：被调者写整数全局标量。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
UINT32 g_ulMax;
UINT32 Ext_Get(void);
static void Fill(UINT8 *p) { UINT32 i; for (i = 0; i < g_ulMax; i++) { p[i] = 0; } }
static void Set_Const(void) { g_ulMax = 32; }
static void Set_Arg(UINT32 n) { g_ulMax = n; }
static UINT32 Set_Maybe(UINT32 n) { if (n > 64) { return 1; } g_ulMax = n; return 0; }
static void Set_Then_Call(UINT32 n) { g_ulMax = n; (void)Ext_Get(); }
void Const_TP(void) { UINT8 a[16]; Set_Const(); Fill(a); }
void Arg_TN(void) { UINT8 a[16]; Set_Arg(16); Fill(a); }
void Maybe_TP(void) { UINT8 a[16]; g_ulMax = 0; (void)Set_Maybe(20); Fill(a); }
/* 对照：写完又调用了不知道写什么的函数 → 不记，照旧 global_value */
void Then_TP(void) { UINT8 a[16]; g_ulMax = 0; Set_Then_Call(8); Fill(a); }
