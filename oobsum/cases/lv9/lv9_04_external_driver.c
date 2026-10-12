/*
 * @id      LV9-04
 * @title   BSP 驱动接口只有声明：按 ulLen 往 pucBuf 读寄存器，调用方缓冲区 16、长度 32
 * @scene   S9.4  A3·M1·P5·V1·R2·X0·K0·G0
 * @year    模型表（程序分析） + AI 生成模型草稿
 * @story   驱动库是二进制交付的，头文件注释写了"pucBuf 至少 ulLen 字节"。
 * @expect  TP  Om_ReadChip_TP  pa=记账  ai+pa=L1
 * @expect  TN  Om_ReadChip_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Drv_ReadReg                  无函数体、无模型 → 断链，记账"未知函数"
 *     模型（summary_models.yaml，来源 = llm，人工确认后生效）：
 *       - name: Drv_ReadReg
 *         conditions: [ { base: param1, bytes: "param2", kind: W } ]
 *     有模型后：req R1  W ulLen @ pucBuf + 0     需 ulLen ≤ ext(pucBuf)                                ★1
 *   Chip_ReadStatus / Om_CollectChip   原样上传
 * 串联
 *   有模型：TP：16 < 32 → L1；TN：ulLen 16 → 成立
 */
#include "../common.h"

/* BSP 库：从 ulRegBase 开始读 ulLen 字节到 pucBuf。pucBuf 至少 ulLen 字节，由调用者保证。 */
UINT32 Drv_ReadReg(UINT32 ulRegBase, UINT8 *pucBuf, UINT32 ulLen);

static UINT32 Chip_ReadStatus(UINT8 *pucBuf, UINT32 ulLen) { return Drv_ReadReg(0x40000000u, pucBuf, ulLen); }   /* ★1 */
static UINT32 Om_CollectChip(UINT8 *pucBuf, UINT32 ulLen)  { return Chip_ReadStatus(pucBuf, ulLen); }
static UINT32 Om_ReadAll(UINT8 *pucBuf, UINT32 ulLen)      { return Om_CollectChip(pucBuf, ulLen); }

UINT32 Om_ReadChip_TP(void)
{
    UINT8 aucStatus[16];
    return Om_ReadAll(aucStatus, 32);
}

UINT32 Om_ReadChip_TN(void)
{
    UINT8 aucStatus[16];
    return Om_ReadAll(aucStatus, sizeof(aucStatus));
}
