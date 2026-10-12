/*
 * @id      LV5-01
 * @title   编码上下文在链顶填好（缓冲区 + 长度），长度用了新宏，缓冲区还是老大小
 * @scene   S5.1  A3·M1·P3·V3·R2·X0·K0·G0
 * @year    承诺
 * @story   编码器的上下文结构体一路以指针下传，最底层按 pstCtx->ulBufLen 当 destMax。
 *          链顶初始化时长度写了 ENC_BUF_LEN（256），缓冲区数组是 ENC_BUF_LEN_OLD（128）。
 * @expect  TP  Rrc_EncodeMsg_TP  pa=L1
 * @expect  TN  Rrc_EncodeMsg_TN  pa=-
 *
 * 摘要（基址和长度都是"形参指向的结构体的字段"——字段级 AccessPath）
 *   Enc_PutBytes(pstCtx, pucData, ulLen)
 *     req R1  W pstCtx->ulBufLen @ pstCtx->pucBuf + 0     需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)  ★1
 *     req R2  R ulLen @ pucData + 0                        需 ulLen ≤ ext(pucData)                  ★1
 *   Enc_PutIe / Enc_Encode                                 原样上传（字段路径不变，形参换名）
 * 串联
 *   Rrc_EncodeMsg_TP：实参 &stCtx；帧内记录了 stCtx.pucBuf = aucBuf（ext 128）、stCtx.ulBufLen = 256
 *                     → R1 代入 → 256 ≤ 128 不成立 → L1，3 层
 *                     R2：pucData = aucIe（16），ulLen = 16 → 成立
 *   Rrc_EncodeMsg_TN：ulBufLen = sizeof(aucBuf) → 成立
 *   要点：要在链顶函数里记住"局部结构体的字段现在指向谁、值是多少"（帧内字段存储）。
 */
#include "../common.h"

#define ENC_BUF_LEN_OLD  128
#define ENC_BUF_LEN      256

typedef struct {
    UINT8  *pucBuf;
    UINT32  ulBufLen;
    UINT32  ulUsed;
} ENC_CTX_S;

static void Enc_PutBytes(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen)
{
    (void)memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen);   /* ★1 */
    pstCtx->ulUsed = ulLen;
}

static void Enc_PutIe(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen)  { Enc_PutBytes(pstCtx, pucIe, ulLen); }
static void Enc_Encode(ENC_CTX_S *pstCtx, const UINT8 *pucIe, UINT32 ulLen) { Enc_PutIe(pstCtx, pucIe, ulLen); }

void Rrc_EncodeMsg_TP(void)
{
    UINT8     aucBuf[ENC_BUF_LEN_OLD];
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    stCtx.pucBuf   = aucBuf;
    stCtx.ulBufLen = ENC_BUF_LEN;
    stCtx.ulUsed   = 0;
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}

void Rrc_EncodeMsg_TN(void)
{
    UINT8     aucBuf[ENC_BUF_LEN_OLD];
    UINT8     aucIe[16] = { 0 };
    ENC_CTX_S stCtx;
    stCtx.pucBuf   = aucBuf;
    stCtx.ulBufLen = sizeof(aucBuf);
    stCtx.ulUsed   = 0;
    Enc_Encode(&stCtx, aucIe, sizeof(aucIe));
}
