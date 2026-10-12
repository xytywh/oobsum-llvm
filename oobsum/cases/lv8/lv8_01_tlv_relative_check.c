/*
 * @id      LV8-01
 * @title   TLV 解析循环：每轮的校验漏算了 2 字节 TLV 头（读越界）
 * @scene   S8.1（相对条件写法）  A3·M1·P1·V5·R2·X1·K3·G1
 * @year    尽力
 * @story   步长是从缓冲区里读出来的长度（数据驱动）。安全与否不需要跨轮不变式：
 *          只看"本轮的访问是否被本轮的校验覆盖"。
 * @expect  TP  Om_OnCfgMsg_TP  pa=L2
 * @expect  TN  Om_OnCfgMsg_TN  pa=-
 *
 * 摘要
 *   Tlv_ParseAll(pucBuf, ulTotal, pucOut)
 *     ulLen = pucBuf[ulOff + 1] 是缓冲区内容（无约束符号）；循环里 ulOff 被 widening，但我们不用它的区间——
 *     本轮访问的末端 E = ulOff + 2 + ulLen，本轮守卫给出 ulOff + ulLen ≤ ulTotal（线性关系）
 *     → 访问末端 E ≤ ulTotal + 2，最小 2
 *     req R1  R ulLen @ pucBuf + ulOff + 2   末端 ∈ [2, ulTotal + 2]   需 ulTotal + 2 ≤ ext(pucBuf)       ★1
 *   Tlv_ParseAll_OK：守卫 ulOff + 2 + ulLen ≤ ulTotal → 末端 ≤ ulTotal → 需 ulTotal ≤ ext(pucBuf)
 *   Msg_ParseBody / Om_HandleCfg     原样上传
 * 串联
 *   Om_OnCfgMsg_TP：aucMsg（64），ulTotal = 64 → 需 66 ≤ 64 不成立，最小末端 2 成立 → L2
 *   Om_OnCfgMsg_TN：需 64 ≤ 64 成立
 *   要点：用的是"守卫蕴含条件"（§3.3 的条件带守卫），不是循环不变式，所以年内可以尽力做。
 */
#include "../common.h"

static void Tlv_ParseAll(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
{
    UINT32 ulOff = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + ulLen > ulTotal) {                        /* 漏了 + 2 */
            break;
        }
        (void)memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen);   /* ★1 */
        ulOff += 2 + ulLen;
    }
}

static void Tlv_ParseAll_OK(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
{
    UINT32 ulOff = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + 2 + ulLen > ulTotal) {
            break;
        }
        (void)memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen);
        ulOff += 2 + ulLen;
    }
}

static void Msg_ParseBody(const UINT8 *p, UINT32 n, UINT8 *o)    { Tlv_ParseAll(p, n, o); }
static void Msg_ParseBody_OK(const UINT8 *p, UINT32 n, UINT8 *o) { Tlv_ParseAll_OK(p, n, o); }
static void Om_HandleCfg(const UINT8 *p, UINT32 n, UINT8 *o)     { Msg_ParseBody(p, n, o); }
static void Om_HandleCfg_OK(const UINT8 *p, UINT32 n, UINT8 *o)  { Msg_ParseBody_OK(p, n, o); }

void Om_OnCfgMsg_TP(const UINT8 *pucIn)
{
    UINT8 aucMsg[64];
    UINT8 aucOut[256];
    (void)memcpy_s(aucMsg, sizeof(aucMsg), pucIn, sizeof(aucMsg));
    Om_HandleCfg(aucMsg, sizeof(aucMsg), aucOut);
}

void Om_OnCfgMsg_TN(const UINT8 *pucIn)
{
    UINT8 aucMsg[64];
    UINT8 aucOut[256];
    (void)memcpy_s(aucMsg, sizeof(aucMsg), pucIn, sizeof(aucMsg));
    Om_HandleCfg_OK(aucMsg, sizeof(aucMsg), aucOut);
}
