/*
 * @id      LV2-05
 * @title   字节型位图按 UINT32 清零，字数算多了一个
 * @scene   S2.6+S2.7  A1·M1·P1·V1·R2·X2+X3·K2·G0
 * @year    承诺
 * @story   位图按字节定义（MAX_UE_NUM / 8 字节），清零时强转成 UINT32* 按字清，
 *          字数写成了 ulBits / 32 + 1 的等价形式（i <= ulBits / 32）。
 * @expect  TP  Cell_ResetUeMap_TP  pa=L2
 * @expect  TN  Cell_ResetUeMap_TN  pa=-
 *
 * 摘要
 *   Map_ClearWords(pucMap, ulBits)
 *     pulMap = (UINT32 *)pucMap：同一基址，元素大小换成 4（字节归一后条件仍写在 pucMap 上）
 *     i ∈ [0, ulBits / 32]，ulBits / 32 作为"除以常量"保留，不降成 Top
 *     req R1  W4 @ pucMap + 4·[0, ulBits/32]     需 4·(ulBits/32) + 4 ≤ ext(pucMap)            ★1
 *   Map_ClearWords_OK(pucMap, ulBits)
 *     req R1' ulBits ≥ 1 ⇒ W4 @ pucMap + 4·[0, (ulBits+31)/32 − 1]   需 4·((ulBits+31)/32) ≤ ext(pucMap)
 *   Ue_ResetMap / Cell_ClearUeMap     原样上传
 * 串联
 *   Cell_ResetUeMap_TP：aucMap 8 字节，ulBits = 64 → 64/32 = 2 → 需 12 ≤ 8；i = 0 那次安全 → L2
 *   Cell_ResetUeMap_TN：(64+31)/32 = 2 → 需 8 ≤ 8 成立
 *   要点：除法、强转都在 Lv2 能做的范围里——除以常量对区间是精确的，强转只是换元素大小。
 */
#include "../common.h"

#define MAX_UE_NUM 64

static void Map_ClearWords(UINT8 *pucMap, UINT32 ulBits)
{
    UINT32 *pulMap = (UINT32 *)(void *)pucMap;
    UINT32 i;
    for (i = 0; i <= ulBits / 32; i++) {
        pulMap[i] = 0;                                        /* ★1 */
    }
}

static void Map_ClearWords_OK(UINT8 *pucMap, UINT32 ulBits)
{
    UINT32 *pulMap = (UINT32 *)(void *)pucMap;
    UINT32 i;
    for (i = 0; i < (ulBits + 31) / 32; i++) {
        pulMap[i] = 0;
    }
}

static void Ue_ResetMap(UINT8 *pucMap, UINT32 ulBits)       { Map_ClearWords(pucMap, ulBits); }
static void Ue_ResetMap_OK(UINT8 *pucMap, UINT32 ulBits)    { Map_ClearWords_OK(pucMap, ulBits); }
static void Cell_ClearUeMap(UINT8 *pucMap, UINT32 ulBits)    { Ue_ResetMap(pucMap, ulBits); }
static void Cell_ClearUeMap_OK(UINT8 *pucMap, UINT32 ulBits) { Ue_ResetMap_OK(pucMap, ulBits); }

void Cell_ResetUeMap_TP(void)
{
    UINT32 aulAlign[MAX_UE_NUM / 32];             /* 保证 4 字节对齐的 8 字节缓冲 */
    Cell_ClearUeMap((UINT8 *)aulAlign, MAX_UE_NUM);
}

void Cell_ResetUeMap_TN(void)
{
    UINT32 aulAlign[MAX_UE_NUM / 32];
    Cell_ClearUeMap_OK((UINT8 *)aulAlign, MAX_UE_NUM);
}
