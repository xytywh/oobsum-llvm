/*
 * @id      LV3-04
 * @title   按源列表的个数循环拷贝，目的数组是老规格
 * @scene   S3.4  A1·M1·P1·V5→G2·R2·X1·K2·G2
 * @year    承诺
 * @story   邻区更新消息最多带 32 个邻区（入口按 32 校验），本地缓存数组还是 16。
 * @expect  TP  Cell_OnNbrUpdate_TP  pa=L2
 * @expect  TN  Cell_OnNbrUpdate_TN  pa=-
 *
 * 摘要
 *   Nbr_CopyN(astDst, astSrc, ulNum)
 *     i ∈ [0, ulNum − 1]，NBR_S 4 字节
 *     req R1  ulNum ≥ 1 ⇒ W4 @ astDst + 4·[0, ulNum−1]   需 4·ulNum ≤ ext(astDst)               ★1
 *     req R2  ulNum ≥ 1 ⇒ R4 @ astSrc + 4·[0, ulNum−1]   需 4·ulNum ≤ ext(astSrc)               ★1
 *   Nbr_Sync / Cell_SyncNbr                原样上传
 * 串联
 *   Cell_OnNbrUpdate_TP：astDst = astLocal（64 字节），ulNum 经校验 ∈ [0, 32] → 需 128 ≤ 64 → L2
 *                        R2 的 astSrc 是入口形参 → 停在入口，记账
 *   Cell_OnNbrUpdate_TN：校验按 16 → 需 64 ≤ 64 成立
 */
#include "../common.h"

#define MAX_NBR_NUM     16
#define MAX_NBR_NUM_V2  32

typedef struct {
    UINT16 usPci;
    UINT16 usEarfcn;
} NBR_S;

static void Nbr_CopyN(NBR_S *astDst, const NBR_S *astSrc, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        astDst[i] = astSrc[i];                                /* ★1 */
    }
}

static void Nbr_Sync(NBR_S *astDst, const NBR_S *astSrc, UINT32 ulNum)     { Nbr_CopyN(astDst, astSrc, ulNum); }
static void Cell_SyncNbr(NBR_S *astDst, const NBR_S *astSrc, UINT32 ulNum) { Nbr_Sync(astDst, astSrc, ulNum); }

void Cell_OnNbrUpdate_TP(const NBR_S *astNew, UINT32 ulNewNum)
{
    NBR_S astLocal[MAX_NBR_NUM];
    if (ulNewNum > MAX_NBR_NUM_V2) {
        return;
    }
    Cell_SyncNbr(astLocal, astNew, ulNewNum);
}

void Cell_OnNbrUpdate_TN(const NBR_S *astNew, UINT32 ulNewNum)
{
    NBR_S astLocal[MAX_NBR_NUM];
    if (ulNewNum > MAX_NBR_NUM) {
        return;
    }
    Cell_SyncNbr(astLocal, astNew, ulNewNum);
}
