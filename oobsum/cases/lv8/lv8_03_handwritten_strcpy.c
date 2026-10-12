/*
 * @id      LV8-03
 * @title   手写的字符串拷贝循环（while (*src)），链顶传了一个比目的缓冲区长的字面量
 * @scene   S8.2+S7.5  A2·M1·P1·V1·R2·X1·K4·G0
 * @year    2027H2（字符串长度抽象 + 指针步进）
 * @story   老代码没用 strcpy_s，自己写了拷贝循环；某个调用方把一个 18 字符的默认名传进来，缓冲区 8 字节。
 * @expect  TP  Cell_SetDefault_TP  pa=记账  later=L2
 * @expect  TN  Cell_SetDefault_TN  pa=记账  later=-
 *
 * 摘要
 *   Str_CopyRaw(pszDst, pszSrc)
 *     循环次数由源串内容决定（非典型循环），两个指针同步步进
 *     年内：偏移 widening 到 [0, +∞)，不是代码给出的界 → 不报，记账"非典型循环 / 字符串长度"
 *     2027H2：req R1  W1 @ pszDst + [0, strlen(pszSrc)]   需 strlen(pszSrc) + 1 ≤ ext(pszDst)       ★1
 *   Name_Set / Cell_ApplyName     原样上传
 * 串联
 *   2027H2：TP：strlen("CELL_DEFAULT_00001") = 17 → 需 18 ≤ 8 → L2；TN："C1" → 3 ≤ 8 成立
 */
#include "../common.h"

static void Str_CopyRaw(CHAR *pszDst, const CHAR *pszSrc)
{
    while (*pszSrc != '\0') {
        *pszDst++ = *pszSrc++;                                /* ★1 */
    }
    *pszDst = '\0';
}

static void Name_Set(CHAR *pszDst, const CHAR *pszSrc)      { Str_CopyRaw(pszDst, pszSrc); }
static void Cell_ApplyName(CHAR *pszDst, const CHAR *pszSrc) { Name_Set(pszDst, pszSrc); }

void Cell_SetDefault_TP(void)
{
    CHAR szName[8];
    Cell_ApplyName(szName, "CELL_DEFAULT_00001");
}

void Cell_SetDefault_TN(void)
{
    CHAR szName[8];
    Cell_ApplyName(szName, "C1");
}
