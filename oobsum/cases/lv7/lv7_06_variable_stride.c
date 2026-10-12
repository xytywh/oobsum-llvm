/*
 * @id      LV7-06
 * @title   二维表按"行数 × 列数"申请，访问用变量步长线性化（两个变量相乘）
 * @scene   S7.6  A1·M5·P2+P1·V1·R2·X5·K2·G0
 * @year    不承诺
 * @story   行列数都来自小区配置；申请 ulRow × ulCol 字节，填表时列循环多了一个等号。
 * @expect  TP  Cell_BuildMap_TP  pa=记账
 * @expect  TN  Cell_BuildMap_TN  pa=记账
 *
 * 摘要
 *   Map_Set(pucMap, r, c, ulCol)
 *     偏移 r·ulCol + c：r·ulCol 是两个符号相乘，按"宁可表示得出"原则保留成不透明项 ⟨r·ulCol⟩
 *     req R1  W1 @ pucMap + ⟨r·ulCol⟩ + c      需 ⟨r·ulCol⟩ + c + 1 ≤ ext(pucMap)                       ★1
 *   Map_Fill(pucMap, ulRow, ulCol)   调用点 r ∈ [0, ulRow−1]、c ∈ [0, ulCol]（TP）
 *   Map_Init                          原样上传
 * 串联
 *   Cell_BuildMap_TP：ext(pucMap) = ⟨ulRow·ulCol⟩，访问末端 ⟨(ulRow−1)·ulCol⟩ + ulCol + 1
 *     要证安全或证越界都得做非线性推理（两个乘积项之间的关系），线性 + 不透明项做不到 → 不报，记账"非线性"
 *   Cell_BuildMap_TN：同样判不了 → 记账（不误报）
 *   对照：如果 ulCol 是常量（LV3-09），乘积变线性，能判。
 */
#include "../common.h"

static void Map_Set(UINT8 *pucMap, UINT32 r, UINT32 c, UINT32 ulCol) { pucMap[r * ulCol + c] = 1; }   /* ★1 */

static void Map_Fill(UINT8 *pucMap, UINT32 ulRow, UINT32 ulCol)
{
    UINT32 r, c;
    for (r = 0; r < ulRow; r++) {
        for (c = 0; c <= ulCol; c++) {
            Map_Set(pucMap, r, c, ulCol);
        }
    }
}

static void Map_Fill_OK(UINT8 *pucMap, UINT32 ulRow, UINT32 ulCol)
{
    UINT32 r, c;
    for (r = 0; r < ulRow; r++) {
        for (c = 0; c < ulCol; c++) {
            Map_Set(pucMap, r, c, ulCol);
        }
    }
}

static void Map_Init(UINT8 *pucMap, UINT32 ulRow, UINT32 ulCol)    { Map_Fill(pucMap, ulRow, ulCol); }
static void Map_Init_OK(UINT8 *pucMap, UINT32 ulRow, UINT32 ulCol) { Map_Fill_OK(pucMap, ulRow, ulCol); }

void Cell_BuildMap_TP(UINT32 ulRow, UINT32 ulCol)
{
    UINT8 *pucMap = (UINT8 *)Pub_MemAlloc(ulRow * ulCol);
    if (pucMap == VOS_NULL_PTR) {
        return;
    }
    Map_Init(pucMap, ulRow, ulCol);
}

void Cell_BuildMap_TN(UINT32 ulRow, UINT32 ulCol)
{
    UINT8 *pucMap = (UINT8 *)Pub_MemAlloc(ulRow * ulCol);
    if (pucMap == VOS_NULL_PTR) {
        return;
    }
    Map_Init_OK(pucMap, ulRow, ulCol);
}
