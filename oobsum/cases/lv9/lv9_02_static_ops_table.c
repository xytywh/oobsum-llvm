/*
 * @id      LV9-02
 * @title   静态消息分发表（全局 const 结构体数组里的函数指针），其中一个处理函数写结尾越界
 * @scene   S9.2  A1·M1·P5·V1·R2·X0·K1·G0
 * @year    2027H1
 * @story   消息分发表在编译期就确定；B 消息的处理函数按 ulLen 位置写 '\0'。
 * @expect  TP  Msg_OnRecv_TP  pa=记账  later=L2
 * @expect  TN  Msg_OnRecv_TN  pa=记账  later=-
 *
 * 摘要
 *   A_Proc(pucBody, ulLen)    req     需 ulLen ≤ ext(pucBody)
 *   B_Proc(pucBody, ulLen)    req R1  W1 @ pucBody + ulLen     需 ulLen + 1 ≤ ext(pucBody)             ★1
 *   Msg_Dispatch(ulMsgId, pucBody, ulLen)
 *     g_astHandler[i].pfnProc：按"MSG_HANDLER_S.pfnProc 这个字段被赋过哪些函数"解析 → {A_Proc, B_Proc}
 *     （字段级解析；目标多时可让 AI 结合 ulMsgId 做精度过滤）
 *     req R1  需 ulLen + 1 ≤ ext(pucBody)      （取最坏的目标；带"可能"标记）
 *   Msg_Route / Msg_Handle    原样上传
 * 串联
 *   2027：TP：aucBody 32、ulLen 32 → 33 ≤ 32 不成立，但只在 B 目标上 → L2；TN：ulLen 31 → 成立
 *   年内：函数指针调用断链 → 记账
 */
#include "../common.h"

#define MSG_ID_A 0x101
#define MSG_ID_B 0x102

typedef struct {
    UINT32 ulMsgId;
    void (*pfnProc)(UINT8 *pucBody, UINT32 ulLen);
} MSG_HANDLER_S;

static void A_Proc(UINT8 *pucBody, UINT32 ulLen) { pucBody[ulLen - 1] = 0; }
static void B_Proc(UINT8 *pucBody, UINT32 ulLen) { pucBody[ulLen] = '\0'; }       /* ★1 */

static const MSG_HANDLER_S g_astHandler[] = {
    { MSG_ID_A, A_Proc },
    { MSG_ID_B, B_Proc },
};

static void Msg_Dispatch(UINT32 ulMsgId, UINT8 *pucBody, UINT32 ulLen)
{
    UINT32 i;
    for (i = 0; i < sizeof(g_astHandler) / sizeof(g_astHandler[0]); i++) {
        if (g_astHandler[i].ulMsgId == ulMsgId) {
            g_astHandler[i].pfnProc(pucBody, ulLen);
            return;
        }
    }
}

static void Msg_Route(UINT32 ulMsgId, UINT8 *pucBody, UINT32 ulLen)  { Msg_Dispatch(ulMsgId, pucBody, ulLen); }
static void Msg_Handle(UINT32 ulMsgId, UINT8 *pucBody, UINT32 ulLen) { Msg_Route(ulMsgId, pucBody, ulLen); }

void Msg_OnRecv_TP(UINT32 ulMsgId)
{
    UINT8 aucBody[32];
    Msg_Handle(ulMsgId, aucBody, sizeof(aucBody));
}

void Msg_OnRecv_TN(UINT32 ulMsgId)
{
    UINT8 aucBody[32];
    Msg_Handle(ulMsgId, aucBody, sizeof(aucBody) - 1);
}
