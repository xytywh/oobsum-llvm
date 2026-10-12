/*
 * @id      LV1-04
 * @title   结构体数组成员的地址逐层下传，底层循环用了新规格的宏
 * @scene   S1.4  A1·M3·P1·V0·R1·X0·K2·G0
 * @year    承诺
 * @story   邻区规格从 16 扩到 32，新宏 MAX_NBR_NUM_V2 只改了邻区模块，小区上下文里的数组还是 16。
 *          小区建立时把 pstCell->astNbr 传给邻区模块，邻区模块按 32 个清零。
 * @expect  TP  Cell_Setup_TP  pa=L2
 * @expect  TN  Cell_Setup_TN  pa=-
 *
 * 摘要
 *   Nbr_ResetAll(pstNbr, ulNum)
 *     i ∈ [0, ulNum − 1]（符号界，narrowing 收出来）；NBR_S 8 字节，usPci 在 +0，ulFlag 在 +4
 *     req R1  ulNum ≥ 1 ⇒ W2 @ pstNbr + 8·[0, ulNum−1]       需 8·ulNum − 6 ≤ ext(pstNbr)          ★1
 *     req R2  ulNum ≥ 1 ⇒ W4 @ pstNbr + 8·[0, ulNum−1] + 4   需 8·ulNum ≤ ext(pstNbr)  （覆盖 R1，合并）★2
 *   Nbr_Clear(pstNbr)        实参 ulNum = 32 代入 → req R2  W4 @ pstNbr + [4, 252]  需 256 ≤ ext(pstNbr)
 *   Nbr_Init(pstNbr)         req R2 原样上传
 *   Cell_InitNbrList(pstCell)
 *     实参 pstCell->astNbr：大小来自成员类型 NBR_S[16] = 128 字节，在这一层就已知
 *     → 256 ≤ 128 不成立；访问范围 [0,256) 与 128 部分重叠 → L2，3 层（Cell_InitNbrList 是汇合点）
 * 串联
 *   汇合点不在入口，而在第一个"知道 astNbr 有多大"的函数。入口函数不需要任何条件。
 *   Cell_Setup_TN 走的是 Cell_InitNbrList_OK（循环用 MAX_NBR_NUM）→ 256 变成 128 → 成立。
 */
#include "../common.h"

#define MAX_NBR_NUM     16
#define MAX_NBR_NUM_V2  32

typedef struct {
    UINT16 usPci;
    UINT16 usEarfcn;
    UINT32 ulFlag;
} NBR_S;

typedef struct {
    UINT32 ulCellId;
    NBR_S  astNbr[MAX_NBR_NUM];
    UINT32 ulNbrNum;
} CELL_CTX_S;

static void Nbr_ResetAll(NBR_S *pstNbr, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        pstNbr[i].usPci  = 0xFFFF;                            /* ★1 */
        pstNbr[i].ulFlag = 0;                                 /* ★2 */
    }
}

static void Nbr_Clear(NBR_S *pstNbr)    { Nbr_ResetAll(pstNbr, MAX_NBR_NUM_V2); }
static void Nbr_ClearOk(NBR_S *pstNbr)  { Nbr_ResetAll(pstNbr, MAX_NBR_NUM); }
static void Nbr_Init(NBR_S *pstNbr)     { Nbr_Clear(pstNbr); }
static void Nbr_InitOk(NBR_S *pstNbr)   { Nbr_ClearOk(pstNbr); }

static void Cell_InitNbrList(CELL_CTX_S *pstCell)
{
    Nbr_Init(pstCell->astNbr);
    pstCell->ulNbrNum = 0;
}

static void Cell_InitNbrList_OK(CELL_CTX_S *pstCell)
{
    Nbr_InitOk(pstCell->astNbr);
    pstCell->ulNbrNum = 0;
}

void Cell_Setup_TP(CELL_CTX_S *pstCell) { Cell_InitNbrList(pstCell); }
void Cell_Setup_TN(CELL_CTX_S *pstCell) { Cell_InitNbrList_OK(pstCell); }
