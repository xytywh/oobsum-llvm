/*
 * @id      LV1-01
 * @title   统计计数：枚举结束标记 BUTT 被当成下标逐层传下去
 * @scene   S1.6  A1·M2·P0·V1·R1·X0·K0·G0
 * @year    承诺
 * @story   建立超时分支本该记 CELL_STAT_SETUP_FAIL，误传了 CELL_STAT_BUTT（枚举结束标记，
 *          只用来定数组长度）。下标穿过 3 层，在统计模块里直接索引全局计数数组。
 * @expect  TP  Cell_OnSetupTimeout_TP  pa=L1
 * @expect  TN  Cell_OnSetupTimeout_TN  pa=-
 *
 * 摘要（req = 调用方必须满足的安全条件；★ 是访问点）
 *   Stat_Inc(ulType)
 *     req R1  W4 @ g_aulCellStat + 4·ulType          需 4·ulType + 4 ≤ 16（即 ulType ≤ 3）   ★1
 *   Cell_UpdateStat(ulType)
 *     req R1  原样上传                                path Cell_UpdateStat → Stat_Inc
 *   Cell_RecordFail(ulCellId, ulCause)
 *     req R1  需 ulCause ≤ 3                          path Cell_RecordFail → Cell_UpdateStat → Stat_Inc
 * 串联
 *   Cell_OnSetupTimeout_TP：ulCause = CELL_STAT_BUTT = 4 代入 → 4 ≤ 3 不成立 → L1，穿过 3 层
 *   Cell_OnSetupTimeout_TN：ulCause = 1 → 成立 → 丢弃
 * 不在摘要里的：两处 LOG_Print、ulCellId —— 它们不流进任何访问的偏移、大小或基址。
 */
#include "../common.h"

typedef enum {
    CELL_STAT_SETUP_SUCC,
    CELL_STAT_SETUP_FAIL,
    CELL_STAT_REL,
    CELL_STAT_HO,
    CELL_STAT_BUTT
} CELL_STAT_E;

static UINT32 g_aulCellStat[CELL_STAT_BUTT];

static void Stat_Inc(UINT32 ulType)
{
    g_aulCellStat[ulType]++;                                  /* ★1 */
}

static void Cell_UpdateStat(UINT32 ulType)
{
    LOG_Print(3, "update stat %u", ulType);
    Stat_Inc(ulType);
}

static void Cell_RecordFail(UINT32 ulCellId, UINT32 ulCause)
{
    LOG_Print(2, "cell %u setup fail", ulCellId);
    Cell_UpdateStat(ulCause);
}

void Cell_OnSetupTimeout_TP(UINT32 ulCellId)
{
    Cell_RecordFail(ulCellId, CELL_STAT_BUTT);
}

void Cell_OnSetupTimeout_TN(UINT32 ulCellId)
{
    Cell_RecordFail(ulCellId, CELL_STAT_SETUP_FAIL);
}
