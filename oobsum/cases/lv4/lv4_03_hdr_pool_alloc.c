/*
 * @id      LV4-03
 * @title   带头部的缓冲池分配器：调用方按"长度 + 结尾符"写
 * @scene   S4.2  A1·M5·P2+P1·V1·R2·X1·K0·G0
 * @year    承诺
 * @story   缓冲池在用户区前面放 16 字节管理头，返回用户区指针。按形参名猜长度会把 ulLen + 16 当大小（错），
 *          摘要推导得到的是 ext(返回值) = ulLen（对）。
 * @expect  TP  Msg_MakeText_TP  pa=L1
 * @expect  TN  Msg_MakeText_TN  pa=-
 *
 * 摘要
 *   Buf_Alloc(ulLen)
 *     pucRaw = new(ulLen + 16)；返回 pucRaw + 16
 *     eff ret = new(ulLen + 16) + 16 ∨ NULL       即 ext(ret) = ulLen
 *   Msg_AllocBody(ulBodyLen)    eff ext(ret) = ulBodyLen
 *   Txt_Terminate(pucTxt, ulLen) req R1  W1 @ pucTxt + ulLen    需 ulLen + 1 ≤ ext(pucTxt)          ★1
 *   Txt_Fill / Msg_FillText      原样上传
 * 串联
 *   Msg_MakeText_TP：pucTxt = Msg_AllocBody(32) → ext = 32；ulLen = 32 → 33 ≤ 32 不成立 → L1
 *   Msg_MakeText_TN：Msg_AllocBody(33) → 成立
 */
#include "../common.h"

#define BUF_HDR_LEN 16

static UINT8 *Buf_Alloc(UINT32 ulLen)
{
    UINT8 *pucRaw = (UINT8 *)Pub_MemAlloc(ulLen + BUF_HDR_LEN);
    if (pucRaw == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    return pucRaw + BUF_HDR_LEN;
}

static UINT8 *Msg_AllocBody(UINT32 ulBodyLen) { return Buf_Alloc(ulBodyLen); }

static void Txt_Terminate(UINT8 *pucTxt, UINT32 ulLen)
{
    pucTxt[ulLen] = '\0';                                     /* ★1 */
}

static void Txt_Fill(UINT8 *pucTxt, UINT32 ulLen)     { pucTxt[0] = 'A'; Txt_Terminate(pucTxt, ulLen); }
static void Msg_FillText(UINT8 *pucTxt, UINT32 ulLen) { Txt_Fill(pucTxt, ulLen); }

UINT8 *Msg_MakeText_TP(void)
{
    UINT8 *pucTxt = Msg_AllocBody(32);
    if (pucTxt == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    Msg_FillText(pucTxt, 32);
    return pucTxt;
}

UINT8 *Msg_MakeText_TN(void)
{
    UINT8 *pucTxt = Msg_AllocBody(33);
    if (pucTxt == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    Msg_FillText(pucTxt, 32);
    return pucTxt;
}
