/*
 * @id      LV9-06
 * @title   跨任务消息：发送方把"端口个数"填进了"端口号"字段，接收任务三层之后按端口号索引
 * @scene   S9.6（v3.11 新增）  A1·M2·P5·V1·R1·X0·K0·G0
 * @year    AI + 程序分析
 * @story   OM 任务给端口任务发配置消息（VOS_SendMsg），接收方的任务入口由平台按 PID 调起。
 *          调用图里两边没有边：一边是"发消息"，另一边是"任务入口"。
 * @expect  TP  Om_OnPortCfg_TP  pa=记账  ai+pa=L1
 * @expect  TN  Om_OnPortCfg_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Port_SetMode(ulPortId)        req R1  W4 @ g_astPort + 24·ulPortId     需 ulPortId ≤ 15              ★1
 *   Port_Apply / Port_HandleCfg   原样上传（实参 pstMsg->ulPortId）
 *   Port_TaskEntry(pstMsg)        req R1  需 pstMsg->ulPortId ≤ 15     → 任务入口没有调用者：停在入口，记账
 *   Om_OnPortCfg_TP / _TN         写 stMsg.ulPortId，然后 VOS_SendMsg(PID_PORT, &stMsg, …)：未知函数，记账
 * 串联
 *   年内：两边各记一条账，连不起来
 *   AI + 程序分析：AI 恢复"发往 PID_PORT 的 PORT_CFG_MSG_S 由 Port_TaskEntry 处理"（看任务注册与消息结构体），
 *     注入一条"消息边"：Port_TaskEntry 的条件里 pstMsg->ulPortId 换成发送方写入的值
 *     → TP：16 ≤ 15 不成立 → L1；TN：15 → 成立
 *   这一类在真实代码里很常见：任务之间、板间、进程间的消息都会在调用图上断开。
 */
#include "../common.h"

#define PID_PORT     0x2001
#define MAX_PORT_NUM 16

typedef struct {
    UINT32 ulMsgId;
    UINT32 ulPortId;
    UINT32 ulMode;
} PORT_CFG_MSG_S;

typedef struct {
    UINT32 ulMode;
    UINT32 ulSpeed;
    UINT8  aucMac[16];
} PORT_S;

void VOS_SendMsg(UINT32 ulDstPid, const void *pMsg, UINT32 ulLen);   /* 平台接口，只有声明 */

static PORT_S g_astPort[MAX_PORT_NUM];

/* ---------------- 接收任务（平台按 PID_PORT 调起 Port_TaskEntry） ---------------- */
static void Port_SetMode(UINT32 ulPortId, UINT32 ulMode)                   { g_astPort[ulPortId].ulMode = ulMode; }   /* ★1 */
static void Port_Apply(UINT32 ulPortId, UINT32 ulMode)                     { Port_SetMode(ulPortId, ulMode); }
static void Port_HandleCfg(const PORT_CFG_MSG_S *pstMsg)                   { Port_Apply(pstMsg->ulPortId, pstMsg->ulMode); }
void        Port_TaskEntry(const PORT_CFG_MSG_S *pstMsg)                   { Port_HandleCfg(pstMsg); }

/* ---------------- 发送方（OM 任务） ---------------- */
void Om_OnPortCfg_TP(UINT32 ulPortNum, UINT32 ulMode)
{
    PORT_CFG_MSG_S stMsg;
    (void)ulPortNum;
    stMsg.ulMsgId  = 1;
    stMsg.ulPortId = MAX_PORT_NUM;          /* 把"个数"当成了"最后一个端口号" */
    stMsg.ulMode   = ulMode;
    VOS_SendMsg(PID_PORT, &stMsg, sizeof(stMsg));
}

void Om_OnPortCfg_TN(UINT32 ulPortNum, UINT32 ulMode)
{
    PORT_CFG_MSG_S stMsg;
    (void)ulPortNum;
    stMsg.ulMsgId  = 1;
    stMsg.ulPortId = MAX_PORT_NUM - 1;
    stMsg.ulMode   = ulMode;
    VOS_SendMsg(PID_PORT, &stMsg, sizeof(stMsg));
}
