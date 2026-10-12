/*
 * @id      LV3-02
 * @title   校验用接入模块的规格宏，数组用上下文模块的老规格宏
 * @scene   S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2
 * @year    承诺
 * @story   接入模块按"每小区 64 个 UE"校验，上下文模块的数组还是 32。两个宏在不同头文件里。
 * @expect  TP  Ue_OnAttach_TP  pa=L2
 * @expect  TN  Ue_OnAttach_TN  pa=-
 *
 * 摘要
 *   UeCtx_SetCrnti(ulUeIdx, usCrnti)
 *     UE_CTX_S 12 字节，usCrnti 在 +4
 *     req R1  W2 @ g_astUeCtx + 12·ulUeIdx + 4     需 12·ulUeIdx + 6 ≤ 384（ulUeIdx ≤ 31）        ★1
 *   UeCtx_Init / Ue_CreateCtx / Ue_ProcAttach     原样上传（4 层）
 * 串联
 *   Ue_OnAttach_TP：校验 ">= 64 返回" → [0, 63] → 32..63 不满足 → L2，4 层
 *   Ue_OnAttach_TN：校验 ">= MAX_UE_CTX_NUM" → [0, 31] → 成立
 */
#include "../common.h"

#define MAX_UE_PER_CELL  64           /* access_spec.h */
#define MAX_UE_CTX_NUM   32           /* ue_ctx.h（老规格） */

typedef struct {
    UINT32 ulState;
    UINT16 usCrnti;
    UINT16 usRsv;
    UINT32 ulTimer;
} UE_CTX_S;

static UE_CTX_S g_astUeCtx[MAX_UE_CTX_NUM];

static void UeCtx_SetCrnti(UINT32 ulUeIdx, UINT16 usCrnti)
{
    g_astUeCtx[ulUeIdx].usCrnti = usCrnti;                   /* ★1 */
}

static void UeCtx_Init(UINT32 ulUeIdx, UINT16 usCrnti)    { UeCtx_SetCrnti(ulUeIdx, usCrnti); }
static void Ue_CreateCtx(UINT32 ulUeIdx, UINT16 usCrnti)  { UeCtx_Init(ulUeIdx, usCrnti); }
static void Ue_ProcAttach(UINT32 ulUeIdx, UINT16 usCrnti) { Ue_CreateCtx(ulUeIdx, usCrnti); }

UINT32 Ue_OnAttach_TP(UINT32 ulUeIdx, UINT16 usCrnti)
{
    if (ulUeIdx >= MAX_UE_PER_CELL) {
        return VOS_ERR;
    }
    Ue_ProcAttach(ulUeIdx, usCrnti);
    return VOS_OK;
}

UINT32 Ue_OnAttach_TN(UINT32 ulUeIdx, UINT16 usCrnti)
{
    if (ulUeIdx >= MAX_UE_CTX_NUM) {
        return VOS_ERR;
    }
    Ue_ProcAttach(ulUeIdx, usCrnti);
    return VOS_OK;
}
