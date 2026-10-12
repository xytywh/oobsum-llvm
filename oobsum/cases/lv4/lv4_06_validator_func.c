/*
 * @id      LV4-06
 * @title   校验封装在独立函数里（返回布尔），校验函数自己差了一
 * @scene   S4.6（v3.11 新增）  A3·M2·P0·V5→G3·R1·X1·K1·G3
 * @year    尽力
 * @story   业务代码几乎都这么写：if (Cell_CheckPara(id) != VOS_OK) return; 真正的比较在两层之下。
 *          要把"返回值为真 ⇒ 参数在范围内"这条条件效应也自底向上传。
 * @expect  TP  Cell_OnDelReq_TP  pa=L2
 * @expect  TN  Cell_OnDelReq_TN  pa=-
 *
 * 摘要
 *   Cell_IsValidId(ulCellId)     eff ret ≠ 0 ⇒ ulCellId ≤ 16；ret = 0 ⇒ ulCellId ≥ 17          （条件效应）
 *   Cell_CheckPara(ulCellId)     eff ret = VOS_OK ⇒ ulCellId ≤ 16                               （沿返回值再上传一层）
 *   Ctx_Clear(ulCellId)          req R1  W32 @ g_astCellCtx + 32·ulCellId    需 ulCellId ≤ 15        ★1
 *   Cell_FreeCtx / Cell_Delete   原样上传
 * 串联
 *   Cell_OnDelReq_TP：分支 "!= VOS_OK 就返回" 之后，应用条件效应 → ulCellId ∈ [0, 16] → L2
 *   Cell_OnDelReq_TN：校验函数用 < → [0, 15] → 成立
 *   不支持条件效应时：TP 里 ulCellId 停在"入口无约束" → 不报（漏报，不是误报）。所以先列为尽力。
 */
#include "../common.h"

#define MAX_CELL_NUM 16

typedef struct {
    UINT32 ulState;
    UINT32 ulUeNum;
    UINT8  aucRsv[24];
} CELL_CTX_S;

static CELL_CTX_S g_astCellCtx[MAX_CELL_NUM];

static UINT32 Cell_IsValidId(UINT32 ulCellId)    { return (ulCellId <= MAX_CELL_NUM) ? VOS_TRUE : VOS_FALSE; }
static UINT32 Cell_IsValidId_OK(UINT32 ulCellId) { return (ulCellId < MAX_CELL_NUM) ? VOS_TRUE : VOS_FALSE; }

static UINT32 Cell_CheckPara(UINT32 ulCellId)
{
    if (Cell_IsValidId(ulCellId) == VOS_FALSE) {
        return VOS_ERR;
    }
    return VOS_OK;
}

static UINT32 Cell_CheckPara_OK(UINT32 ulCellId)
{
    if (Cell_IsValidId_OK(ulCellId) == VOS_FALSE) {
        return VOS_ERR;
    }
    return VOS_OK;
}

static void Ctx_Clear(UINT32 ulCellId)
{
    (void)memset_s(&g_astCellCtx[ulCellId], sizeof(CELL_CTX_S), 0, sizeof(CELL_CTX_S));   /* ★1 */
}

static void Cell_FreeCtx(UINT32 ulCellId) { Ctx_Clear(ulCellId); }
static void Cell_Delete(UINT32 ulCellId)  { Cell_FreeCtx(ulCellId); }

UINT32 Cell_OnDelReq_TP(UINT32 ulCellId)
{
    if (Cell_CheckPara(ulCellId) != VOS_OK) {
        return VOS_ERR;
    }
    Cell_Delete(ulCellId);
    return VOS_OK;
}

UINT32 Cell_OnDelReq_TN(UINT32 ulCellId)
{
    if (Cell_CheckPara_OK(ulCellId) != VOS_OK) {
        return VOS_ERR;
    }
    Cell_Delete(ulCellId);
    return VOS_OK;
}
