/*
 * @id      LV2-07
 * @title   哈希桶下标写成 key & BUCKET_NUM（应为 BUCKET_NUM − 1）
 * @scene   S2.7  A1·M1·P1·V5→X3·R2·X3·K0·G0
 * @year    承诺
 * @story   经典笔误。key 来自消息（入口形参，没有约束），但掩码本身给出了有限上界。
 * @expect  TP  Tbl_OnAddReq_TP  pa=L2
 * @expect  TN  Tbl_OnAddReq_TN  pa=-
 *
 * 摘要
 *   Hash_Insert(pulBucket, ulKey, ulVal)
 *     ulKey & 64 的区间是 [0, 64]（按位与只能取到掩码以内）
 *     req R1  W4 @ pulBucket + 4·[0, 64]        需 260 ≤ ext(pulBucket)                          ★1
 *   Hash_Insert_OK：ulKey & 63 → [0, 63] → 需 256 ≤ ext(pulBucket)
 *   Tbl_Add / Tbl_AddEntry      原样上传（条件里已经没有 ulKey 了——掩码把它"吃掉"了）
 * 串联
 *   Tbl_OnAddReq_TP：aulBucket（256 字节）→ 260 ≤ 256 不成立，最小访问安全 → L2
 *   Tbl_OnAddReq_TN：256 ≤ 256 成立
 *   要点：入口值没有约束，但掩码之后有了代码给出的上界，所以报 L2 而不是停在 S2。
 */
#include "../common.h"

#define BUCKET_NUM 64

static void Hash_Insert(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal)
{
    pulBucket[ulKey & BUCKET_NUM] = ulVal;                   /* ★1 */
}

static void Hash_Insert_OK(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal)
{
    pulBucket[ulKey & (BUCKET_NUM - 1)] = ulVal;
}

static void Tbl_AddEntry(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal)    { Hash_Insert(pulBucket, ulKey, ulVal); }
static void Tbl_AddEntry_OK(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal) { Hash_Insert_OK(pulBucket, ulKey, ulVal); }
static void Tbl_Add(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal)         { Tbl_AddEntry(pulBucket, ulKey, ulVal); }
static void Tbl_Add_OK(UINT32 *pulBucket, UINT32 ulKey, UINT32 ulVal)      { Tbl_AddEntry_OK(pulBucket, ulKey, ulVal); }

void Tbl_OnAddReq_TP(UINT32 ulKey, UINT32 ulVal)
{
    UINT32 aulBucket[BUCKET_NUM] = { 0 };
    Tbl_Add(aulBucket, ulKey, ulVal);
}

void Tbl_OnAddReq_TN(UINT32 ulKey, UINT32 ulVal)
{
    UINT32 aulBucket[BUCKET_NUM] = { 0 };
    Tbl_Add_OK(aulBucket, ulKey, ulVal);
}
