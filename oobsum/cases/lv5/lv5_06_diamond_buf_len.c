/*
 * @id      LV5-06
 * @title   菱形：缓冲区从 A 链（取块，三层）、长度从 B 链（取配置，两层）分别回到链顶，再一起下传
 * @scene   S4.1+S4.4 组合（R3）  A3·M4·P2+P1·V2·R3·X0·K0·G0
 * @year    承诺（用 Lv4 的返回值效应就够）
 * @story   报告块大小 64，上报长度配置从 48 改成了 96，两边由不同的人维护。
 * @expect  TP  Rpt_Flush_TP  pa=L1
 * @expect  TN  Rpt_Flush_TN  pa=-
 *
 * 摘要
 *   Pool_GetBlk(ulLen)        eff ret = new(ulLen) ∨ NULL
 *   Rpt_BlkAlloc()            eff ret = new(64) ∨ NULL
 *   Rpt_GetBuf()              eff ret = new(64) ∨ NULL              （A 链：大小往上走 3 层）
 *   Cfg_GetRptLen()           eff ret == 96
 *   Rpt_GetLen()              eff ret == 96                         （B 链：值往上走 2 层）
 *   Rpt_Write(pucBuf, ulLen, pucData)   req R1  需 ulLen ≤ ext(pucBuf)；R2 需 ulLen ≤ ext(pucData)  ★1
 *   Rpt_Store / Rpt_Commit             原样上传
 * 串联
 *   Rpt_Flush_TP：pucBuf = new(64)，ulLen = 96 → R1：96 ≤ 64 不成立 → L1；R2：aucData 128 → 成立
 *   Rpt_Flush_TN：Rpt_GetLen_OK 返回 48 → 成立
 *   要点：汇合点在链顶，两侧都是"往上走"的效应（V 形 + 菱形），不需要 Lv5 的字段能力。
 */
#include "../common.h"

#define RPT_BLK_LEN 64

static void  *Pool_GetBlk(UINT32 ulLen) { return Pub_MemAlloc(ulLen); }
static UINT8 *Rpt_BlkAlloc(void)        { return (UINT8 *)Pool_GetBlk(RPT_BLK_LEN); }
static UINT8 *Rpt_GetBuf(void)          { return Rpt_BlkAlloc(); }

static UINT32 Cfg_GetRptLen(void)    { return 96; }
static UINT32 Cfg_GetRptLen_OK(void) { return 48; }
static UINT32 Rpt_GetLen(void)       { return Cfg_GetRptLen(); }
static UINT32 Rpt_GetLen_OK(void)    { return Cfg_GetRptLen_OK(); }

static void Rpt_Write(UINT8 *pucBuf, UINT32 ulLen, const UINT8 *pucData)
{
    (void)memcpy_s(pucBuf, ulLen, pucData, ulLen);           /* ★1 */
}

static void Rpt_Store(UINT8 *pucBuf, UINT32 ulLen, const UINT8 *pucData)  { Rpt_Write(pucBuf, ulLen, pucData); }
static void Rpt_Commit(UINT8 *pucBuf, UINT32 ulLen, const UINT8 *pucData) { Rpt_Store(pucBuf, ulLen, pucData); }

void Rpt_Flush_TP(void)
{
    UINT8  aucData[128] = { 0 };
    UINT8 *pucBuf = Rpt_GetBuf();
    if (pucBuf == VOS_NULL_PTR) {
        return;
    }
    Rpt_Commit(pucBuf, Rpt_GetLen(), aucData);
}

void Rpt_Flush_TN(void)
{
    UINT8  aucData[128] = { 0 };
    UINT8 *pucBuf = Rpt_GetBuf();
    if (pucBuf == VOS_NULL_PTR) {
        return;
    }
    Rpt_Commit(pucBuf, Rpt_GetLen_OK(), aucData);
}
