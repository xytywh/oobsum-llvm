/*
 * @id      LV4-04
 * @title   出参分配器申请报告缓冲，填充时按"报告长 + 尾长"清零
 * @scene   S4.3  A3·M4·P2+P1·V0·R1·X1·K0·G0
 * @year    承诺
 * @story   报告缓冲由 Buf_Create(&p, len) 申请；后来报告加了 8 字节尾，填充改了，申请没改。
 * @expect  TP  Rpt_Send_TP  pa=L1
 * @expect  TN  Rpt_Send_TN  pa=-
 *
 * 摘要
 *   Buf_Create(ppucBuf, ulLen)     eff *ppucBuf = new(ulLen) ∨ NULL；ret == VOS_OK ⇒ *ppucBuf ≠ NULL
 *   Rpt_CreateBuf(ppucBuf)         eff *ppucBuf = new(64) ∨ NULL（实参 RPT_LEN 代入）
 *   Rpt_Clear(pucBuf, ulLen)       req R1  W ulLen @ pucBuf + 0   需 ulLen ≤ ext(pucBuf)             ★1
 *   Rpt_Fill / Rpt_Prepare         实参 ulLen = 72 → 需 72 ≤ ext(pucBuf)
 * 串联
 *   Rpt_Send_TP：出参效果把 pucBuf 记成 new(64) → 72 ≤ 64 不成立 → L1
 *   Rpt_Send_TN：Rpt_CreateBuf_OK 申请 72 → 成立
 */
#include "../common.h"

#define RPT_LEN       64
#define RPT_TAIL_LEN  8

static UINT32 Buf_Create(UINT8 **ppucBuf, UINT32 ulLen)
{
    *ppucBuf = (UINT8 *)Pub_MemAlloc(ulLen);
    return (*ppucBuf == VOS_NULL_PTR) ? VOS_ERR : VOS_OK;
}

static UINT32 Rpt_CreateBuf(UINT8 **ppucBuf)    { return Buf_Create(ppucBuf, RPT_LEN); }
static UINT32 Rpt_CreateBuf_OK(UINT8 **ppucBuf) { return Buf_Create(ppucBuf, RPT_LEN + RPT_TAIL_LEN); }

static void Rpt_Clear(UINT8 *pucBuf, UINT32 ulLen)
{
    (void)memset_s(pucBuf, ulLen, 0, ulLen);                  /* ★1 */
}

static void Rpt_Fill(UINT8 *pucBuf)    { Rpt_Clear(pucBuf, RPT_LEN + RPT_TAIL_LEN); }
static void Rpt_Prepare(UINT8 *pucBuf) { Rpt_Fill(pucBuf); }

void Rpt_Send_TP(void)
{
    UINT8 *pucBuf = VOS_NULL_PTR;
    if (Rpt_CreateBuf(&pucBuf) != VOS_OK) {
        return;
    }
    Rpt_Prepare(pucBuf);
}

void Rpt_Send_TN(void)
{
    UINT8 *pucBuf = VOS_NULL_PTR;
    if (Rpt_CreateBuf_OK(&pucBuf) != VOS_OK) {
        return;
    }
    Rpt_Prepare(pucBuf);
}
