/*
 * @id      LV3-01
 * @title   消息入口校验写成 > MAX（应为 >=），小区号三层之后索引全局上下文表
 * @scene   S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2
 * @year    承诺
 * @story   多层越界最常见的真实形态：校验在入口、访问在三层之外的另一个模块，
 *          两处写代码的人不是同一个，校验差了一。
 * @expect  TP  Cell_OnSetupReq_TP  pa=L2
 * @expect  TN  Cell_OnSetupReq_TN  pa=-
 *
 * 摘要
 *   Cell_SetState(ulCellId, ulState)
 *     CELL_CTX_S 32 字节，ulState 在 +0
 *     req R1  W4 @ g_astCellCtx + 32·ulCellId     需 32·ulCellId + 4 ≤ 512（即 ulCellId ≤ 15）  ★1
 *   Cell_Activate / Cell_ProcSetup               原样上传
 * 串联
 *   Cell_OnSetupReq_TP：ulCellId 来自消息（入口无约束），但校验 "> 16 就返回" 把调用点的区间收窄成 [0, 16]
 *                       → 16 不满足 ≤ 15，0..15 满足 → L2（上界来自代码里的校验，按口径要报）
 *   Cell_OnSetupReq_TN：">= 16" → [0, 15] → 成立
 *   要点：校验在调用方、访问在三层之下，靠的是"调用点处的区间"代入被调者的条件。
 */
#include "../common.h"

#define MAX_CELL_NUM 16

typedef struct {
    UINT32 ulState;
    UINT32 ulUeNum;
    UINT8  aucRsv[24];
} CELL_CTX_S;

typedef struct {
    UINT32 ulMsgId;
    UINT32 ulCellId;
} CELL_SETUP_REQ_S;

static CELL_CTX_S g_astCellCtx[MAX_CELL_NUM];

static void Cell_SetState(UINT32 ulCellId, UINT32 ulState)
{
    g_astCellCtx[ulCellId].ulState = ulState;                /* ★1 */
}

static void Cell_Activate(UINT32 ulCellId)  { Cell_SetState(ulCellId, 1); }
static void Cell_ProcSetup(UINT32 ulCellId) { Cell_Activate(ulCellId); }

UINT32 Cell_OnSetupReq_TP(const CELL_SETUP_REQ_S *pstReq)
{
    UINT32 ulCellId = pstReq->ulCellId;
    if (ulCellId > MAX_CELL_NUM) {
        return VOS_ERR;
    }
    Cell_ProcSetup(ulCellId);
    return VOS_OK;
}

UINT32 Cell_OnSetupReq_TN(const CELL_SETUP_REQ_S *pstReq)
{
    UINT32 ulCellId = pstReq->ulCellId;
    if (ulCellId >= MAX_CELL_NUM) {
        return VOS_ERR;
    }
    Cell_ProcSetup(ulCellId);
    return VOS_OK;
}
