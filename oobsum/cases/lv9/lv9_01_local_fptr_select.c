/*
 * @id      LV9-01
 * @title   同一函数里按制式选处理函数（函数指针取常量），NR 处理按"长度"位置写结尾
 * @scene   S9.1  A1·M1·P5·V1·R2·X0·K1·G0
 * @year    尽力（计划 P1）
 * @story   能力处理按制式分发，两个处理函数对"长度"的约定不同：NR 版把 ulLen 当成最后一个字节之后的位置。
 * @expect  TP  Ue_OnCap_TP  pa=L2
 * @expect  TN  Ue_OnCap_TN  pa=-
 *
 * 摘要
 *   Nr_SaveCap(pucBuf, ulLen)    req R1  W1 @ pucBuf + ulLen       需 ulLen + 1 ≤ ext(pucBuf)              ★1
 *   Lte_SaveCap(pucBuf, ulLen)   req R2  W1 @ pucBuf + ulLen − 1   需 ulLen ≤ ext(pucBuf)
 *   Cap_Dispatch(pucBuf, ulLen, ulRat)
 *     pfn 在本函数里只可能是 {Nr_SaveCap, Lte_SaveCap}（局部常量赋值）→ 两个摘要都代入，各带守卫：
 *     req R1  ulRat == NR ⇒ 需 ulLen + 1 ≤ ext(pucBuf)
 *     req R2  ulRat ≠ NR ⇒ 需 ulLen ≤ ext(pucBuf)
 *   Cap_Handle / Ue_ProcCap      原样上传
 * 串联
 *   Ue_OnCap_TP：aucCap 16、ulLen 16、ulRat 入口无约束 → R1 在 NR 分支上不成立 → L2（依赖制式）
 *   Ue_OnCap_TN：ulLen 15 → 都成立
 *   没有 S9.1 时：Cap_Dispatch 的调用是断链 → 记账"函数指针"
 */
#include "../common.h"

#define RAT_LTE 0
#define RAT_NR  1

typedef void (*CAP_PROC_FUNC)(UINT8 *pucBuf, UINT32 ulLen);

static void Nr_SaveCap(UINT8 *pucBuf, UINT32 ulLen)  { pucBuf[ulLen] = 0; }        /* ★1 */
static void Lte_SaveCap(UINT8 *pucBuf, UINT32 ulLen) { pucBuf[ulLen - 1] = 0; }

static void Cap_Dispatch(UINT8 *pucBuf, UINT32 ulLen, UINT32 ulRat)
{
    CAP_PROC_FUNC pfnProc = (ulRat == RAT_NR) ? Nr_SaveCap : Lte_SaveCap;
    pfnProc(pucBuf, ulLen);
}

static void Cap_Handle(UINT8 *pucBuf, UINT32 ulLen, UINT32 ulRat) { Cap_Dispatch(pucBuf, ulLen, ulRat); }
static void Ue_ProcCap(UINT8 *pucBuf, UINT32 ulLen, UINT32 ulRat) { Cap_Handle(pucBuf, ulLen, ulRat); }

void Ue_OnCap_TP(UINT32 ulRat)
{
    UINT8 aucCap[16];
    Ue_ProcCap(aucCap, sizeof(aucCap), ulRat);
}

void Ue_OnCap_TN(UINT32 ulRat)
{
    UINT8 aucCap[16];
    Ue_ProcCap(aucCap, sizeof(aucCap) - 1, ulRat);
}
