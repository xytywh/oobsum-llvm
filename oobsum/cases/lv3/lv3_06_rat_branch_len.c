/*
 * @id      LV3-06
 * @title   按制式选长度：NR 的能力长度超过了缓冲区（分支合并后一支越界）
 * @scene   S3.3  A3·M1·P1·V1·R2·X0·K1·G0
 * @year    尽力
 * @story   UE 能力缓存按 LTE 时代的 32 字节定义，NR 能力 48 字节。
 * @expect  TP  Ue_OnCapInfo_TP  pa=L2
 * @expect  TN  Ue_OnCapInfo_TN  pa=-
 *
 * 摘要
 *   Cap_Store(pucDst, pucSrc, ulLen)
 *     req R1  W ulLen @ pucDst + 0     需 ulLen ≤ ext(pucDst)                                    ★1
 *     req R2  R ulLen @ pucSrc + 0     需 ulLen ≤ ext(pucSrc)                                    ★1
 *   Cap_Save / Ue_SaveCap             原样上传
 * 串联
 *   Ue_OnCapInfo_TP：ulLen 在两个分支是 48 和 24，合并成 [24, 48]；aucCap 32 → 部分不满足 → L2
 *                    （保留两个分支各自的值能给出"NR 分支确定越界"，年内不做，所以是尽力）
 *   Ue_OnCapInfo_TN：aucCap 48 → 成立；R2 的 pucSrc 是入口形参 → 记账
 */
#include "../common.h"

#define RAT_LTE      0
#define RAT_NR       1
#define LTE_CAP_LEN  24
#define NR_CAP_LEN   48

static void Cap_Store(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen)
{
    (void)memcpy_s(pucDst, ulLen, pucSrc, ulLen);            /* ★1 */
}

static void Cap_Save(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen)   { Cap_Store(pucDst, pucSrc, ulLen); }
static void Ue_SaveCap(UINT8 *pucDst, const UINT8 *pucSrc, UINT32 ulLen) { Cap_Save(pucDst, pucSrc, ulLen); }

void Ue_OnCapInfo_TP(const UINT8 *pucCap, UINT32 ulRat)
{
    UINT8  aucCap[32];
    UINT32 ulLen = (ulRat == RAT_NR) ? NR_CAP_LEN : LTE_CAP_LEN;
    Ue_SaveCap(aucCap, pucCap, ulLen);
}

void Ue_OnCapInfo_TN(const UINT8 *pucCap, UINT32 ulRat)
{
    UINT8  aucCap[NR_CAP_LEN];
    UINT32 ulLen = (ulRat == RAT_NR) ? NR_CAP_LEN : LTE_CAP_LEN;
    Ue_SaveCap(aucCap, pucCap, ulLen);
}
