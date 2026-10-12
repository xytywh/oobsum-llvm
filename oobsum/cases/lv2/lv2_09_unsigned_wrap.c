/*
 * @id      LV2-09
 * @title   无符号下标减一回绕：pos 为 0 时取"前一个"
 * @scene   S2.2  A1·M1·P1·V1·R2·X1·K0·G0/G1
 * @year    承诺
 * @story   历史记录取前一项；有一个调用方在初始化时传了 0。UINT32 的 0 − 1 是 0xFFFFFFFF。
 * @expect  TP  Ue_CheckJitter_TP  pa=L1
 * @expect  TN  Ue_CheckJitter_TN  pa=-
 *
 * 摘要
 *   Hist_GetPrev(pucHist, ulPos)
 *     偏移是无符号减法 (ulPos − 1) mod 2^32，BoundExpr 记成带回绕标记的线性式
 *     req R1  R1 @ pucHist + (ulPos − 1)ᵘ      需 (ulPos − 1)ᵘ + 1 ≤ ext(pucHist)                ★1
 *   Hist_GetPrevSafe(pucHist, ulPos)
 *     本函数先判 ulPos == 0 就返回 → 条件带守卫：
 *     req R1' ulPos ≥ 1 ⇒ R1 @ pucHist + ulPos − 1     需 ulPos ≤ ext(pucHist)
 *   Ue_CalcJitter / Ue_UpdateJitter     原样上传
 * 串联
 *   Ue_CheckJitter_TP：ulPos = 0 → 偏移 0xFFFFFFFF → L1
 *   Ue_CheckJitter_TN：走 Safe 版本，ulPos = 0 → 守卫为假 → 安全
 *   要点：守卫让"传 0"这个调用点在 TN 里不误报（Lv3 的"条件带守卫"在这里已经要用到）。
 */
#include "../common.h"

#define HIST_LEN 16

static UINT8 Hist_GetPrev(const UINT8 *pucHist, UINT32 ulPos)
{
    return pucHist[ulPos - 1];                                /* ★1 */
}

static UINT8 Hist_GetPrevSafe(const UINT8 *pucHist, UINT32 ulPos)
{
    if (ulPos == 0) {
        return 0;
    }
    return pucHist[ulPos - 1];
}

static UINT32 Ue_CalcJitter(const UINT8 *pucHist, UINT32 ulPos)      { return (UINT32)Hist_GetPrev(pucHist, ulPos); }
static UINT32 Ue_CalcJitter_OK(const UINT8 *pucHist, UINT32 ulPos)   { return (UINT32)Hist_GetPrevSafe(pucHist, ulPos); }
static UINT32 Ue_UpdateJitter(const UINT8 *pucHist, UINT32 ulPos)    { return Ue_CalcJitter(pucHist, ulPos); }
static UINT32 Ue_UpdateJitter_OK(const UINT8 *pucHist, UINT32 ulPos) { return Ue_CalcJitter_OK(pucHist, ulPos); }

UINT32 Ue_CheckJitter_TP(void)
{
    UINT8 aucHist[HIST_LEN] = { 0 };
    return Ue_UpdateJitter(aucHist, 0);
}

UINT32 Ue_CheckJitter_TN(void)
{
    UINT8 aucHist[HIST_LEN] = { 0 };
    return Ue_UpdateJitter_OK(aucHist, 0);
}
