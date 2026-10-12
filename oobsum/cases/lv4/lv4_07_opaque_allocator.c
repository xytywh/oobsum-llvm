/*
 * @id      LV4-07
 * @title   分配器的函数体不在分析范围里，只出现在 config.ini / alloc_free_identity 结果里
 * @scene   S4.5  A1·M5·P2+P1·V0·R1·X0·K0·G0
 * @year    规则性处理
 * @story   第三方内存池 Ext_PoolAlloc 只有头文件。config.ini 里配了 MEM_ALLOC=Ext_PoolAlloc,-1，
 *          但没有配字节数下标。
 * @expect  TP  Pool_UseBlock_TP  pa=记账  later=L1
 * @expect  TN  Pool_UseBlock_TN  pa=-
 *
 * 摘要
 *   Ext_PoolAlloc               无函数体；按配置：eff ret = new(?) ∨ NULL —— 知道是新堆，不知道多大
 *   Blk_SetTail(pucBlk, ulPos)  req R1  W1 @ pucBlk + ulPos   需 ulPos + 1 ≤ ext(pucBlk)            ★1
 *   Blk_Init / Pool_PrepareBlk  原样上传
 * 串联
 *   Pool_UseBlock_TP：ulPos = 64；ext(pucBlk) = ext(new@Ext_PoolAlloc) 是符号 → 判不了 → 不报，记账"分配大小未知"
 *                     补上 MEM_ALLOC_SIZE=Ext_PoolAlloc,1 之后：ext = 64 → 65 ≤ 64 不成立 → L1
 *   Pool_UseBlock_TN：写 63 → 需 64 ≤ ext；同样判不了，但它本来就没问题
 *   要点：不知道大小时宁可不报，不能拿"未知"当 0 去比（那样每个外部分配都会误报）。
 */
#include "../common.h"

#define POOL_BLK_LEN 64

void *Ext_PoolAlloc(UINT32 ulPoolId, UINT32 ulSize);     /* 第三方库，只有声明 */

static void Blk_SetTail(UINT8 *pucBlk, UINT32 ulPos)  { pucBlk[ulPos] = 0xEE; }   /* ★1 */
static void Blk_Init(UINT8 *pucBlk, UINT32 ulPos)     { Blk_SetTail(pucBlk, ulPos); }
static void Pool_PrepareBlk(UINT8 *pucBlk, UINT32 ulPos) { Blk_Init(pucBlk, ulPos); }

void Pool_UseBlock_TP(void)
{
    UINT8 *pucBlk = (UINT8 *)Ext_PoolAlloc(1, POOL_BLK_LEN);
    if (pucBlk == VOS_NULL_PTR) {
        return;
    }
    Pool_PrepareBlk(pucBlk, POOL_BLK_LEN);
}

void Pool_UseBlock_TN(void)
{
    UINT8 *pucBlk = (UINT8 *)Ext_PoolAlloc(1, POOL_BLK_LEN);
    if (pucBlk == VOS_NULL_PTR) {
        return;
    }
    Pool_PrepareBlk(pucBlk, POOL_BLK_LEN - 1);
}
