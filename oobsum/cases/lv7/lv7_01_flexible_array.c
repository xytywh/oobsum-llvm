/*
 * @id      LV7-01
 * @title   柔性数组：按 n 项申请，封口时写第 n 项
 * @scene   S7.1  A1·M6·P2+P1·V1·R2·X1·K0·G0
 * @year    2027H1（计划 P1）
 * @story   变长列表 LIST_S 的最后一个成员是柔性数组；申请按 n 项，"封口"写在 astItem[n]。
 * @expect  TP  List_Build_TP  pa=记账  later=L1
 * @expect  TN  List_Build_TN  pa=记账  later=-
 *
 * 摘要
 *   List_New(ulNum)           eff ret = new(4 + 8·ulNum) ∨ NULL
 *   Item_Seal(pstList, ulIdx)
 *     astItem 在 +4，元素 8 字节，ulFlag 在元素内 +4
 *     req R1  W4 @ pstList + 4 + 8·ulIdx + 4     需 8·ulIdx + 12 ≤ ext(pstList)                      ★1
 *     （年内：柔性数组成员的大小显式标 Unknown——成员类型给不出大小，不能按 0 算）
 *   List_Seal / List_Finish   原样上传
 * 串联
 *   2027：List_Build_TP：ext = 4 + 8·n，ulIdx = n → 需 8n + 12 ≤ 8n + 4 → 差 −8 恒不成立 → L1（与 LV2-04 同理）
 *   年内：标 Unknown → 不报，记账"柔性数组"
 */
#include "../common.h"

typedef struct {
    UINT32 ulId;
    UINT32 ulFlag;
} ITEM_S;

typedef struct {
    UINT32 ulNum;
    ITEM_S astItem[];
} LIST_S;

static LIST_S *List_New(UINT32 ulNum)
{
    LIST_S *pstList = (LIST_S *)Pub_MemAlloc((UINT32)sizeof(LIST_S) + ulNum * (UINT32)sizeof(ITEM_S));
    if (pstList != VOS_NULL_PTR) {
        pstList->ulNum = ulNum;
    }
    return pstList;
}

static void Item_Seal(LIST_S *pstList, UINT32 ulIdx)    { pstList->astItem[ulIdx].ulFlag = 0xFFFFFFFFu; }   /* ★1 */
static void List_Seal(LIST_S *pstList, UINT32 ulIdx)    { Item_Seal(pstList, ulIdx); }
static void List_Finish(LIST_S *pstList, UINT32 ulIdx)  { List_Seal(pstList, ulIdx); }

LIST_S *List_Build_TP(UINT32 ulNum)
{
    LIST_S *pstList = List_New(ulNum);
    if (pstList == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    List_Finish(pstList, ulNum);
    return pstList;
}

LIST_S *List_Build_TN(UINT32 ulNum)
{
    LIST_S *pstList = List_New(ulNum + 1);
    if (pstList == VOS_NULL_PTR) {
        return VOS_NULL_PTR;
    }
    List_Finish(pstList, ulNum);
    return pstList;
}
