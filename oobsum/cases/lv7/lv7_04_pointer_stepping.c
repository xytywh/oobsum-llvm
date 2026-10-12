/*
 * @id      LV7-04
 * @title   循环中指针步进拷贝：个数来自三层之上，比目的缓冲区大
 * @scene   S7.4  A2·M1·P1·V1·R2·X1·K3·G0
 * @year    2027H1
 * @story   老代码手写的字节拷贝 while (ulCnt-- > 0) *pucOut++ = *pucIn++;
 * @expect  TP  Om_DumpReg_TP  pa=记账  later=L2
 * @expect  TN  Om_DumpReg_TN  pa=记账  later=-
 *
 * 摘要
 *   Buf_CopyStep(pucOut, pucIn, ulCnt)
 *     年内：pucOut 的偏移在循环里被 widening 到 [0, +∞)，和 ulCnt 的关系丢了
 *           → 这种"widening 出来的无穷上界"不算代码给出的界 → 不产生 L2 条件，记账"指针步进"
 *     2027：记住"偏移 = 已迭代次数 ∈ [0, ulCnt − 1]"
 *           req R1  ulCnt ≥ 1 ⇒ W1 @ pucOut + [0, ulCnt−1]   需 ulCnt ≤ ext(pucOut)                     ★1
 *           req R2  同上，读 pucIn                               需 ulCnt ≤ ext(pucIn)
 *   Reg_Snapshot / Om_CollectReg     原样上传
 * 串联
 *   2027：TP：aucOut 16、aucIn 32、ulCnt = 32 → R1 部分不满足 → L2；TN：ulCnt = 16 → 成立
 *   要点：这里不能偷懒报 L2——同样的 widening 结果在 TN 里也是 [0, +∞)，会误报。
 */
#include "../common.h"

static void Buf_CopyStep(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt)
{
    while (ulCnt-- > 0) {
        *pucOut++ = *pucIn++;                                 /* ★1 */
    }
}

static void Reg_Snapshot(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt)  { Buf_CopyStep(pucOut, pucIn, ulCnt); }
static void Om_CollectReg(UINT8 *pucOut, const UINT8 *pucIn, UINT32 ulCnt) { Reg_Snapshot(pucOut, pucIn, ulCnt); }

void Om_DumpReg_TP(void)
{
    UINT8 aucIn[32] = { 0 };
    UINT8 aucOut[16];
    Om_CollectReg(aucOut, aucIn, sizeof(aucIn));
}

void Om_DumpReg_TN(void)
{
    UINT8 aucIn[32] = { 0 };
    UINT8 aucOut[16];
    Om_CollectReg(aucOut, aucIn, sizeof(aucOut));
}
