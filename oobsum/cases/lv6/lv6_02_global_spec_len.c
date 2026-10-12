/*
 * @id      LV6-02
 * @title   全局规格变量只在初始化函数里写常量（新规格 64），数组还是老规格 32
 * @scene   S6.2  A1·M2·P0·V4·R1·X0·K2·G0
 * @year    2027H1
 * @story   规格值放在全局变量里，Spec_Init 写一次；三层之下的清理循环按它遍历全局 UE 表。
 * @expect  TP  Cell_ResetUe_TP  pa=记账  later=L2
 * @expect  TN  Cell_ResetUe_TN  pa=记账  later=-
 *
 * 摘要
 *   Ue_ClearAll()
 *     g_ulMaxUeNum 以"函数入口时的全局值"作符号 G
 *     req R1  G ≥ 1 ⇒ W4 @ g_astUe + 16·[0, G−1]     需 16·G ≤ 512                               ★1
 *   Ue_Release / Cell_ReleaseUe                       原样上传（全局符号 G 一路不变）
 * 串联
 *   年内：链顶也不知道 G → 停在入口，记账"依赖全局值"
 *   2027：全局取值集合 = {0（静态初始化）, 64（Spec_Init）} → G ∈ [0, 64] → 需 1024 ≤ 512 → L2
 *         TN 版本的全局只被写成 32 → 成立
 *   说明：经全局变量传递没有"层数"可言，按跨函数处理；这里的调用链只是让访问点在 3 层之下。
 */
#include "../common.h"

#define MAX_UE_CTX_NUM 32

typedef struct {
    UINT32 ulState;
    UINT32 ulUeId;
    UINT32 ulTimer;
    UINT32 ulRsv;
} UE_S;

static UINT32 g_ulMaxUeNum;
static UINT32 g_ulMaxUeNumOk;
static UE_S   g_astUe[MAX_UE_CTX_NUM];

void Spec_Init(void)    { g_ulMaxUeNum = 64; }
void Spec_Init_OK(void) { g_ulMaxUeNumOk = MAX_UE_CTX_NUM; }

static void Ue_ClearAll(void)
{
    UINT32 i;
    for (i = 0; i < g_ulMaxUeNum; i++) {
        g_astUe[i].ulState = 0;                               /* ★1 */
    }
}

static void Ue_ClearAll_OK(void)
{
    UINT32 i;
    for (i = 0; i < g_ulMaxUeNumOk; i++) {
        g_astUe[i].ulState = 0;
    }
}

static void Ue_Release(void)        { Ue_ClearAll(); }
static void Ue_Release_OK(void)     { Ue_ClearAll_OK(); }
static void Cell_ReleaseUe(void)    { Ue_Release(); }
static void Cell_ReleaseUe_OK(void) { Ue_Release_OK(); }

void Cell_ResetUe_TP(void) { Cell_ReleaseUe(); }
void Cell_ResetUe_TN(void) { Cell_ReleaseUe_OK(); }
