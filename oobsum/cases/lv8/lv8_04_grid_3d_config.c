/*
 * @id      LV8-04
 * @title   三维资源格（符号 × RB × 端口）按配置值线性化，嵌套循环多走了一个端口
 * @scene   S8.3  A1·M5·P2+P1·V1·R2·X5·K4·G0
 * @year    不承诺
 * @story   物理层常见写法：缓冲区按 ulSymNum × ulRbNum × ulPortNum 申请，下标 (s·RB + r)·PORT + p。
 * @expect  TP  Phy_BuildGrid_TP  pa=记账
 * @expect  TN  Phy_BuildGrid_TN  pa=记账
 *
 * 摘要
 *   Grid_Set(pucGrid, s, r, p, ulRbNum, ulPortNum)
 *     偏移 (s·ulRbNum + r)·ulPortNum + p：多个变量相乘，保留成不透明项
 *     req R1  W1 @ pucGrid + ⟨(s·ulRbNum + r)·ulPortNum⟩ + p     需 … ≤ ext(pucGrid)                    ★1
 * 串联
 *   ext(pucGrid) = ⟨ulSymNum·ulRbNum·ulPortNum⟩；要比较两个乘积式需要非线性推理 → TP、TN 都只记账
 *   这是程序分析覆盖上限的第 ② 条（取值要能用线性表达式和区间表达）落空的典型
 */
#include "../common.h"

static void Grid_Set(UINT8 *pucGrid, UINT32 s, UINT32 r, UINT32 p, UINT32 ulRbNum, UINT32 ulPortNum)
{
    pucGrid[(s * ulRbNum + r) * ulPortNum + p] = 0;           /* ★1 */
}

static void Grid_Fill(UINT8 *pucGrid, UINT32 ulSym, UINT32 ulRb, UINT32 ulPort, UINT32 bOk)
{
    UINT32 s, r, p;
    UINT32 ulPortEnd = bOk ? ulPort : ulPort + 1;
    for (s = 0; s < ulSym; s++) {
        for (r = 0; r < ulRb; r++) {
            for (p = 0; p < ulPortEnd; p++) {
                Grid_Set(pucGrid, s, r, p, ulRb, ulPort);
            }
        }
    }
}

static void Phy_InitGrid(UINT8 *g, UINT32 a, UINT32 b, UINT32 c, UINT32 k) { Grid_Fill(g, a, b, c, k); }

void Phy_BuildGrid_TP(UINT32 ulSym, UINT32 ulRb, UINT32 ulPort)
{
    UINT8 *pucGrid = (UINT8 *)Pub_MemAlloc(ulSym * ulRb * ulPort);
    if (pucGrid == VOS_NULL_PTR) {
        return;
    }
    Phy_InitGrid(pucGrid, ulSym, ulRb, ulPort, VOS_FALSE);
}

void Phy_BuildGrid_TN(UINT32 ulSym, UINT32 ulRb, UINT32 ulPort)
{
    UINT8 *pucGrid = (UINT8 *)Pub_MemAlloc(ulSym * ulRb * ulPort);
    if (pucGrid == VOS_NULL_PTR) {
        return;
    }
    Phy_InitGrid(pucGrid, ulSym, ulRb, ulPort, VOS_TRUE);
}
