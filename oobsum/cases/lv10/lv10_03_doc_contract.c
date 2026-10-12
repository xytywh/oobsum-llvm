/*
 * @id      LV10-03
 * @title   对外接口注释承诺"端口号 0~31"，实现里的表只有 16 项且不校验
 * @scene   S10.3  A1·M2·P0·V1·R1·X0·K0·G4
 * @year    AI + 程序分析
 * @story   接口是对外导出的，分析范围里没有调用者。注释（接口文档）就是唯一的调用约定。
 * @expect  TP  Port_SetSpeed_TP  pa=记账  ai+pa=L2
 * @expect  TN  Port_SetSpeed_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Port_WriteSpeed(ulPortId, ulSpeed)   req R1  W4 @ g_astPort + 24·ulPortId + 4    需 ulPortId ≤ 15      ★1
 *   Port_Apply / Port_Cfg                原样上传
 *   Port_SetSpeed_TP / _TN（对外接口）    条件停在入口（没有调用者）→ 记账"入口未证明"
 * 串联
 *   AI + 程序分析：AI 从注释抽出前置条件：TP 的注释是 ulPortId ∈ [0, 31]，TN 的是 [0, 15]
 *     → 在这个前提下判 R1：TP：16..31 不满足 → L2（实现没兑现文档）；TN：成立
 */
#include "../common.h"

#define MAX_PORT_NUM    16

typedef struct {
    UINT32 ulMode;
    UINT32 ulSpeed;
    UINT8  aucMac[16];
} PORT_S;

static PORT_S g_astPort[MAX_PORT_NUM];

static void Port_WriteSpeed(UINT32 ulPortId, UINT32 ulSpeed) { g_astPort[ulPortId].ulSpeed = ulSpeed; }   /* ★1 */
static void Port_Apply(UINT32 ulPortId, UINT32 ulSpeed)      { Port_WriteSpeed(ulPortId, ulSpeed); }
static void Port_Cfg(UINT32 ulPortId, UINT32 ulSpeed)        { Port_Apply(ulPortId, ulSpeed); }

/**
 * 设置端口速率（对外接口）。
 * @param ulPortId  端口号，取值 0 ~ MAX_PORT_NUM_V2 - 1（0~31），由调用者保证
 */
void Port_SetSpeed_TP(UINT32 ulPortId, UINT32 ulSpeed) { Port_Cfg(ulPortId, ulSpeed); }

/**
 * 设置端口速率（对外接口）。
 * @param ulPortId  端口号，取值 0 ~ MAX_PORT_NUM - 1（0~15），由调用者保证
 */
void Port_SetSpeed_TN(UINT32 ulPortId, UINT32 ulSpeed) { Port_Cfg(ulPortId, ulSpeed); }
