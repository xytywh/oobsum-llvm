/* global-current.test 的输入（SPEC-0041）：调用过可能写全局的函数之后，全局标量不是入口值。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);
void LOG_Print(const char *fmt, ...);

static UINT32 g_ulIdx;
static UINT8 g_aucTbl[16];

/* 被调者把下标加 8，调用方之前检查过 g_ulIdx < 16（原来是错误的 SAFE） */
static void Idx_Inc(void) { g_ulIdx += 8; }
static void Tbl_Set(void) { Idx_Inc(); g_aucTbl[g_ulIdx] = 1; }
void Tbl_TP(void) { if (g_ulIdx < 16) { Tbl_Set(); } }

/* 对照：调用之前读的下标；只调用纯函数、访问类模型之后读的下标——仍是入口值，被调用方的检查证明安全 */
static void Tbl_SetFirst(void) { UINT32 i = g_ulIdx; Idx_Inc(); g_aucTbl[i] = 1; }
void TblFirst_OK(void) { if (g_ulIdx < 16) { Tbl_SetFirst(); } }
static void Tbl_SetLog(void) { UINT8 auc[4]; LOG_Print("x"); (void)memset_s(auc, 4, 0, 4); g_aucTbl[g_ulIdx] = 1; }
void TblLog_OK(void) { if (g_ulIdx < 16) { Tbl_SetLog(); } }
