/*
 * @id      LV10-05
 * @title   "个数"字段与数组容量的对应关系：解码时直接从消息里取个数，没跟数组容量比
 * @scene   S10.1 变体（个数 ↔ 数组）  A1·M3·P3·V3·R2·X1·K2·G4
 * @year    AI + 程序分析
 * @story   NBR_LIST_S { ulNum; astNbr[16]; }——"ulNum 是 astNbr 的有效个数"只是约定。
 *          解码函数写 ulNum 时没截断；使用处三层之下按 ulNum 遍历。
 * @expect  TP  Cell_OnNbrMsg_TP  pa=记账  ai+pa=L2
 * @expect  TN  Cell_OnNbrMsg_TN  pa=-
 *
 * 摘要
 *   NbrList_Decode(pstList, pucMsg)   eff pstList->ulNum := pucMsg[0]（消息内容，无约束）
 *   NbrList_Decode_OK                 eff pstList->ulNum := min(pucMsg[0], 16)   → ∈ [0, 16]
 *   Nbr_Use(pstList, ulNum)           调用点 i ∈ [0, ulNum − 1]
 *     req R1  ulNum ≥ 1 ⇒ R2 @ pstList + 4 + 4·[0, ulNum−1]    需 4·ulNum ≤ 64（astNbr 64 字节）        ★1
 *   Nbr_Apply / Cell_ApplyNbr         实参 pstList->ulNum
 * 串联
 *   Cell_OnNbrMsg_TN：解码效应给出 ulNum ∈ [0, 16] → 64 ≤ 64 成立 → 不报（程序分析自己就能证明）
 *   Cell_OnNbrMsg_TP：ulNum 来自消息内容 → 无约束 → 记账
 *   AI + 程序分析：AI 提出字段不变式 ulNum ≤ ARRAY_SIZE(astNbr)；程序分析检查所有写 ulNum 的地方：
 *     NbrList_Decode 的写入不维持它 → 在写入点报 L2（比在使用点报更有用：修复点就在这里）
 */
#include "../common.h"

#define MAX_NBR_NUM 16

typedef struct {
    UINT16 usPci;
    UINT16 usEarfcn;
} NBR_S;

typedef struct {
    UINT32 ulNum;
    NBR_S  astNbr[MAX_NBR_NUM];
} NBR_LIST_S;

static void NbrList_Decode(NBR_LIST_S *pstList, const UINT8 *pucMsg)
{
    pstList->ulNum = pucMsg[0];
}

static void NbrList_Decode_OK(NBR_LIST_S *pstList, const UINT8 *pucMsg)
{
    UINT32 ulNum = pucMsg[0];
    pstList->ulNum = (ulNum > MAX_NBR_NUM) ? MAX_NBR_NUM : ulNum;
}

static UINT32 Nbr_Use(const NBR_LIST_S *pstList, UINT32 ulNum)
{
    UINT32 i, ulSum = 0;
    for (i = 0; i < ulNum; i++) {
        ulSum += pstList->astNbr[i].usPci;                   /* ★1 */
    }
    return ulSum;
}

static UINT32 Nbr_Apply(const NBR_LIST_S *pstList)     { return Nbr_Use(pstList, pstList->ulNum); }
static UINT32 Cell_ApplyNbr(const NBR_LIST_S *pstList) { return Nbr_Apply(pstList); }

UINT32 Cell_OnNbrMsg_TP(const UINT8 *pucMsg)
{
    NBR_LIST_S stList;
    NbrList_Decode(&stList, pucMsg);
    return Cell_ApplyNbr(&stList);
}

UINT32 Cell_OnNbrMsg_TN(const UINT8 *pucMsg)
{
    NBR_LIST_S stList;
    NbrList_Decode_OK(&stList, pucMsg);
    return Cell_ApplyNbr(&stList);
}
