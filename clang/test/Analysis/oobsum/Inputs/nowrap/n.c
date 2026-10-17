/* assume-nowrap.test 的输入（SPEC-0108）：检查里可能回绕的写法（LV8-01、LV3-07 同类）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
int memcpy_s(void *dest, UINT32 destMax, const void *src, UINT32 count);
static void Tlv(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
{
    UINT32 ulOff = 0;
    while (ulOff + 2 <= ulTotal) {
        UINT32 ulLen = pucBuf[ulOff + 1];
        if (ulOff + ulLen > ulTotal) {
            break;
        }
        (void)memcpy_s(pucOut, 256, pucBuf + ulOff + 2, ulLen);
        ulOff += 2 + ulLen;
    }
}
static void Tlv_OK(const UINT8 *pucBuf, UINT32 ulTotal, UINT8 *pucOut)
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
void Tlv_TP(void) { UINT8 aucMsg[64]; UINT8 aucOut[256]; Tlv(aucMsg, sizeof(aucMsg), aucOut); }
void Tlv_TN(void) { UINT8 aucMsg[64]; UINT8 aucOut[256]; Tlv_OK(aucMsg, sizeof(aucMsg), aucOut); }
static void Put(UINT8 *pucPkt, UINT32 ulPktLen, const UINT8 *pucSrc, UINT32 ulNum)
{
    UINT32 ulOff = 0;
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        UINT32 ulLen = pucSrc[i];
        if (ulOff + ulLen > ulPktLen) {
            break;
        }
        (void)memcpy_s(pucPkt + ulOff, ulPktLen, pucSrc, ulLen);
        ulOff += ulLen;
    }
}
static void Put_OK(UINT8 *pucPkt, UINT32 ulPktLen, const UINT8 *pucSrc, UINT32 ulNum)
{
    UINT32 ulOff = 0;
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        UINT32 ulLen = pucSrc[i];
        if (ulOff + ulLen > ulPktLen) {
            break;
        }
        (void)memcpy_s(pucPkt + ulOff, ulPktLen - ulOff, pucSrc, ulLen);
        ulOff += ulLen;
    }
}
void Put_TP(const UINT8 *pucSrc, UINT32 ulNum) { UINT8 aucPkt[128]; Put(aucPkt, sizeof(aucPkt), pucSrc, ulNum); }
void Put_TN(const UINT8 *pucSrc, UINT32 ulNum) { UINT8 aucPkt[128]; Put_OK(aucPkt, sizeof(aucPkt), pucSrc, ulNum); }
