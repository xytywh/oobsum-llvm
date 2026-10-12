/*
 * @id      LV7-05
 * @title   strcpy_s 的 destMax 用了名字的最大规格，目的缓冲区是老长度
 * @scene   S7.5（destMax 那一半）  A4·M1·P1·V0·R1·X0·K0·G0
 * @year    计划 P1（destMax 一半与 memcpy_s 相同；源串长度那一半 2027H2）
 * @story   小区名规格从 16 扩到 32，拷贝函数统一用 CELL_NAME_LEN_MAX 当 destMax。
 * @expect  TP  Cell_Rename_TP  pa=-  later=L1
 * @expect  TN  Cell_Rename_TN  pa=-
 *
 * 摘要
 *   Name_Copy(pszDst, pszSrc)
 *     req R1  W32 @ pszDst + 0     需 32 ≤ ext(pszDst)          （destMax；出错路径会按 destMax 清零）        ★1
 *     req R2  需 strlen(pszSrc) + 1 ≤ 32                          （源串那一半，要字符串长度抽象，2027H2）
 *   Cell_SetName / Cell_UpdateName    原样上传
 * 串联
 *   P1 之后：Cell_Rename_TP：szName（16）→ 32 ≤ 16 不成立 → L1
 *   年内（P0 只有 memcpy_s / memmove_s / memset_s 模型）：strcpy_s 没有模型 → 不报
 */
#include "../common.h"

#define CELL_NAME_LEN      16
#define CELL_NAME_LEN_MAX  32

static void Name_Copy(CHAR *pszDst, const CHAR *pszSrc)
{
    (void)strcpy_s(pszDst, CELL_NAME_LEN_MAX, pszSrc);       /* ★1 */
}

static void Name_Copy_OK(CHAR *pszDst, const CHAR *pszSrc)
{
    (void)strcpy_s(pszDst, CELL_NAME_LEN, pszSrc);
}

static void Cell_SetName(CHAR *pszDst, const CHAR *pszSrc)       { Name_Copy(pszDst, pszSrc); }
static void Cell_SetName_OK(CHAR *pszDst, const CHAR *pszSrc)    { Name_Copy_OK(pszDst, pszSrc); }
static void Cell_UpdateName(CHAR *pszDst, const CHAR *pszSrc)    { Cell_SetName(pszDst, pszSrc); }
static void Cell_UpdateName_OK(CHAR *pszDst, const CHAR *pszSrc) { Cell_SetName_OK(pszDst, pszSrc); }

void Cell_Rename_TP(const CHAR *pszNew)
{
    CHAR szName[CELL_NAME_LEN];
    Cell_UpdateName(szName, pszNew);
}

void Cell_Rename_TN(const CHAR *pszNew)
{
    CHAR szName[CELL_NAME_LEN];
    Cell_UpdateName_OK(szName, pszNew);
}
