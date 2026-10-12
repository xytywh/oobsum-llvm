/*
 * @id      LV3-10
 * @title   校验之后又改了下标："取下一个位置"没再校验
 * @scene   S3.1  A1·M2·P0·V5→G2·R1·X1·K1·G2
 * @year    承诺
 * @story   切换流程先校验源侧下标，再按"源 + 1"取目标侧预留位置。校验只覆盖了源。
 * @expect  TP  Ue_OnHoPrepare_TP  pa=L2
 * @expect  TN  Ue_OnHoPrepare_TN  pa=-
 *
 * 摘要
 *   Ho_MarkSlot(ulSlot)
 *     req R1  W4 @ g_aulHoSlot + 4·ulSlot     需 4·ulSlot + 4 ≤ 128（ulSlot ≤ 31）                 ★1
 *   Ho_Reserve / Ue_PrepareHo               原样上传
 * 串联
 *   Ue_OnHoPrepare_TP：校验后 ulIdx ∈ [0, 31]；ulNext = ulIdx + 1 ∈ [1, 32] → L2
 *                      （流敏感：校验收窄的是 ulIdx，ulNext 是它 + 1 之后的新值，区间跟着平移）
 *   Ue_OnHoPrepare_TN：校验写成 ulIdx + 1 >= 32 → ulIdx ∈ [0, 30] → ulNext ∈ [1, 31] 成立
 */
#include "../common.h"

#define MAX_HO_SLOT 32

static UINT32 g_aulHoSlot[MAX_HO_SLOT];

static void Ho_MarkSlot(UINT32 ulSlot)
{
    g_aulHoSlot[ulSlot] = 1;                                  /* ★1 */
}

static void Ho_Reserve(UINT32 ulSlot)    { Ho_MarkSlot(ulSlot); }
static void Ue_PrepareHo(UINT32 ulSlot)  { Ho_Reserve(ulSlot); }

UINT32 Ue_OnHoPrepare_TP(UINT32 ulIdx)
{
    UINT32 ulNext;
    if (ulIdx >= MAX_HO_SLOT) {
        return VOS_ERR;
    }
    ulNext = ulIdx + 1;
    Ue_PrepareHo(ulNext);
    return VOS_OK;
}

UINT32 Ue_OnHoPrepare_TN(UINT32 ulIdx)
{
    UINT32 ulNext;
    if (ulIdx + 1 >= MAX_HO_SLOT) {
        return VOS_ERR;
    }
    ulNext = ulIdx + 1;
    Ue_PrepareHo(ulNext);
    return VOS_OK;
}
