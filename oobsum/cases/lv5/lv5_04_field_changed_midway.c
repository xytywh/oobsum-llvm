/*
 * @id      LV5-04
 * @title   中间层打开扩展字段时把长度加了，缓冲区没变
 * @scene   S5.4  A3·M1·P3·V3·R2·X1·K0·G1
 * @year    尽力（S5.4 里"本层先改字段再下传"的形态，用 S5.1 的帧内字段跟踪即可；被调者改写字段的形态见 LV5-02，2027H1）
 * @story   上下文在链顶正确初始化（128 / 128），中间一层"打开扩展"时把 ulBufLen 加了 16，没有换更大的缓冲区。
 * @expect  TP  Rrc_EncodeExt_TP  pa=L1
 * @expect  TN  Rrc_EncodeExt_TN  pa=-
 *
 * 摘要
 *   Enc_PutBytes(pstCtx, …)    req R1  需 pstCtx->ulBufLen ≤ ext(pstCtx->pucBuf)                   ★1
 *   Enc_WithExt(pstCtx, …)
 *     本函数先写 pstCtx->ulBufLen += 16，再调用 → 代换时 pstCtx->ulBufLen 取"调用点处的值"
 *     req R1  需 pstCtx->ulBufLen + 16 ≤ ext(pstCtx->pucBuf)     （以本函数入口时的字段值表示）
 *     eff     pstCtx->ulBufLen := pstCtx->ulBufLen + 16
 *   Enc_EncodeExt              原样上传
 * 串联
 *   Rrc_EncodeExt_TP：stCtx.ulBufLen = 128、pucBuf = aucBuf（128）→ 144 ≤ 128 不成立 → L1
 *   Rrc_EncodeExt_TN：缓冲区 144 → 成立
 */
#include "../common.h"

#define ENC_BUF_LEN  128
#define ENC_EXT_LEN  16

typedef struct {
    UINT8  *pucBuf;
    UINT32  ulBufLen;
} ENC_CTX_S;

static void Enc_PutBytes(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen)
{
    (void)memcpy_s(pstCtx->pucBuf, pstCtx->ulBufLen, pucData, ulLen);   /* ★1 */
}

static void Enc_WithExt(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen)
{
    pstCtx->ulBufLen += ENC_EXT_LEN;
    Enc_PutBytes(pstCtx, pucData, ulLen);
}

static void Enc_EncodeExt(ENC_CTX_S *pstCtx, const UINT8 *pucData, UINT32 ulLen) { Enc_WithExt(pstCtx, pucData, ulLen); }

void Rrc_EncodeExt_TP(void)
{
    UINT8     aucBuf[ENC_BUF_LEN];
    UINT8     aucIe[8] = { 0 };
    ENC_CTX_S stCtx = { aucBuf, ENC_BUF_LEN };
    Enc_EncodeExt(&stCtx, aucIe, sizeof(aucIe));
}

void Rrc_EncodeExt_TN(void)
{
    UINT8     aucBuf[ENC_BUF_LEN + ENC_EXT_LEN];
    UINT8     aucIe[8] = { 0 };
    ENC_CTX_S stCtx = { aucBuf, ENC_BUF_LEN };
    Enc_EncodeExt(&stCtx, aucIe, sizeof(aucIe));
}
