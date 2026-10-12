/*
 * @id      LV8-05
 * @title   环形缓冲区：尾指针回绕条件写成 > RING_LEN（应为 >=），访问在三层之下
 * @scene   S8.1 变体（字段不变式）  A1·M2·P4·V4·R1·X0·K3·G4
 * @year    AI + 程序分析（也是 2027 字段级不变式的候选）
 * @story   trace 环形缓冲在全局；每次写入后尾指针加一，回绕条件差一，tail 会停在 RING_LEN 上一次。
 * @expect  TP  Om_Trace_TP  pa=记账  ai+pa=L2
 * @expect  TN  Om_Trace_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Ring_Put(pstRing, ucVal)
 *     req R1  W1 @ pstRing + 4 + pstRing->ulTail      需 pstRing->ulTail ≤ 63                        ★1
 *     eff     pstRing->ulTail := (pstRing->ulTail + 1 > 64) ? 0 : pstRing->ulTail + 1
 *   Trace_Put / Trace_Record          原样上传
 * 串联
 *   年内：链顶传的是 &g_stTraceRing，ulTail 的值来自"上一次调用留下的状态" → 无约束 → 记账
 *   AI + 程序分析：AI 从字段名与访问方式提出字段不变式 0 ≤ ulTail ≤ 63；
 *     程序分析检查所有写 ulTail 的地方是否维持它：Ring_Put 的更新允许 ulTail = 64 → 不维持 → 在写入点报 L2
 *     TN（>=）：更新后 ∈ [0, 63] → 维持 → R1 在不变式下成立
 */
#include "../common.h"

#define RING_LEN 64

typedef struct {
    UINT32 ulTail;
    UINT8  aucData[RING_LEN];
} RING_S;

static RING_S g_stTraceRing;
static RING_S g_stTraceRingOk;

static void Ring_Put(RING_S *pstRing, UINT8 ucVal)
{
    pstRing->aucData[pstRing->ulTail] = ucVal;                /* ★1 */
    pstRing->ulTail++;
    if (pstRing->ulTail > RING_LEN) {
        pstRing->ulTail = 0;
    }
}

static void Ring_Put_OK(RING_S *pstRing, UINT8 ucVal)
{
    pstRing->aucData[pstRing->ulTail] = ucVal;
    pstRing->ulTail++;
    if (pstRing->ulTail >= RING_LEN) {
        pstRing->ulTail = 0;
    }
}

static void Trace_Put(RING_S *r, UINT8 v)       { Ring_Put(r, v); }
static void Trace_Put_OK(RING_S *r, UINT8 v)    { Ring_Put_OK(r, v); }
static void Trace_Record(RING_S *r, UINT8 v)    { Trace_Put(r, v); }
static void Trace_Record_OK(RING_S *r, UINT8 v) { Trace_Put_OK(r, v); }

void Om_Trace_TP(UINT8 ucEvt) { Trace_Record(&g_stTraceRing, ucEvt); }
void Om_Trace_TN(UINT8 ucEvt) { Trace_Record_OK(&g_stTraceRingOk, ucEvt); }
