/*
 * @id      LV3-03
 * @title   访问函数自己校验了，但校验的界比多层之上传进来的数组大（TN 是常量调用点，不能误报）
 * @scene   S3.2  A1·M2·P1·V5→G1·R2·X1·K1·G1
 * @year    承诺
 * @story   端口配置函数按 V2 规格（32 口）自检，单板上的端口表还是 16 口。
 *          同一个函数还被初始化流程用常量 3 调用——那个调用点是安全的，不能报。
 * @expect  TP  Board_OnCfgMsg_TP     pa=L2
 * @expect  TN  Board_InitDefault_TN  pa=-
 *
 * 摘要
 *   Port_SetMode(astPort, ulPortId, ulMode)
 *     PORT_S 24 字节；先判 ulPortId >= 32 就返回 → 条件带守卫：
 *     req R1  ulPortId < 32 ⇒ W4 @ astPort + 24·ulPortId     需 24·ulPortId + 4 ≤ ext(astPort)   ★1
 *     （只留数值区间 [0,31] 的写法会变成"需 748 ≤ ext(astPort)"，常量 3 的调用点也被判越界——这就是要带守卫的原因）
 *   Port_ApplyCfg / Board_CfgPort    原样上传
 * 串联
 *   Board_OnCfgMsg_TP：astPort = g_astBoardPort（384 字节），ulPortId 入口无约束
 *                      → 守卫给出 [0, 31] → 24·31 + 4 = 748 > 384，最小访问安全 → L2
 *   Board_InitDefault_TN：ulPortId = 3 → 守卫为真，24·3 + 4 = 76 ≤ 384 → 安全
 */
#include "../common.h"

#define MAX_PORT_NUM     16
#define MAX_PORT_NUM_V2  32

typedef struct {
    UINT32 ulMode;
    UINT32 ulSpeed;
    UINT8  aucMac[16];
} PORT_S;

static PORT_S g_astBoardPort[MAX_PORT_NUM];

static void Port_SetMode(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode)
{
    if (ulPortId >= MAX_PORT_NUM_V2) {
        return;
    }
    astPort[ulPortId].ulMode = ulMode;                        /* ★1 */
}

static void Port_ApplyCfg(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode) { Port_SetMode(astPort, ulPortId, ulMode); }
static void Board_CfgPort(PORT_S *astPort, UINT32 ulPortId, UINT32 ulMode) { Port_ApplyCfg(astPort, ulPortId, ulMode); }

void Board_OnCfgMsg_TP(UINT32 ulPortId, UINT32 ulMode) { Board_CfgPort(g_astBoardPort, ulPortId, ulMode); }
void Board_InitDefault_TN(void)                        { Board_CfgPort(g_astBoardPort, 3, 0); }
