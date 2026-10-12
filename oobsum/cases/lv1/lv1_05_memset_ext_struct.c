/*
 * @id      LV1-05
 * @title   结构体扩展后，清零函数按扩展后的大小 memset_s，传进来的却是老结构体
 * @scene   S1.7  A3·M1·P1·V0·R1·X0·K0·G0
 * @year    承诺
 * @story   UE_CTX_EX_S 在 UE_CTX_S 后面追加了扩展能力。释放流程的最底层统一按 EX 清零，
 *          但有一条老路径传进来的还是 UE_CTX_S。指针在中间层是 void*，类型信息早就丢了。
 * @expect  TP  Ue_OnDetach_TP  pa=L1
 * @expect  TN  Ue_OnDetach_TN  pa=-
 *
 * 摘要
 *   Ue_ClearExt(pCtx)
 *     req R1  W108 @ pCtx + 0       （memset_s 的 destMax = sizeof(UE_CTX_EX_S) = 108）
 *                                   需 108 ≤ ext(pCtx)                                         ★1
 *   Ue_ResetCtx / Ue_Release        req R1 原样上传
 * 串联
 *   Ue_OnDetach_TP：pCtx = &stCtx（UE_CTX_S，40 字节）→ 108 ≤ 40 不成立 → L1，3 层
 *   Ue_OnDetach_TN：&stCtxEx（108 字节）→ 成立
 *   要点：大小按"对象"算，不按指针类型算。void* 一路传下去不影响判定。
 */
#include "../common.h"

typedef struct {
    UINT32 ulUeId;
    UINT32 ulState;
    UINT8  aucCap[32];
} UE_CTX_S;                                                   /* 40 字节 */

typedef struct {
    UE_CTX_S stBase;
    UINT8    aucExtCap[64];
    UINT32   ulExtFlag;
} UE_CTX_EX_S;                                                /* 108 字节 */

static void Ue_ClearExt(void *pCtx)
{
    (void)memset_s(pCtx, sizeof(UE_CTX_EX_S), 0, sizeof(UE_CTX_EX_S));   /* ★1 */
}

static void Ue_ResetCtx(void *pCtx)
{
    Ue_ClearExt(pCtx);
}

static void Ue_Release(void *pCtx, UINT32 ulCause)
{
    LOG_Print(3, "release cause %u", ulCause);
    Ue_ResetCtx(pCtx);
}

void Ue_OnDetach_TP(UINT32 ulCause)
{
    UE_CTX_S stCtx;
    Ue_Release(&stCtx, ulCause);
}

void Ue_OnDetach_TN(UINT32 ulCause)
{
    UE_CTX_EX_S stCtxEx;
    Ue_Release(&stCtxEx, ulCause);
}
