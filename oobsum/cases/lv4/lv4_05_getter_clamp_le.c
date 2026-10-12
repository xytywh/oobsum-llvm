/*
 * @id      LV4-05
 * @title   个数来自"取配置并截断"的 getter（两层），使用处循环多了一个等号
 * @scene   S4.4  A1·M1·P1·V2·R3·X0·K2·G0
 * @year    承诺
 * @story   DRB 个数从配置读，取值函数把它截到 MAX_DRB_NUM；复位循环写成了 i <= ulNum。
 * @expect  TP  Ue_OnReconfig_TP  pa=L2
 * @expect  TN  Ue_OnReconfig_TN  pa=-
 *
 * 摘要
 *   Cfg_ClampDrbNum(ulCfg)     eff ret ∈ [0, 8]（两个分支：8 或 ulCfg 且 ulCfg ≤ 8）
 *   Drb_GetMaxNum(ulCfg)       eff ret ∈ [0, 8]                       ← 返回值范围自底向上
 *   Drb_ResetOne(astDrb, i)    req R1  W4 @ astDrb + 8·i       需 8·i + 4 ≤ ext(astDrb)            ★1
 *   Drb_ResetAll(astDrb, ulNum)
 *     调用点 i ∈ [0, ulNum]
 *     req R1  W4 @ astDrb + 8·[0, ulNum]      需 8·ulNum + 4 ≤ ext(astDrb)
 *   Ue_ResetDrb               原样上传
 * 串联
 *   Ue_OnReconfig_TP：ulNum = Drb_GetMaxNum(...) → ∈ [0, 8]（ensures 代入）；astDrb 64 字节
 *                     → 8·8 + 4 = 68 > 64，ulNum = 0 时安全 → L2
 *   Ue_OnReconfig_TN：Drb_ResetAll_OK（i < ulNum）→ 需 8·ulNum − 4 ≤ 64 → 成立
 *   要点：长度从"兄弟链"的返回值来（异链汇合最简单的形态），靠的是返回值的取值范围。
 */
#include "../common.h"

#define MAX_DRB_NUM 8

typedef struct {
    UINT32 ulState;
    UINT32 ulQci;
} DRB_S;

static UINT32 Cfg_ClampDrbNum(UINT32 ulCfg)
{
    return (ulCfg > MAX_DRB_NUM) ? MAX_DRB_NUM : ulCfg;
}

static UINT32 Drb_GetMaxNum(UINT32 ulCfg) { return Cfg_ClampDrbNum(ulCfg); }

static void Drb_ResetOne(DRB_S *astDrb, UINT32 i)
{
    astDrb[i].ulState = 0;                                    /* ★1 */
}

static void Drb_ResetAll(DRB_S *astDrb, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i <= ulNum; i++) {
        Drb_ResetOne(astDrb, i);
    }
}

static void Drb_ResetAll_OK(DRB_S *astDrb, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        Drb_ResetOne(astDrb, i);
    }
}

static void Ue_ResetDrb(DRB_S *astDrb, UINT32 ulNum)    { Drb_ResetAll(astDrb, ulNum); }
static void Ue_ResetDrb_OK(DRB_S *astDrb, UINT32 ulNum) { Drb_ResetAll_OK(astDrb, ulNum); }

void Ue_OnReconfig_TP(UINT32 ulCfgDrbNum)
{
    DRB_S  astDrb[MAX_DRB_NUM];
    UINT32 ulNum = Drb_GetMaxNum(ulCfgDrbNum);
    Ue_ResetDrb(astDrb, ulNum);
}

void Ue_OnReconfig_TN(UINT32 ulCfgDrbNum)
{
    DRB_S  astDrb[MAX_DRB_NUM];
    UINT32 ulNum = Drb_GetMaxNum(ulCfgDrbNum);
    Ue_ResetDrb_OK(astDrb, ulNum);
}
