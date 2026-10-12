/*
 * @id      LV4-01
 * @title   三层封装的分配器按老规格申请，初始化按新规格遍历
 * @scene   S4.1  A1·M4·P2+P1·V0·R1·X1·K2·G0
 * @year    承诺
 * @story   UE 表由模块自己的分配器申请（Ue_AllocTbl → Cell_MemAlloc → Pub_MemAlloc → VOS_MemAlloc），
 *          初始化循环用了扩容后的 MAX_UE_NUM_EX。大小信息先往上走三层、再往下走三层（V 形）。
 * @expect  TP  Ue_ModuleInit_TP  pa=L2
 * @expect  TN  Ue_ModuleInit_TN  pa=-
 *
 * 摘要（eff = 对调用方的效果）
 *   Pub_MemAlloc(ulSize)       eff ret = new(ulSize) ∨ NULL         （由 MEM_ALLOC_SIZE 给 VOS_MemAlloc 的字节数下标）
 *   Cell_MemAlloc(ulSize)      eff ret = new(ulSize) ∨ NULL         （LOG_Print 不影响效果）
 *   Ue_AllocTbl()              eff ret = new(512) ∨ NULL            （32 × sizeof(UE_S) = 32 × 16）
 *   Ue_InitOne(pstTbl, ulIdx)  req R1  W4 @ pstTbl + 16·ulIdx      需 16·ulIdx + 4 ≤ ext(pstTbl)          ★1
 *   Ue_InitTbl(pstTbl, ulNum)  调用点 i ∈ [0, ulNum − 1] 代入
 *                              req R1  ulNum ≥ 1 ⇒ W4 @ pstTbl + 16·[0, ulNum−1]   需 16·ulNum − 12 ≤ ext(pstTbl)
 *   Ue_Setup(pstTbl)           实参 ulNum = 64 → 需 1012 ≤ ext(pstTbl)
 * 串联
 *   Ue_ModuleInit_TP：pstTbl = Ue_AllocTbl() 的返回 → ext = 512（NULL 分支已被判空排除）
 *                     → 1012 ≤ 512 不成立，最小访问安全 → L2
 *   Ue_ModuleInit_TN：走 Ue_Setup_OK（ulNum = 32）→ 需 500 ≤ 512 成立
 */
#include "../common.h"

#define MAX_UE_NUM     32
#define MAX_UE_NUM_EX  64

typedef struct {
    UINT32 ulState;
    UINT32 ulUeId;
    UINT32 ulTimer;
    UINT32 ulRsv;
} UE_S;

static void *Cell_MemAlloc(UINT32 ulSize)
{
    void *p = Pub_MemAlloc(ulSize);
    if (p == VOS_NULL_PTR) {
        LOG_Print(1, "cell alloc %u fail", ulSize);
    }
    return p;
}

static UE_S *Ue_AllocTbl(void)
{
    return (UE_S *)Cell_MemAlloc(MAX_UE_NUM * (UINT32)sizeof(UE_S));
}

static void Ue_InitOne(UE_S *pstTbl, UINT32 ulIdx)
{
    pstTbl[ulIdx].ulState = 0;                                /* ★1 */
}

static void Ue_InitTbl(UE_S *pstTbl, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        Ue_InitOne(pstTbl, i);
    }
}

static void Ue_Setup(UE_S *pstTbl)    { Ue_InitTbl(pstTbl, MAX_UE_NUM_EX); }
static void Ue_Setup_OK(UE_S *pstTbl) { Ue_InitTbl(pstTbl, MAX_UE_NUM); }

void Ue_ModuleInit_TP(void)
{
    UE_S *pstTbl = Ue_AllocTbl();
    if (pstTbl == VOS_NULL_PTR) {
        return;
    }
    Ue_Setup(pstTbl);
}

void Ue_ModuleInit_TN(void)
{
    UE_S *pstTbl = Ue_AllocTbl();
    if (pstTbl == VOS_NULL_PTR) {
        return;
    }
    Ue_Setup_OK(pstTbl);
}
