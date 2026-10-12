/*
 * @id      LV6-05
 * @title   全局指针在初始化时指向静态内存池的尾部 64 字节，三层之下按 64 写结尾
 * @scene   S6.3 变体  A1·M2·P4·V1·R1·X1·K0·G0
 * @year    2027H1
 * @story   共享区指针 g_pucShm 只在 Shm_Init 里赋值一次（池的最后 64 字节）。
 * @expect  TP  Om_SaveTrace_TP  pa=记账  later=L1
 * @expect  TN  Om_SaveTrace_TN  pa=记账  later=-
 *
 * 摘要
 *   Shm_Init()                  eff g_pucShm := &g_aucShmPool[192]
 *   Shm_WriteAt(ulPos)          req R1  W1 @ S + ulPos     需 ulPos + 1 ≤ ext(S)    （S = g_pucShm 的入口值）  ★1
 *   Shm_Put / Trace_Save        原样上传
 * 串联
 *   年内：S 指向哪里不知道 → 记账"全局指针目标未知"
 *   2027：全局指针的流不敏感指向 {NULL, g_aucShmPool + 192} → ext(S) = 64 → TP：ulPos = 64 → 65 ≤ 64 → L1
 */
#include "../common.h"

#define SHM_POOL_LEN  256
#define SHM_TRACE_OFF 192
#define SHM_TRACE_LEN 64

static UINT8  g_aucShmPool[SHM_POOL_LEN];
static UINT8 *g_pucShm;

void Shm_Init(void) { g_pucShm = &g_aucShmPool[SHM_TRACE_OFF]; }

static void Shm_WriteAt(UINT32 ulPos) { g_pucShm[ulPos] = 0; }        /* ★1 */
static void Shm_Put(UINT32 ulPos)     { Shm_WriteAt(ulPos); }
static void Trace_Save(UINT32 ulPos)  { Shm_Put(ulPos); }

void Om_SaveTrace_TP(void) { Trace_Save(SHM_TRACE_LEN); }
void Om_SaveTrace_TN(void) { Trace_Save(SHM_TRACE_LEN - 1); }
