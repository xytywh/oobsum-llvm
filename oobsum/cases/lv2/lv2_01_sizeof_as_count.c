/*
 * @id      LV2-01
 * @title   把 sizeof(数组) 当成"个数"传下去，底层按个数循环写 UINT16
 * @scene   S2.1+S2.2  A1·M1·P1·V1·R2·X1·K2·G0
 * @year    承诺
 * @story   ERAB 列表接口的第二个参数是"个数"，调用方顺手写成了 sizeof(ausId)（字节数 = 个数 × 2）。
 * @expect  TP  Erab_OnSetupReq_TP  pa=L2
 * @expect  TN  Erab_OnSetupReq_TN  pa=-
 *
 * 摘要
 *   Erab_FillIds(pusId, ulNum)
 *     i ∈ [0, ulNum − 1]
 *     req R1  ulNum ≥ 1 ⇒ W2 @ pusId + 2·[0, ulNum−1]   需 2·ulNum ≤ ext(pusId)                  ★1
 *   Erab_BuildList / Erab_Setup      req R1 原样上传（两侧——缓冲区和个数——一起沿同一条链下来）
 * 串联
 *   Erab_OnSetupReq_TP：pusId = ausId（16 字节），ulNum = sizeof(ausId) = 16 → 需 32 ≤ 16；
 *                       最小那次访问（i = 0）是安全的 → 部分重叠 → L2
 *   Erab_OnSetupReq_TN：ulNum = 8 → 16 ≤ 16 成立
 *   要点：条件里同时有"指针"和"个数"两个形参，代换时一起换掉；元素大小 2 由类型给出（字节归一）。
 */
#include "../common.h"

#define MAX_ERAB_NUM 8

static void Erab_FillIds(UINT16 *pusId, UINT32 ulNum)
{
    UINT32 i;
    for (i = 0; i < ulNum; i++) {
        pusId[i] = (UINT16)(i + 5);                           /* ★1 */
    }
}

static void Erab_BuildList(UINT16 *pusId, UINT32 ulNum) { Erab_FillIds(pusId, ulNum); }
static void Erab_Setup(UINT16 *pusId, UINT32 ulNum)     { Erab_BuildList(pusId, ulNum); }

void Erab_OnSetupReq_TP(void)
{
    UINT16 ausId[MAX_ERAB_NUM];
    Erab_Setup(ausId, sizeof(ausId));
}

void Erab_OnSetupReq_TN(void)
{
    UINT16 ausId[MAX_ERAB_NUM];
    Erab_Setup(ausId, MAX_ERAB_NUM);
}
