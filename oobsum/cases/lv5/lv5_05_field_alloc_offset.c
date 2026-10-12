/*
 * @id      LV5-05
 * @title   字段级申请（alloc_free_identity 里带 offset 的那类）：资源缓冲写尾越界
 * @scene   S5.5  A1·M4·P3+P2·V0·R1·X0·K0·G0
 * @year    尽力
 * @story   Ue_AllocRes 把申请结果写进 pstUe->pucRes（JSON 里是 para 0、offset [4]）；
 *          三层之下的填充函数按 RES_LEN 位置写校验字节。
 * @expect  TP  Ue_OnSetup_TP  pa=L1
 * @expect  TN  Ue_OnSetup_TN  pa=-
 *
 * 摘要
 *   Ue_AllocRes(pstUe)       eff pstUe->pucRes := new(64) ∨ NULL；ret == VOS_OK ⇒ pstUe->pucRes ≠ NULL
 *                            （与 alloc_free_identity 的 {para: 0, offset: [4]} 交叉校验一致）
 *   Res_WriteChk(pucRes, ulPos)   req R1  W1 @ pucRes + ulPos     需 ulPos + 1 ≤ ext(pucRes)           ★1
 *   Res_Fill(pstUe, ulPos)        实参 pstUe->pucRes → req R1  需 ulPos + 1 ≤ ext(pstUe->pucRes)
 *   Ue_PrepareRes                 原样上传
 * 串联
 *   Ue_OnSetup_TP：Ue_AllocRes(&stUe) 返回 OK 的分支上 stUe.pucRes = new(64)，ulPos = 64 → 65 ≤ 64 不成立 → L1
 *   Ue_OnSetup_TN：校验字节写在 RES_LEN − 1 → 成立
 */
#include "../common.h"

#define RES_LEN 64

typedef struct {
    UINT32  ulUeId;
    UINT8  *pucRes;
    UINT32  ulResLen;
} UE_RES_S;

static UINT32 Ue_AllocRes(UE_RES_S *pstUe)
{
    pstUe->pucRes = (UINT8 *)Pub_MemAlloc(RES_LEN);
    if (pstUe->pucRes == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    pstUe->ulResLen = RES_LEN;
    return VOS_OK;
}

static void Res_WriteChk(UINT8 *pucRes, UINT32 ulPos)   { pucRes[ulPos] = 0xA5; }       /* ★1 */
static void Res_Fill(UE_RES_S *pstUe, UINT32 ulPos)      { Res_WriteChk(pstUe->pucRes, ulPos); }
static void Ue_PrepareRes(UE_RES_S *pstUe, UINT32 ulPos) { Res_Fill(pstUe, ulPos); }

void Ue_OnSetup_TP(UINT32 ulUeId)
{
    UE_RES_S stUe;
    stUe.ulUeId = ulUeId;
    if (Ue_AllocRes(&stUe) != VOS_OK) {
        return;
    }
    Ue_PrepareRes(&stUe, RES_LEN);
}

void Ue_OnSetup_TN(UINT32 ulUeId)
{
    UE_RES_S stUe;
    stUe.ulUeId = ulUeId;
    if (Ue_AllocRes(&stUe) != VOS_OK) {
        return;
    }
    Ue_PrepareRes(&stUe, RES_LEN - 1);
}
