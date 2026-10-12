/*
 * @id      LV1-06
 * @title   查表函数自己的常量表，下标从多层之上传入，中间层只是改名转存
 * @scene   S1.5  A1·M1·P0·V1·R1·X0·K0·G0
 * @year    承诺
 * @story   QCI 权重表按 0..8 存 9 个值；有一个调用方把"QCI 个数"当成了 QCI 传进来。
 *          中间每层都把参数转存到一个局部变量再往下传。
 * @expect  TP  Sched_InitDefault_TP  pa=L1
 * @expect  TN  Sched_InitDefault_TN  pa=-
 *
 * 摘要
 *   Qci_GetWeight(ulQci)
 *     req R1  R4 @ s_aulWeight + 4·ulQci       需 4·ulQci + 4 ≤ 36（ulQci ≤ 8）               ★1
 *   Bearer_CalcPrio(ulQci)
 *     ulTmp = ulQci; ulIdx = ulTmp —— 流敏感分析里中间变量就是同一个符号，不需要任何额外机制
 *     req R1  需 ulQci ≤ 8
 *   Sched_AddBearer(ulQci)    req R1 原样上传
 * 串联
 *   Sched_InitDefault_TP：ulQci = MAX_QCI_NUM = 9 → 9 ≤ 8 不成立 → L1
 *   Sched_InitDefault_TN：ulQci = 9 − 1 = 8 → 成立
 */
#include "../common.h"

#define MAX_QCI_NUM 9

static UINT32 Qci_GetWeight(UINT32 ulQci)
{
    static const UINT32 s_aulWeight[MAX_QCI_NUM] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    return s_aulWeight[ulQci];                                /* ★1 */
}

static UINT32 Bearer_CalcPrio(UINT32 ulQci)
{
    UINT32 ulTmp = ulQci;
    UINT32 ulIdx = ulTmp;
    return Qci_GetWeight(ulIdx) * 10;
}

static UINT32 Sched_AddBearer(UINT32 ulQci)
{
    UINT32 ulPrio = Bearer_CalcPrio(ulQci);
    LOG_Print(3, "prio %u", ulPrio);
    return ulPrio;
}

UINT32 Sched_InitDefault_TP(void) { return Sched_AddBearer(MAX_QCI_NUM); }
UINT32 Sched_InitDefault_TN(void) { return Sched_AddBearer(MAX_QCI_NUM - 1); }
