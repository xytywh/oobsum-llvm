/*
 * @id      LV3-08
 * @title   调用链在循环里：上层遍历小区用了 <=，三层之下按小区号清上下文
 * @scene   S3.9  A3·M2·P0·V1·R1·X1·K2·G0
 * @year    承诺
 * @story   全小区复位流程，循环条件多了一个等号。
 * @expect  TP  Cell_ResetAll_TP  pa=L2
 * @expect  TN  Cell_ResetAll_TN  pa=-
 *
 * 摘要
 *   Ctx_Clear(ulCellId)
 *     memset_s(&g_astCellCtx[ulCellId], 32, 0, 32)
 *     req R1  W32 @ g_astCellCtx + 32·ulCellId    需 32·ulCellId + 32 ≤ 512（ulCellId ≤ 15）      ★1
 *   Cell_FreeRes / Cell_Release                原样上传
 * 串联
 *   Cell_ResetAll_TP：调用点在循环里，i ∈ [0, 16] → L2
 *   Cell_ResetAll_TN：i ∈ [0, 15] → 成立
 */
#include "../common.h"

#define MAX_CELL_NUM 16

typedef struct {
    UINT32 ulState;
    UINT32 ulUeNum;
    UINT8  aucRsv[24];
} CELL_CTX_S;

static CELL_CTX_S g_astCellCtx[MAX_CELL_NUM];

static void Ctx_Clear(UINT32 ulCellId)
{
    (void)memset_s(&g_astCellCtx[ulCellId], sizeof(CELL_CTX_S), 0, sizeof(CELL_CTX_S));   /* ★1 */
}

static void Cell_FreeRes(UINT32 ulCellId) { Ctx_Clear(ulCellId); }
static void Cell_Release(UINT32 ulCellId) { Cell_FreeRes(ulCellId); }

void Cell_ResetAll_TP(void)
{
    UINT32 i;
    for (i = 0; i <= MAX_CELL_NUM; i++) {
        Cell_Release(i);
    }
}

void Cell_ResetAll_TN(void)
{
    UINT32 i;
    for (i = 0; i < MAX_CELL_NUM; i++) {
        Cell_Release(i);
    }
}
