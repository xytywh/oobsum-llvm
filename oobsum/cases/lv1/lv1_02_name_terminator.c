/*
 * @id      LV1-02
 * @title   名字缓冲区逐层下传，最底层按"长度"位置写结尾符
 * @scene   S1.1  A1·M1·P1·V0·R1·X0·K0·G0
 * @year    承诺
 * @story   UE 名字的格式化分了三层，最底层统一在 pszName[UE_NAME_LEN] 写 '\0'，
 *          约定是"缓冲区要比名字长 1"。新加的调用方按 UE_NAME_LEN 定义了数组。
 * @expect  TP  Ue_ShowInfo_TP  pa=L1
 * @expect  TN  Ue_ShowInfo_TN  pa=-
 *
 * 摘要
 *   Name_Terminate(pszName)
 *     req R1  W1 @ pszName + 16                       需 17 ≤ ext(pszName)                    ★1
 *   Name_Format(pszName, ulUeId)
 *     本函数还写了 pszName[0]、pszName[1]（需 1 ≤ ext、2 ≤ ext），被 R1 覆盖，合并后只留最强的一条
 *     req R1  需 17 ≤ ext(pszName)                    path Name_Format → Name_Terminate
 *   Ue_BuildName(pszName, ulUeId)
 *     req R1  需 17 ≤ ext(pszName)                    path Ue_BuildName → Name_Format → Name_Terminate
 * 串联
 *   Ue_ShowInfo_TP：pszName = szName，ext = 16 → 17 ≤ 16 不成立 → L1，3 层
 *   Ue_ShowInfo_TN：ext = 17 → 成立
 */
#include "../common.h"

#define UE_NAME_LEN 16

static void Name_Terminate(CHAR *pszName)
{
    pszName[UE_NAME_LEN] = '\0';                              /* ★1 */
}

static void Name_Format(CHAR *pszName, UINT32 ulUeId)
{
    pszName[0] = 'U';
    pszName[1] = (CHAR)('0' + (ulUeId % 10));
    Name_Terminate(pszName);
}

static void Ue_BuildName(CHAR *pszName, UINT32 ulUeId)
{
    Name_Format(pszName, ulUeId);
}

void Ue_ShowInfo_TP(UINT32 ulUeId)
{
    CHAR szName[UE_NAME_LEN];
    Ue_BuildName(szName, ulUeId);
    LOG_Print(3, "ue %s", szName);
}

void Ue_ShowInfo_TN(UINT32 ulUeId)
{
    CHAR szName[UE_NAME_LEN + 1];
    Ue_BuildName(szName, ulUeId);
    LOG_Print(3, "ue %s", szName);
}
