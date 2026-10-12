/*
 * @id      LV8-02
 * @title   两遍式 TLV：第一遍校验总长，第二遍按字节搬运时不再逐个校验
 * @scene   S8.1（跨轮 / 跨循环关系）  A1·M1·P1·V5·R2·X1·K3·G4
 * @year    AI + 程序分析
 * @story   第二遍的安全性靠"第一遍算过 Σlen ≤ cap"。缺陷版本里第一遍只加了 len，
 *          第二遍却把 2 字节头也搬了进去。
 * @expect  TP  Om_OnBatchMsg_TP  pa=记账  ai+pa=L2
 * @expect  TN  Om_OnBatchMsg_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Tlv_SumLen(pucBuf, ulTotal, ulCap)   eff ret == VOS_OK ⇒ ⟨第一遍的 Σ⟩ ≤ ulCap   （关于"另一个循环"的事实，摘要表达不了）
 *   Tlv_Gather(pucBuf, ulTotal, pucDst)
 *     ulDOff 在循环里被 widening 到 [0, +∞)，且与 ulTotal、ulCap 没有可用的线性关系
 *     → 无穷上界来自 widening，不是代码给出的界 → 不报 L2，记账"跨循环关系"（不误报，也查不出 TP）
 * 串联
 *   年内：TP、TN 都只记账
 *   AI + 程序分析：AI 识别"两遍式"模式，提出不变式"第二遍写入总量 = 第一遍校验过的量"；
 *     程序分析检查两遍是否对同一序列、同一长度表达式求和：
 *       TN：两遍都是 Σ len → 不变式成立 → 安全
 *       TP：第一遍 Σ len、第二遍 Σ (2 + len) → 不一致 → L2
 */
#include "../common.h"

#define BATCH_CAP 128

static UINT32 Tlv_SumLen(const UINT8 *pucBuf, UINT32 ulTotal, UINT32 ulCap)
{
    UINT32 ulOff = 0, ulSum = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + 2 + ulLen > ulTotal) {
            return VOS_ERR;
        }
        ulSum += ulLen;
        ulOff += 2 + ulLen;
    }
    return (ulSum <= ulCap) ? VOS_OK : VOS_ERR;
}

static void Tlv_Gather(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucDst, UINT32 bWithHdr)
{
    UINT32 ulOff = 0, ulDOff = 0, j;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen  = pucBuf[ulOff + 1];
        UINT32 ulCopy = bWithHdr ? (2 + ulLen) : ulLen;
        UINT32 ulSrc  = bWithHdr ? ulOff : (ulOff + 2);
        if (ulOff + 2 + ulLen > ulTotal) {
            break;
        }
        for (j = 0; j < ulCopy; j++) {
            pucDst[ulDOff + j] = pucBuf[ulSrc + j];          /* ★1 */
        }
        ulDOff += ulCopy;
        ulOff  += 2 + ulLen;
    }
}

static void Batch_Unpack(const UINT8 *p, UINT32 n, UINT8 *d, UINT32 b) { Tlv_Gather(p, n, d, b); }
static void Om_HandleBatch(const UINT8 *p, UINT32 n, UINT8 *d, UINT32 b) { Batch_Unpack(p, n, d, b); }

void Om_OnBatchMsg_TP(const UINT8 *pucMsg, UINT32 ulMsgLen)
{
    UINT8 aucDst[BATCH_CAP];
    if (Tlv_SumLen(pucMsg, ulMsgLen, sizeof(aucDst)) != VOS_OK) {
        return;
    }
    Om_HandleBatch(pucMsg, ulMsgLen, aucDst, VOS_TRUE);
}

void Om_OnBatchMsg_TN(const UINT8 *pucMsg, UINT32 ulMsgLen)
{
    UINT8 aucDst[BATCH_CAP];
    if (Tlv_SumLen(pucMsg, ulMsgLen, sizeof(aucDst)) != VOS_OK) {
        return;
    }
    Om_HandleBatch(pucMsg, ulMsgLen, aucDst, VOS_FALSE);
}
