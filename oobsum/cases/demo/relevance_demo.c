/*
 * 05- §1.2 的例子："整个函数好像都相关"——摘要里实际剩下什么。
 * 每行行尾的注释说明这一行为什么进 / 不进摘要。
 */
#include "../common.h"

#define UE_STATE_CONNECTED 2
#define STAT_TIMER_ERR     2

typedef struct {
    UINT32 ulUeId;
    UINT32 ulState;
    UINT32 ulTimerId;
    UINT32 ulRxCnt;
    UINT8  aucLastIe[32];
} UE_CTX_S;

static UINT32 g_aulStat[4];

static UINT32 Timer_Restart(UINT32 ulTimerId) { return (ulTimerId != 0) ? VOS_OK : VOS_ERR; }   /* 摘要：无条件 */
static void   Stat_Inc(UINT32 ulType)         { g_aulStat[ulType]++; }                          /* 摘要：需 ulType ≤ 3 */

UINT32 Ue_ProcRrcMsg(UE_CTX_S *pstUe, const UINT8 *pucMsg, UINT32 ulMsgLen)
{
    UINT32 ulIeLen;

    LOG_Print(3, "ue %u rrc len %u", pstUe->ulUeId, ulMsgLen);  /* 不进：什么都不流进访问 */
    pstUe->ulRxCnt++;                                            /* 进效应：整数字段写（固定词表） */
    if (pstUe->ulState != UE_STATE_CONNECTED) {                  /* 不进：守卫里没有被跟踪的量 */
        return VOS_ERR;
    }
    if (Timer_Restart(pstUe->ulTimerId) != VOS_OK) {             /* 不进：被调摘要没有条件 */
        Stat_Inc(STAT_TIMER_ERR);                                /* 不进：常量 2 代入"需 ≤ 3"，当场判安全 */
    }
    ulIeLen = pucMsg[1];                                         /* 进：读 pucMsg[1] → 需 2 ≤ ext(pucMsg) */
    if (ulIeLen + 2 > ulMsgLen) {                                /* 进：守卫，约束了 ulIeLen 与 ulMsgLen 的关系 */
        return VOS_ERR;
    }
    (void)memcpy_s(pstUe->aucLastIe, sizeof(pstUe->aucLastIe),   /* 目的：成员 32 字节、destMax 32 → 当场安全，不进 */
                   pucMsg + 2, ulIeLen);                         /* 源：末端 2 + ulIeLen ≤ ulMsgLen → 需 ulMsgLen ≤ ext(pucMsg) */
    return VOS_OK;
}
