/*
 * @id      LV1-03
 * @title   常量长度的堆内存逐层下传，最底层循环多写一个
 * @scene   S1.2+S1.3  A1·M4·P1·V0·R1·X0·K2·G0
 * @year    承诺
 * @story   消息体按 MSG_BODY_LEN 申请，填充函数用 i <= MSG_BODY_LEN 循环。
 * @expect  TP  Msg_Build_TP  pa=L2
 * @expect  TN  Msg_Build_TN  pa=-
 *
 * 摘要
 *   Msg_FillPadding(pucBody)
 *     循环 i ∈ [0, 64]（阈值 widening 收在 64）
 *     req R1  W1 @ pucBody + [0, 64]                  需 65 ≤ ext(pucBody)                     ★1
 *   Msg_InitBody(pucBody)      req R1 原样上传
 *   Msg_Prepare(pucBody)       req R1 原样上传         path Msg_Prepare → Msg_InitBody → Msg_FillPadding
 * 串联
 *   Msg_Build_TP：pucBody = new(64)（底层分配器配置了字节数下标）→ 访问范围 [0,64] 与大小 64 部分重叠 → L2
 *   Msg_Build_TN：new(65) → 成立
 *   分级说明：循环的访问是一段范围，最后一次才越界，按区间口径是 L2（Infer 的口径相同）。
 */
#include "../common.h"

#define MSG_BODY_LEN 64

static void Msg_FillPadding(UINT8 *pucBody)
{
    UINT32 i;
    for (i = 0; i <= MSG_BODY_LEN; i++) {
        pucBody[i] = 0xFF;                                    /* ★1 */
    }
}

static void Msg_InitBody(UINT8 *pucBody)
{
    Msg_FillPadding(pucBody);
}

static UINT32 Msg_Prepare(UINT8 *pucBody)
{
    Msg_InitBody(pucBody);
    return VOS_OK;
}

UINT32 Msg_Build_TP(void)
{
    UINT8 *pucBody = (UINT8 *)VOS_MemAlloc(PID_CELL, 0, MSG_BODY_LEN);
    if (pucBody == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    (void)Msg_Prepare(pucBody);
    (void)VOS_MemFree(PID_CELL, pucBody);
    return VOS_OK;
}

UINT32 Msg_Build_TN(void)
{
    UINT8 *pucBody = (UINT8 *)VOS_MemAlloc(PID_CELL, 0, MSG_BODY_LEN + 1);
    if (pucBody == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    (void)Msg_Prepare(pucBody);
    (void)VOS_MemFree(PID_CELL, pucBody);
    return VOS_OK;
}
