/*
 * @id      LV4-02
 * @title   申请写成 sizeof(指针)，三层之下按结构体成员写
 * @scene   S4.1  A3·M4·P2+P1·V0·R1·X1·K0·G0
 * @year    承诺
 * @story   经典笔误 sizeof(pstSec)。目标是 ILP32，申请到的是 4 字节；host 上分析是 8 字节——两种都越界。
 * @expect  TP  Ue_CreateSec_TP  pa=L1
 * @expect  TN  Ue_CreateSec_TN  pa=-
 *
 * 摘要
 *   Pub_MemAlloc(ulSize)       eff ret = new(ulSize) ∨ NULL
 *   Sec_SetKey(pstSec, pucKey)
 *     aucKey 在 +8，32 字节
 *     req R1  W32 @ pstSec + 8       需 40 ≤ ext(pstSec)                                         ★1
 *     req R2  R32 @ pucKey + 0       需 32 ≤ ext(pucKey)                                         ★1
 *   Sec_Init / Ue_InitSec      原样上传
 * 串联
 *   Ue_CreateSec_TP：ext(pstSec) = sizeof(SEC_CTX_S *) = 4（ILP32；按 host 算是 8）→ 40 ≤ 4 不成立 → L1
 *                    R2 的 pucKey 是入口形参 → 记账
 *   Ue_CreateSec_TN：sizeof(*pstSec) = 40 → 成立
 *   要点：sizeof 的值按被分析代码的目标 ABI 算；这条两种 ABI 结论一样，有的用例不一样（T0-1c）。
 */
#include "../common.h"

typedef struct {
    UINT32 ulUeId;
    UINT32 ulState;
    UINT8  aucKey[32];
} SEC_CTX_S;

static void Sec_SetKey(SEC_CTX_S *pstSec, const UINT8 *pucKey)
{
    (void)memcpy_s(pstSec->aucKey, sizeof(pstSec->aucKey), pucKey, sizeof(pstSec->aucKey));   /* ★1 */
}

static void Sec_Init(SEC_CTX_S *pstSec, const UINT8 *pucKey)
{
    pstSec->ulState = 0;
    Sec_SetKey(pstSec, pucKey);
}

static void Ue_InitSec(SEC_CTX_S *pstSec, const UINT8 *pucKey) { Sec_Init(pstSec, pucKey); }

SEC_CTX_S *Ue_CreateSec_TP(const UINT8 *pucKey)
{
    SEC_CTX_S *pstSec = (SEC_CTX_S *)Pub_MemAlloc(sizeof(pstSec));
    if (pstSec == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    Ue_InitSec(pstSec, pucKey);
    return pstSec;
}

SEC_CTX_S *Ue_CreateSec_TN(const UINT8 *pucKey)
{
    SEC_CTX_S *pstSec = (SEC_CTX_S *)Pub_MemAlloc(sizeof(*pstSec));
    if (pstSec == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    Ue_InitSec(pstSec, pucKey);
    return pstSec;
}
