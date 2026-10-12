/*
 * @id      LV2-06
 * @title   置位图：上层按扩容后的 UE 规格循环，下层按 idx / 32 定位字
 * @scene   S2.7+S3.9  A1·M1·P1·V1·R2·X3·K2·G0
 * @year    承诺
 * @story   UE 规格从 64 扩到 256，活动 UE 刷新的循环改成了 MAX_UE_NUM_EX，位图还是 64 位。
 * @expect  TP  Cell_RefreshActive_TP  pa=L2
 * @expect  TN  Cell_RefreshActive_TN  pa=-
 *
 * 摘要
 *   Bit_Set(pulMap, ulIdx)
 *     req R1  W4 @ pulMap + 4·(ulIdx / 32)     需 4·(ulIdx/32) + 4 ≤ ext(pulMap)                 ★1
 *   Ue_MarkActive / Cell_MarkUe                原样上传
 * 串联
 *   Cell_RefreshActive_TP：调用点在循环里，ulUe ∈ [0, 255] → ulUe/32 ∈ [0, 7] → 需 32 ≤ 8；
 *                          最小那次安全 → L2（调用点的区间代入，不需要知道循环跑了几次）
 *   Cell_RefreshActive_TN：ulUe ∈ [0, 63] → 需 8 ≤ 8 成立
 *   为什么不用 UINT8 下标举例：类型自带的范围不算"代码给出的上界"（§1.3 的口径），
 *   否则每个 UINT8 / UINT16 字段都会变成 L2。这里的上界来自循环条件。
 */
#include "../common.h"

#define MAX_UE_NUM     64
#define MAX_UE_NUM_EX  256

static void Bit_Set(UINT32 *pulMap, UINT32 ulIdx)
{
    pulMap[ulIdx / 32] |= (1u << (ulIdx % 32));               /* ★1 */
}

static void Ue_MarkActive(UINT32 *pulMap, UINT32 ulUeIdx) { Bit_Set(pulMap, ulUeIdx); }
static void Cell_MarkUe(UINT32 *pulMap, UINT32 ulUeIdx)   { Ue_MarkActive(pulMap, ulUeIdx); }

void Cell_RefreshActive_TP(void)
{
    UINT32 aulMap[MAX_UE_NUM / 32] = { 0 };
    UINT32 ulUe;
    for (ulUe = 0; ulUe < MAX_UE_NUM_EX; ulUe++) {
        Cell_MarkUe(aulMap, ulUe);
    }
}

void Cell_RefreshActive_TN(void)
{
    UINT32 aulMap[MAX_UE_NUM / 32] = { 0 };
    UINT32 ulUe;
    for (ulUe = 0; ulUe < MAX_UE_NUM; ulUe++) {
        Cell_MarkUe(aulMap, ulUe);
    }
}
