/*
 * @id      LV10-02
 * @title   平台 API 的"缓冲区 + 长度"约定只写在注释里，调用方传的长度大于缓冲区
 * @scene   S10.2  A3·M1·P5·V0·R1·X0·K0·G4
 * @year    AI + 程序分析
 * @story   平台接口 Plat_GetDevInfo 二进制交付，注释写"ulBufLen 为 pucBuf 的字节数，不小于 DEV_INFO_LEN"。
 * @expect  TP  Om_QueryDev_TP  pa=记账  ai+pa=L1
 * @expect  TN  Om_QueryDev_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Plat_GetDevInfo     无函数体 → 断链；AI 从注释抽出参数配对 pucBuf ↔ ulBufLen，生成模型：
 *                       W ulBufLen @ param0（写满 ulBufLen 字节）；前置条件 ulBufLen ≥ 64
 *   Dev_Read(pucBuf, ulBufLen)   有模型后 req R1  需 ulBufLen ≤ ext(pucBuf)                          ★1
 *   Dev_Query / Om_GetDev        原样上传
 * 串联
 *   AI + 程序分析：TP：aucInfo 32、ulBufLen 64 → 64 ≤ 32 不成立 → L1；TN：aucInfo 64 → 成立
 *   与 LV9-04 的区别：这里不仅要模型，还要从注释里读出"哪个参数是哪个缓冲区的长度"。
 */
#include "../common.h"

#define DEV_INFO_LEN 64

/* 平台 API：把设备信息写入 pucBuf。ulBufLen 为 pucBuf 的字节数，不小于 DEV_INFO_LEN，由调用者保证。 */
UINT32 Plat_GetDevInfo(UINT8 *pucBuf, UINT32 ulBufLen);

static UINT32 Dev_Read(UINT8 *pucBuf, UINT32 ulBufLen)  { return Plat_GetDevInfo(pucBuf, ulBufLen); }   /* ★1 */
static UINT32 Dev_Query(UINT8 *pucBuf, UINT32 ulBufLen) { return Dev_Read(pucBuf, ulBufLen); }
static UINT32 Om_GetDev(UINT8 *pucBuf, UINT32 ulBufLen) { return Dev_Query(pucBuf, ulBufLen); }

UINT32 Om_QueryDev_TP(void)
{
    UINT8 aucInfo[32];
    return Om_GetDev(aucInfo, DEV_INFO_LEN);
}

UINT32 Om_QueryDev_TN(void)
{
    UINT8 aucInfo[DEV_INFO_LEN];
    return Om_GetDev(aucInfo, sizeof(aucInfo));
}
