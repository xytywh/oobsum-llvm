/*
 * @id      LV7-03
 * @title   字节缓冲按消息结构体解释，三层之下写数据区——缓冲区本身比结构体短
 * @scene   S7.3  A1·M1·P1·V1·R2·X4·K0·G0
 * @year    2027H1
 * @story   收到的短消息只有 8 字节，处理函数统一强转成 MSG_S*（头 4 字节 + 数据 12 字节）去写确认位。
 * @expect  TP  Om_OnShortMsg_TP  pa=记账  later=L1
 * @expect  TN  Om_OnShortMsg_TN  pa=记账  later=-
 *
 * 摘要
 *   Msg_SetData(pucRaw, ulIdx)
 *     (MSG_S *)pucRaw 之后访问 aucData[ulIdx]：字节偏移 4 + ulIdx；同时受成员（子对象）12 字节约束
 *     req R1  W1 @ pucRaw + 4 + ulIdx     需 ulIdx + 5 ≤ ext(pucRaw)      （对象边界）                   ★1
 *     req R2  需 ulIdx ≤ 11                                                 （成员边界）
 *   Msg_Ack / Om_HandleMsg            原样上传
 * 串联
 *   2027：Om_OnShortMsg_TP：aucRaw（8），ulIdx = 6 → R1：11 ≤ 8 不成立 → L1；R2 成立
 *   年内："把字节缓冲当结构体用"不建模 → 记账；不会误报
 */
#include "../common.h"

typedef struct {
    UINT16 usType;
    UINT16 usLen;
    UINT8  aucData[12];
} MSG_S;

static void Msg_SetData(UINT8 *pucRaw, UINT32 ulIdx)
{
    MSG_S *pstMsg = (MSG_S *)(void *)pucRaw;
    pstMsg->aucData[ulIdx] = 0x01;                            /* ★1 */
}

static void Msg_Ack(UINT8 *pucRaw, UINT32 ulIdx)       { Msg_SetData(pucRaw, ulIdx); }
static void Om_HandleMsg(UINT8 *pucRaw, UINT32 ulIdx)  { Msg_Ack(pucRaw, ulIdx); }

void Om_OnShortMsg_TP(void)
{
    UINT32 aulRaw[2] = { 0 };                 /* 8 字节、4 字节对齐 */
    Om_HandleMsg((UINT8 *)aulRaw, 6);
}

void Om_OnShortMsg_TN(void)
{
    UINT32 aulRaw[2] = { 0 };
    Om_HandleMsg((UINT8 *)aulRaw, 3);
}
