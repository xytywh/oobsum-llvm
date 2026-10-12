/*
 * @id      LV3-05
 * @title   填满后补结尾符：循环退出时计数器等于长度
 * @scene   S3.5  A1·M1·P1·V1·R2·X0·K2·G0
 * @year    承诺
 * @story   小区名按长度填空格再补 '\0'，调用方传的长度是 sizeof(缓冲区)，没有给 '\0' 留位置。
 * @expect  TP  Cell_SetDefaultName_TP  pa=L1
 * @expect  TN  Cell_SetDefaultName_TN  pa=-
 *
 * 摘要
 *   Name_Pad(pszName, ulLen)
 *     循环里 i ∈ [0, ulLen − 1]；退出时用退出条件收窄（narrowing）：i ≥ ulLen 且 i ≤ ulLen → i == ulLen
 *     req R1  ulLen ≥ 1 ⇒ W1 @ pszName + [0, ulLen−1]   需 ulLen ≤ ext(pszName)
 *     req R2  W1 @ pszName + ulLen                     需 ulLen + 1 ≤ ext(pszName)   （覆盖 R1）  ★1
 *   Name_Init / Cell_InitName             原样上传
 * 串联
 *   Cell_SetDefaultName_TP：szName（16），ulLen = 16 → 17 ≤ 16 不成立 → L1（退出后的那次访问是确定值）
 *   Cell_SetDefaultName_TN：ulLen = 15 → 16 ≤ 16 成立
 */
#include "../common.h"

#define CELL_NAME_LEN 16

static void Name_Pad(CHAR *pszName, UINT32 ulLen)
{
    UINT32 i;
    for (i = 0; i < ulLen; i++) {
        pszName[i] = ' ';
    }
    pszName[i] = '\0';                                        /* ★1 */
}

static void Name_Init(CHAR *pszName, UINT32 ulLen)     { Name_Pad(pszName, ulLen); }
static void Cell_InitName(CHAR *pszName, UINT32 ulLen) { Name_Init(pszName, ulLen); }

void Cell_SetDefaultName_TP(void)
{
    CHAR szName[CELL_NAME_LEN];
    Cell_InitName(szName, sizeof(szName));
}

void Cell_SetDefaultName_TN(void)
{
    CHAR szName[CELL_NAME_LEN];
    Cell_InitName(szName, sizeof(szName) - 1);
}
