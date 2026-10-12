/*
 * @id      LV6-03
 * @title   运行期申请的全局表：大小由 g_ulTblNum 决定，访问前按 > g_ulTblNum 校验（应为 >=）
 * @scene   S6.3  A1·M5·P4·V4·R1·X1·K1·G4
 * @year    2027（程序分析验证 + AI 提不变式）
 * @story   全局资源在模块初始化时按配置申请；各处访问只认 g_ulTblNum。
 * @expect  TP  Svc_OnSetReq_TP  pa=记账  ai+pa=L2
 * @expect  TN  Svc_OnSetReq_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Tbl_Init(ulNum)
 *     eff g_pulTbl := new(4·ulNum) ∨ NULL；ret == VOS_OK ⇒ g_ulTblNum := ulNum
 *   Tbl_Set(ulIdx, ulVal)
 *     req R1  ulIdx ≤ N ⇒ W4 @ T + 4·ulIdx     需 4·ulIdx + 4 ≤ ext(T)      （T、N 是两个全局的入口值）    ★1
 *   Tbl_Update / Svc_SetValue                 原样上传
 * 串联
 *   年内：ext(T) 和 N 没有关系可用 → 停在入口，记账"依赖全局不变式"
 *   AI + 程序分析：AI 从 Tbl_Init 与各处校验提出不变式 ext(g_pulTbl) = 4·g_ulTblNum；
 *     程序分析先验证所有写 g_pulTbl / g_ulTblNum 的地方都维持它（只有 Tbl_Init，成立），再代入：
 *     TP：ulIdx ≤ N ⇒ 需 4·ulIdx + 4 ≤ 4·N → ulIdx = N 时不成立 → L2
 *     TN：ulIdx < N ⇒ 成立
 *   为什么交给 AI 提：从代码里自动归纳"哪个全局量是哪块全局内存的长度"不可靠；验证它却很便宜。
 */
#include "../common.h"

static UINT32 *g_pulTbl;
static UINT32  g_ulTblNum;

UINT32 Tbl_Init(UINT32 ulNum)
{
    g_pulTbl = (UINT32 *)Pub_MemAlloc(ulNum * (UINT32)sizeof(UINT32));
    if (g_pulTbl == VOS_NULL_PTR) {
        return VOS_ERR;
    }
    g_ulTblNum = ulNum;
    return VOS_OK;
}

static void Tbl_Set(UINT32 ulIdx, UINT32 ulVal)
{
    if (ulIdx > g_ulTblNum) {
        return;
    }
    g_pulTbl[ulIdx] = ulVal;                                  /* ★1 */
}

static void Tbl_Set_OK(UINT32 ulIdx, UINT32 ulVal)
{
    if (ulIdx >= g_ulTblNum) {
        return;
    }
    g_pulTbl[ulIdx] = ulVal;
}

static void Tbl_Update(UINT32 ulIdx, UINT32 ulVal)       { Tbl_Set(ulIdx, ulVal); }
static void Tbl_Update_OK(UINT32 ulIdx, UINT32 ulVal)    { Tbl_Set_OK(ulIdx, ulVal); }
static void Svc_SetValue(UINT32 ulIdx, UINT32 ulVal)     { Tbl_Update(ulIdx, ulVal); }
static void Svc_SetValue_OK(UINT32 ulIdx, UINT32 ulVal)  { Tbl_Update_OK(ulIdx, ulVal); }

void Svc_OnSetReq_TP(UINT32 ulIdx, UINT32 ulVal) { Svc_SetValue(ulIdx, ulVal); }
void Svc_OnSetReq_TN(UINT32 ulIdx, UINT32 ulVal) { Svc_SetValue_OK(ulIdx, ulVal); }
