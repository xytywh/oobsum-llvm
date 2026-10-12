/*
 * @id      LV2-03
 * @title   memcpy_s(pkt + off, 整包大小, …)：destMax 没扣偏移，偏移在中间层又加了头长
 * @scene   S2.4  A3·M1·P1·V1·R2·X1·K0·G0
 * @year    承诺
 * @story   往报文里追加一个 IE：中间层把偏移加上 TLV 头长，最底层 destMax 仍然写整包长度。
 * @expect  TP  Pkt_Build_TP  pa=L1
 * @expect  TN  Pkt_Build_TN  pa=-
 *
 * 摘要
 *   Tlv_WriteVal(pucPkt, ulOff, pucVal, ulValLen)
 *     req R1  W128 @ pucPkt + ulOff      需 ulOff + 128 ≤ ext(pucPkt)                           ★1
 *     req R2  R ulValLen @ pucVal + 0    需 ulValLen ≤ ext(pucVal)                              ★1
 *   Tlv_WriteVal_OK
 *     destMax = 128 − ulOff：需 ulOff + (128 − ulOff) ≤ ext(pucPkt) → 先化简（ulOff 抵消）
 *     req R1' W(128 − ulOff) @ pucPkt + ulOff   需 128 ≤ ext(pucPkt)
 *   Tlv_Append(pucPkt, ulOff, …)        实参 ulOff + 4 → req R1  需 ulOff + 132 ≤ ext(pucPkt)
 *   Pkt_AddIe                            原样上传
 * 串联
 *   Pkt_Build_TP：aucPkt（128），ulOff = 16 → 148 ≤ 128 不成立 → L1；
 *                 R2 的 pucVal 是入口形参 → 停在入口，记账
 *   Pkt_Build_TN：R1' → 128 ≤ 128 成立
 */
#include "../common.h"

#define PKT_MAX_LEN 128
#define TLV_HDR_LEN 4

static void Tlv_WriteVal(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    (void)memcpy_s(pucPkt + ulOff, PKT_MAX_LEN, pucVal, ulValLen);            /* ★1 */
}

static void Tlv_WriteVal_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    (void)memcpy_s(pucPkt + ulOff, PKT_MAX_LEN - ulOff, pucVal, ulValLen);
}

static void Tlv_Append(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    pucPkt[ulOff] = 0x10;                       /* type */
    Tlv_WriteVal(pucPkt, ulOff + TLV_HDR_LEN, pucVal, ulValLen);
}

static void Tlv_Append_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)
{
    pucPkt[ulOff] = 0x10;
    Tlv_WriteVal_OK(pucPkt, ulOff + TLV_HDR_LEN, pucVal, ulValLen);
}

static void Pkt_AddIe(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen)    { Tlv_Append(pucPkt, ulOff, pucVal, ulValLen); }
static void Pkt_AddIe_OK(UINT8 *pucPkt, UINT32 ulOff, const UINT8 *pucVal, UINT32 ulValLen) { Tlv_Append_OK(pucPkt, ulOff, pucVal, ulValLen); }

void Pkt_Build_TP(const UINT8 *pucVal)
{
    UINT8 aucPkt[PKT_MAX_LEN];
    Pkt_AddIe(aucPkt, 16, pucVal, 8);
}

void Pkt_Build_TN(const UINT8 *pucVal)
{
    UINT8 aucPkt[PKT_MAX_LEN];
    Pkt_AddIe_OK(aucPkt, 16, pucVal, 8);
}
