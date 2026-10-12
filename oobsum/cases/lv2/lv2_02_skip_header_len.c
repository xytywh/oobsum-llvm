/*
 * @id      LV2-02
 * @title   跳过消息头后下传，长度没扣头长（读越界）
 * @scene   S2.3  A3·M1·P1·V1·R2·X1·K0·G0
 * @year    承诺
 * @story   消息处理入口把整条消息的长度原样交给消息体解析，而消息体指针已经跳过了 8 字节头。
 * @expect  TP  Msg_Entry_TP  pa=L1
 * @expect  TN  Msg_Entry_TN  pa=-
 *
 * 摘要
 *   Body_Copy(pucBody, ulLen, pucOut)
 *     req R1  W256 @ pucOut + 0          需 256 ≤ ext(pucOut)                                    ★1
 *     req R2  R ulLen @ pucBody + 0      需 ulLen ≤ ext(pucBody)                                 ★1
 *   Body_Parse                            R1、R2 原样上传
 *   Msg_Proc(pucMsg, ulMsgLen, pucOut)
 *     实参 pucBody = pucMsg + 8：ext(pucMsg + 8) = ext(pucMsg) − 8
 *     req R2  R ulMsgLen @ pucMsg + 8    需 ulMsgLen + 8 ≤ ext(pucMsg)
 *   Msg_Proc_OK：实参 ulLen = ulMsgLen − 8 → 需 ulMsgLen ≤ ext(pucMsg)（−8 与 +8 抵消）
 * 串联
 *   Msg_Entry_TP：pucMsg = aucMsg（64），ulMsgLen = 64 → 72 ≤ 64 不成立 → L1
 *                 pucOut = aucOut（256）→ R1 成立
 *   Msg_Entry_TN：走 Msg_Proc_OK → 64 ≤ 64 成立
 */
#include "../common.h"

#define MSG_HEAD_LEN 8

static void Body_Copy(const UINT8 *pucBody, UINT32 ulLen, UINT8 *pucOut)
{
    (void)memcpy_s(pucOut, 256, pucBody, ulLen);             /* ★1 */
}

static void Body_Parse(const UINT8 *pucBody, UINT32 ulLen, UINT8 *pucOut)
{
    Body_Copy(pucBody, ulLen, pucOut);
}

static void Msg_Proc(const UINT8 *pucMsg, UINT32 ulMsgLen, UINT8 *pucOut)
{
    Body_Parse(pucMsg + MSG_HEAD_LEN, ulMsgLen, pucOut);
}

static void Msg_Proc_OK(const UINT8 *pucMsg, UINT32 ulMsgLen, UINT8 *pucOut)
{
    Body_Parse(pucMsg + MSG_HEAD_LEN, ulMsgLen - MSG_HEAD_LEN, pucOut);
}

void Msg_Entry_TP(void)
{
    UINT8 aucMsg[64] = { 0 };
    UINT8 aucOut[256];
    Msg_Proc(aucMsg, sizeof(aucMsg), aucOut);
}

void Msg_Entry_TN(void)
{
    UINT8 aucMsg[64] = { 0 };
    UINT8 aucOut[256];
    Msg_Proc_OK(aucMsg, sizeof(aucMsg), aucOut);
}
