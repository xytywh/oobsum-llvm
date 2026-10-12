/*
 * @id      LV7-02
 * @title   二维数组行级越界：列下标多走一个，越进了下一行（整数组范围内）
 * @scene   S7.2  A1·M7·P0·V1·R1·X1·K2·G0
 * @year    计划 P1（CSA 盲区 #18，1–2 层也报）
 * @story   调度表 g_aucSched[SLOT][RB]；某个时隙的 RB 循环写成了 <= RB_NUM。
 *          按整个数组算偏移 1·8 + 8 = 16 < 32 不越界；按行算第 8 列已经越界。
 * @expect  TP  Sched_OnSlot_TP  pa=-  later=L2
 * @expect  TN  Sched_OnSlot_TN  pa=-
 *
 * 摘要
 *   Sched_Mark(ulSlot, ulRb)
 *     年内（整数组口径）：req R1  W1 @ g_aucSched + 8·ulSlot + ulRb    需 8·ulSlot + ulRb + 1 ≤ 32      ★1
 *     P1（逐维口径）：再加 req R2  ulRb ≤ 7、req R3  ulSlot ≤ 3
 *   Sched_MarkRbs(ulSlot, ulRbNum)   调用点 ulRb ∈ [0, ulRbNum] 代入
 *   Sched_Alloc                      原样上传
 * 串联
 *   Sched_OnSlot_TP：ulSlot = 1，ulRbNum = 8 → R1：8 + 8 + 1 = 17 ≤ 32 成立（所以年内不报）；
 *                    R2：ulRb ∈ [0, 8] → 8 > 7 → L2（P1 之后）
 */
#include "../common.h"

#define SLOT_NUM 4
#define RB_NUM   8

static UINT8 g_aucSched[SLOT_NUM][RB_NUM];

static void Sched_Mark(UINT32 ulSlot, UINT32 ulRb) { g_aucSched[ulSlot][ulRb] = 1; }     /* ★1 */

static void Sched_MarkRbs(UINT32 ulSlot, UINT32 ulRbNum)
{
    UINT32 r;
    for (r = 0; r <= ulRbNum; r++) {
        Sched_Mark(ulSlot, r);
    }
}

static void Sched_MarkRbs_OK(UINT32 ulSlot, UINT32 ulRbNum)
{
    UINT32 r;
    for (r = 0; r < ulRbNum; r++) {
        Sched_Mark(ulSlot, r);
    }
}

static void Sched_Alloc(UINT32 ulSlot)    { Sched_MarkRbs(ulSlot, RB_NUM); }
static void Sched_Alloc_OK(UINT32 ulSlot) { Sched_MarkRbs_OK(ulSlot, RB_NUM); }

void Sched_OnSlot_TP(void) { Sched_Alloc(1); }
void Sched_OnSlot_TN(void) { Sched_Alloc_OK(1); }
