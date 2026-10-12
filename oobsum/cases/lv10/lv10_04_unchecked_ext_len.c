/*
 * @id      LV10-04
 * @title   外部消息里的长度一路下传到 memcpy_s，没有任何校验（TN：框架在注册时声明了最大长度）
 * @scene   S10.4  A3·M2·P0·V5·R1·X0·K0·G0
 * @year    AI + 程序分析（候选研判）
 * @story   OM 消息入口把 usMsgLen 原样交给配置保存；TN 版本的消息在注册时声明了最大长度 256，
 *          由平台框架在投递前统一校验——这件事在分析范围之外。
 * @expect  TP  Om_OnSaveCfg_TP  pa=记账  ai+pa=L2
 * @expect  TN  Om_OnSaveCfg_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Cfg_Save(pucBody, ulLen)    req R1  W ulLen @ g_aucCfg + 0   需 ulLen ≤ 256                         ★1
 *                               req R2  R ulLen @ pucBody + 0    需 ulLen ≤ ext(pucBody)
 *   Cfg_Store / Om_SaveCfg      原样上传
 *   入口：ulLen = pstMsg->usMsgLen，只有类型范围 [0, 65535]，不算代码给出的界 → 停在 S2，不报 L2
 * 串联
 *   年内：两条入口都记一条"外部输入未证明安全"的候选（带逐层 trace）
 *   AI 研判：TP 找不到任何校验 → 升级 L2；TN 读到 OM_RegMsg(…, 256) 的注册与框架的投递校验 → 抑制
 */
#include "../common.h"

#define CFG_LEN 256

typedef struct {
    UINT16 usMsgId;
    UINT16 usMsgLen;
    UINT8  aucBody[1024];
} OM_MSG_S;

void OM_RegMsg(UINT32 ulMsgId, void (*pfnEntry)(const OM_MSG_S *), UINT32 ulMaxLen);   /* 平台：投递前按 ulMaxLen 校验 */

static UINT8 g_aucCfg[CFG_LEN];

static void Cfg_Save(const UINT8 *pucBody, UINT32 ulLen)
{
    (void)memcpy_s(g_aucCfg, ulLen, pucBody, ulLen);         /* ★1 */
}

static void Cfg_Store(const UINT8 *p, UINT32 n)  { Cfg_Save(p, n); }
static void Om_SaveCfg(const UINT8 *p, UINT32 n) { Cfg_Store(p, n); }

void Om_OnSaveCfg_TP(const OM_MSG_S *pstMsg) { Om_SaveCfg(pstMsg->aucBody, pstMsg->usMsgLen); }
void Om_OnSaveCfg_TN(const OM_MSG_S *pstMsg) { Om_SaveCfg(pstMsg->aucBody, pstMsg->usMsgLen); }

void Om_ModInit(void) { OM_RegMsg(0x3001, Om_OnSaveCfg_TN, CFG_LEN); }
