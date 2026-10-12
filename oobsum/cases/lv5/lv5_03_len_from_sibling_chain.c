/*
 * @id      LV5-03
 * @title   IE 长度沿另一条链（按类型查表、两层返回）得到，缓冲区沿主链下传
 * @scene   S5.3  A3·M1·P1·V2·R3·X0·K1·G0
 * @year    尽力
 * @story   按 IE 类型取最大长度的函数里新增了一种 64 字节的 IE，接收缓冲区还是 32。
 * @expect  TP  Ue_OnIeInd_TP  pa=L2
 * @expect  TN  Ue_OnIeInd_TN  pa=-
 *
 * 摘要
 *   Ie_LookupLen(ucType)       eff ret ∈ [0, 64]       （switch 的各分支：16、24、64、默认 0）
 *   Ie_GetMaxLen(ucType)       eff ret ∈ [0, 64]       （返回值范围再上传一层）
 *   Ie_Store(pucDst, pucSrc, ulLen)  req R1  W ulLen @ pucDst   需 ulLen ≤ ext(pucDst)            ★1
 *                                    req R2  R ulLen @ pucSrc   需 ulLen ≤ ext(pucSrc)            ★1
 *   Ie_Save / Ue_SaveIe              原样上传
 * 串联
 *   Ue_OnIeInd_TP：ulLen = Ie_GetMaxLen(...) ∈ [0, 64]；aucIe 32 → L2；R2 的 pucIe 是入口形参 → 记账
 *   Ue_OnIeInd_TN：aucIe 64 → 成立
 *   为什么是尽力：返回值范围靠各分支合并，链上任何一层不精确都会退成更宽的区间。
 */
#include "../common.h"

#define IE_TYPE_MEAS   1
#define IE_TYPE_CAP    2
#define IE_TYPE_NR_CAP 3

static UINT32 Ie_LookupLen(UINT8 ucType)
{
    switch (ucType) {
        case IE_TYPE_MEAS:   return 16;
        case IE_TYPE_CAP:    return 24;
        case IE_TYPE_NR_CAP: return 64;
        default:             return 0;
    }
}

static UINT32 Ie_GetMaxLen(UINT8 ucType) { return Ie_LookupLen(ucType); }

static void Ie_Store(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen)
{
    (void)memcpy_s(pucDst, ulLen, pucSrc, ulLen);            /* ★1 */
}

static void Ie_Save(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen)   { Ie_Store(pucDst, pucSrc, ulLen); }
static void Ue_SaveIe(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen) { Ie_Save(pucDst, pucSrc, ulLen); }

void Ue_OnIeInd_TP(UINT8 ucType, const UINT8 *pucIe)
{
    UINT8 aucIe[32];
    Ue_SaveIe(aucIe, pucIe, Ie_GetMaxLen(ucType));
}

void Ue_OnIeInd_TN(UINT8 ucType, const UINT8 *pucIe)
{
    UINT8 aucIe[64];
    Ue_SaveIe(aucIe, pucIe, Ie_GetMaxLen(ucType));
}
