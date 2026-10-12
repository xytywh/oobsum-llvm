/*
 * @id      LV2-04
 * @title   按 n 个元素申请，结束标记写在第 n 个（申请与访问同源，不需要知道 n）
 * @scene   S2.5  A1·M5·P1·V1·R2·X1·K0·G0
 * @year    承诺
 * @story   列表约定"最后一项后面放一个结束标记"，申请的时候忘了多申请一个。
 * @expect  TP  List_Create_TP  pa=L1
 * @expect  TN  List_Create_TN  pa=-
 *
 * 摘要
 *   Item_MarkEnd(pstItem, ulNum)
 *     req R1  W4 @ pstItem + 8·ulNum + 4      需 8·ulNum + 8 ≤ ext(pstItem)                       ★1
 *   List_InitItems / List_Init     原样上传
 * 串联
 *   List_Create_TP：pstItem = new(8·ulNum)（VOS_MemAlloc 的字节数）→ 需 8·ulNum + 8 ≤ 8·ulNum
 *                   两边相减恒为 −8 → 对任何 ulNum 都不成立 → L1（ulNum 是入口形参也不影响）
 *   List_Create_TN：new(8·(ulNum + 1)) → 差为 0 → 成立
 *   要点：变量长度的堆不需要知道具体值，符号相减就能判定。乘法溢出（8·ulNum 回绕）另记，不在这条里。
 */
#include "../common.h"

typedef struct {
    UINT32 ulId;
    UINT32 ulFlag;
} ITEM_S;

#define ITEM_FLAG_END 0xFFFFFFFFu

static void Item_MarkEnd(ITEM_S *pstItem, UINT32 ulNum)
{
    pstItem[ulNum].ulFlag = ITEM_FLAG_END;                   /* ★1 */
}

static void List_InitItems(ITEM_S *pstItem, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        pstItem[i].ulId = i;
        pstItem[i].ulFlag = 0;
    }
    Item_MarkEnd(pstItem, ulNum);
}

static void List_Init(ITEM_S *pstItem, UINT32 ulNum) { List_InitItems(pstItem, ulNum); }

ITEM_S *List_Create_TP(UINT32 ulNum)
{
    ITEM_S *pstItem = (ITEM_S *)VOS_MemAlloc(PID_CELL, 0, ulNum * (UINT32)sizeof(ITEM_S));
    if (pstItem == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    List_Init(pstItem, ulNum);
    return pstItem;
}

ITEM_S *List_Create_TN(UINT32 ulNum)
{
    ITEM_S *pstItem = (ITEM_S *)VOS_MemAlloc(PID_CELL, 0, (ulNum + 1) * (UINT32)sizeof(ITEM_S));
    if (pstItem == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    List_Init(pstItem, ulNum);
    return pstItem;
}
