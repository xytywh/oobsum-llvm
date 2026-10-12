/*
 * @id      LV5-02
 * @title   上下文由独立的 Init 函数填：长度多算了尾部，缓冲区按原长申请（写效应）
 * @scene   S5.2  A3·M5·P3+P2·V3·R3·X1·K0·G0
 * @year    2027H1
 * @story   与 LV5-01 同一类错误，区别是缓冲区和长度在 Enc_Init 里被写进结构体字段——
 *          链顶看到的只是一次函数调用。
 * @expect  TP  Rrc_EncodeSib_TP  pa=记账  later=L1
 * @expect  TN  Rrc_EncodeSib_TN  pa=记账  later=-
 *
 * 摘要
 *   Enc_Init(pstCtx, ulLen)
 *     eff（写效应，在 ret == VOS_OK 时成立）
 *         pstCtx->pucBuf   := new(ulLen)
 *         pstCtx->ulBufLen := ulLen + 4
 *         pstCtx->ulUsed   := 0
 *   Enc_PutBytes / Enc_PutIe / Enc_Encode   同 LV5-01：req R1  需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)
 * 串联
 *   Rrc_EncodeSib_TP：Enc_Init(&stCtx, 128) 返回 OK 的分支上，把写效应应用到 stCtx
 *                     → pucBuf = new(128)、ulBufLen = 132 → R1：132 ≤ 128 不成立 → L1
 *   年内（没有写效应）：stCtx 的字段在调用后变成未知 → R1 停在链顶判不了 → 不报，记账"字段被调用改写"（漏报，不误报）
 */
#include "../common.h"

#define ENC_TAIL_LEN 4

typedef struct {
    UINT8  *pucBuf;
    UINT32  ulBufLen;
    UINT32  ulUsed;
} ENC_CTX_S;

static UINT32 Enc_Init(ENC_CTX_S *pstCtx, UINT32 ulLen)
{
    pstCtx->pucBuf = (UINT8 *)Pub_MemAlloc(ulLen);
    if (pstCtx->pucBuf == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    pstCtx->ulBufLen = ulLen + ENC_TAIL_LEN;
    pstCtx->ulUsed   = 0;
    return VOS_OK;
}

static UINT32 Enc_Init_OK(ENC_CTX_S *pstCtx, UINT32 ulLen)
{
    pstCtx->pucBuf = (UINT8 *)Pub_MemAlloc(ulLen + ENC_TAIL_LEN);
    if (pstCtx->pucBuf == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    pstCtx->ulBufLen = ulLen + ENC_TAIL_LEN;
    pstCtx->ulUsed   = 0;
    return VOS_OK;
}

static void Enc_PutBytes(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen)
{
    (void)memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen);   /* ★1 */
}

static void Enc_PutIe(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen)  { Enc_PutBytes(pstCtx, pucIe, ulLen); }
static void Enc_Encode(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen) { Enc_PutIe(pstCtx, pucIe, ulLen); }

void Rrc_EncodeSib_TP(void)
{
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    if (Enc_Init(&stCtx, 128) != VOS_OK) {
        return;
    }
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}

void Rrc_EncodeSib_TN(void)
{
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    if (Enc_Init_OK(&stCtx, 128) != VOS_OK) {
        return;
    }
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}
