/*
 * @id      LV3-09
 * @title   调度网格清零：行列宏写反（嵌套循环，各维界是常量）
 * @scene   S3.6  A1·M1·P1·V0·R1·X1·K2·G0
 * @year    承诺
 * @story   网格按 SLOT_NUM × RB_NUM 定义，按行优先线性化；清零时外层循环用了 RB_NUM。
 * @expect  TP  Sched_InitGrid_TP  pa=L2
 * @expect  TN  Sched_InitGrid_TN  pa=-
 *
 * 摘要
 *   Grid_Clear(pucGrid)
 *     s ∈ [0, 7]，r ∈ [0, 3]；s·8 + r ∈ [0, 59]（乘常量、加区间都是精确的）
 *     req R1  W1 @ pucGrid + [0, 59]       需 60 ≤ ext(pucGrid)                                      ★1
 *   Grid_Clear_OK                         s ∈ [0, 3] → [0, 27]  需 28 ≤ ext(pucGrid)
 *   Sched_ResetGrid / Sched_PrepareSlot   原样上传
 * 串联
 *   Sched_InitGrid_TP：aucGrid（32）→ 60 ≤ 32 不成立，最小访问安全 → L2
 *   Sched_InitGrid_TN：28 ≤ 32 成立
 *   要点：r * COLS 里 COLS 是常量才这么精确；COLS 换成变量就是 S7.6 / S8.3（不承诺）。
 */
#include "../common.h"

#define SLOT_NUM 4
#define RB_NUM   8

static void Grid_Clear(UINT8 *pucGrid)
{
    UINT32 s, r;
    for (s = 0; s < RB_NUM; s++) {
        for (r = 0; r < SLOT_NUM; r++) {
            pucGrid[s * RB_NUM + r] = 0;                      /* ★1 */
        }
    }
}

static void Grid_Clear_OK(UINT8 *pucGrid)
{
    UINT32 s, r;
    for (s = 0; s < SLOT_NUM; s++) {
        for (r = 0; r < RB_NUM; r++) {
            pucGrid[s * RB_NUM + r] = 0;
        }
    }
}

static void Sched_ResetGrid(UINT8 *pucGrid)      { Grid_Clear(pucGrid); }
static void Sched_ResetGrid_OK(UINT8 *pucGrid)   { Grid_Clear_OK(pucGrid); }
static void Sched_PrepareSlot(UINT8 *pucGrid)    { Sched_ResetGrid(pucGrid); }
static void Sched_PrepareSlot_OK(UINT8 *pucGrid) { Sched_ResetGrid_OK(pucGrid); }

void Sched_InitGrid_TP(void)
{
    UINT8 aucGrid[SLOT_NUM * RB_NUM];
    Sched_PrepareSlot(aucGrid);
}

void Sched_InitGrid_TN(void)
{
    UINT8 aucGrid[SLOT_NUM * RB_NUM];
    Sched_PrepareSlot_OK(aucGrid);
}
