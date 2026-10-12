/*
 * @id      LV6-04
 * @title   端口数来自配置文件解析（运行期值），遍历全局端口表
 * @scene   S6.4  A1·M2·P0·V4·R1·X0·K2·G0
 * @year    AI + 程序分析
 * @story   单板端口数从配置文件读进 g_stBoardCfg.ulMaxPort，产品文档写的取值范围是 1~32，端口表按 16 定义。
 * @expect  TP  Board_InitPorts_TP  pa=记账  ai+pa=L2
 * @expect  TN  Board_InitPorts_TN  pa=-
 *
 * 摘要
 *   Cfg_Parse(pucFile, ulLen)    eff g_stBoardCfg.ulMaxPort := 文件内容（无约束）
 *   Port_ResetAll()
 *     req R1  P ≥ 1 ⇒ W4 @ g_astPort + 24·[0, P−1]   需 24·P ≤ 384        （P = ulMaxPort 的入口值）     ★1
 *   Port_ResetAll_OK()
 *     先截断 ulNum = min(P, 16) → 需 24·ulNum ≤ 384 恒成立 → 不产生条件
 *   Board_ResetPort / Board_PrepPort   原样上传
 * 串联
 *   年内：P 只来自文件内容 → 无约束 → 不报 L2（口径），记账"依赖运行期配置"
 *   AI + 程序分析：AI 从产品文档 / 配置模板读出"ulMaxPort ∈ [1, 32]"作为前置条件 → 需 768 ≤ 384 → L2
 *   TN：本地截断让条件在函数内就判定为安全，不需要任何外部事实
 */
#include "../common.h"

#define MAX_PORT_NUM 16

typedef struct {
    UINT32 ulMode;
    UINT32 ulSpeed;
    UINT8  aucMac[16];
} PORT_S;

typedef struct {
    UINT32 ulMaxPort;
    UINT32 ulClkSrc;
} BOARD_CFG_S;

static BOARD_CFG_S g_stBoardCfg;
static PORT_S      g_astPort[MAX_PORT_NUM];

void Cfg_Parse(const UINT8 *pucFile, UINT32 ulLen)
{
    if (ulLen < 2) {
        return;
    }
    g_stBoardCfg.ulMaxPort = (UINT32)pucFile[0] | ((UINT32)pucFile[1] << 8);
}

static void Port_ResetAll(void)
{
    UINT32 i;
    for (i = 0; i < g_stBoardCfg.ulMaxPort; i++) {
        g_astPort[i].ulMode = 0;                              /* ★1 */
    }
}

static void Port_ResetAll_OK(void)
{
    UINT32 i;
    UINT32 ulNum = g_stBoardCfg.ulMaxPort;
    if (ulNum > MAX_PORT_NUM) {
        ulNum = MAX_PORT_NUM;
    }
    for (i = 0; i < ulNum; i++) {
        g_astPort[i].ulMode = 0;
    }
}

static void Board_ResetPort(void)    { Port_ResetAll(); }
static void Board_ResetPort_OK(void) { Port_ResetAll_OK(); }
static void Board_PrepPort(void)     { Board_ResetPort(); }
static void Board_PrepPort_OK(void)  { Board_ResetPort_OK(); }

void Board_InitPorts_TP(void) { Board_PrepPort(); }
void Board_InitPorts_TN(void) { Board_PrepPort_OK(); }
