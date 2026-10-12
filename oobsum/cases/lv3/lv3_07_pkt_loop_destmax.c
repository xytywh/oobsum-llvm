/*
 * @id      LV3-07
 * @title   报文组装循环：按剩余长度拷贝（安全） vs. destMax 用整包长度（缺陷）
 * @scene   S3.7+S3.8  A3·M1·P1·V1·R2·X1·K3·G1
 * @year    承诺（TN）/ 尽力（TP）
 * @story   最常见的报文组装写法。安全写法里 destMax = 总长 − 偏移；有人复制粘贴时漏了 − 偏移。
 * @expect  TP  Msg_Encode_TP  pa=L2
 * @expect  TN  Msg_Encode_TN  pa=-
 *
 * 摘要
 *   Pkt_PutIes_OK(pucPkt, ulPktLen, astIe, ulIeNum)
 *     访问点：memcpy_s(pucPkt + ulOff, ulPktLen − ulOff, …)
 *     先符号化简：ulOff + (ulPktLen − ulOff) = ulPktLen，ulOff 抵消，与它的循环区间无关
 *     req R1  W(ulPktLen − ulOff) @ pucPkt + ulOff     化简后 需 ulPktLen ≤ ext(pucPkt)            ★1
 *     req R2  R astIe[i].ulLen @ astIe[i].pucVal       基址来自数组元素的字段 → 不跟踪，记账
 *   Pkt_PutIes(pucPkt, ulPktLen, …)
 *     访问点：memcpy_s(pucPkt + ulOff, ulPktLen, …)；循环内校验 ulOff + len ≤ ulPktLen 给出 ulOff ∈ [0, ulPktLen]
 *     req R1  W ulPktLen @ pucPkt + [0, ulPktLen]     需 2·ulPktLen ≤ ext(pucPkt)，最小访问需 ulPktLen ≤ ext ★2
 *   Body_PutIes / Msg_BuildBody          原样上传
 * 串联
 *   Msg_Encode_TN：aucPkt（128），ulPktLen = 128 → 128 ≤ 128 成立 → 不报
 *   Msg_Encode_TP：→ 需 256 ≤ 128 不成立，ulOff = 0 那次成立 → L2
 *   要点：TN 一侧不需要任何循环不变式；不先化简、直接拿 ulOff 的区间去比，这里会误报。
 */
#include "../common.h"

#define PKT_LEN 128

typedef struct {
    const UINT8 *pucVal;
    UINT32       ulLen;
} IE_S;

static UINT32 Pkt_PutIes_OK(UINT8 *pucPkt, UINT32 ulPktLen, const IE_S *astIe, UINT32 ulIeNum)
{
    UINT32 i;
    UINT32 ulOff = 0;
    for (i = 0; i < ulIeNum; i++) {
        if (ulOff + astIe[i].ulLen > ulPktLen) {
            break;
        }
        if (memcpy_s(pucPkt + ulOff, ulPktLen - ulOff, astIe[i].pucVal, astIe[i].ulLen) != EOK) {   /* ★1 */
            break;
        }
        ulOff += astIe[i].ulLen;
    }
    return ulOff;
}

static UINT32 Pkt_PutIes(UINT8 *pucPkt, UINT32 ulPktLen, const IE_S *astIe, UINT32 ulIeNum)
{
    UINT32 i;
    UINT32 ulOff = 0;
    for (i = 0; i < ulIeNum; i++) {
        if (ulOff + astIe[i].ulLen > ulPktLen) {
            break;
        }
        if (memcpy_s(pucPkt + ulOff, ulPktLen, astIe[i].pucVal, astIe[i].ulLen) != EOK) {          /* ★2 */
            break;
        }
        ulOff += astIe[i].ulLen;
    }
    return ulOff;
}

static UINT32 Body_PutIes(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)       { return Pkt_PutIes(p, n, a, k); }
static UINT32 Body_PutIes_OK(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)    { return Pkt_PutIes_OK(p, n, a, k); }
static UINT32 Msg_BuildBody(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)     { return Body_PutIes(p, n, a, k); }
static UINT32 Msg_BuildBody_OK(UINT8 *p, UINT32 n, const IE_S *a, UINT32 k)  { return Body_PutIes_OK(p, n, a, k); }

UINT32 Msg_Encode_TP(const IE_S *astIe, UINT32 ulIeNum)
{
    UINT8 aucPkt[PKT_LEN];
    return Msg_BuildBody(aucPkt, sizeof(aucPkt), astIe, ulIeNum);
}

UINT32 Msg_Encode_TN(const IE_S *astIe, UINT32 ulIeNum)
{
    UINT8 aucPkt[PKT_LEN];
    return Msg_BuildBody_OK(aucPkt, sizeof(aucPkt), astIe, ulIeNum);
}
