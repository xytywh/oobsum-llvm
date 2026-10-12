/*
 * @id      LV6-01
 * @title   跨文件：extern 不完整声明的全局统计表，下标常量从三层之上传入
 * @scene   S6.1  A1·M2·P4·V1·R1·X0·K0·G0（跨 TU）
 * @year    尽力
 * @story   本文件只有 extern UINT32 g_aulPortStat[];——看不到大小。表定义在 port_stat_def.c 里，16 项。
 * @expect  TP  Port_OnLinkDown_TP  pa=L1
 * @expect  TN  Port_OnLinkDown_TN  pa=-
 *
 * 摘要
 *   Stat_PortInc(ulPort)
 *     req R1  W4 @ g_aulPortStat + 4·ulPort     需 4·ulPort + 4 ≤ ext(g_aulPortStat)               ★1
 *     （本文件里 ext(g_aulPortStat) 是未知符号，所以条件留成符号，不在这里判）
 *   Port_CountDown / Port_HandleDown           原样上传
 * 串联
 *   Port_OnLinkDown_TP：ulPort = 16 → 需 68 ≤ ext(g_aulPortStat)
 *                       查跨 TU 全局大小表（扫 port_stat_def.c 时登记 g_aulPortStat = 64 字节）→ 68 ≤ 64 不成立 → L1
 *   没有全局大小表时：条件停在 ext(g_aulPortStat) 上 → 不报，记账"全局大小未知"
 */
#include "../../common.h"

#define MAX_PORT_NUM 16

extern UINT32 g_aulPortStat[];

static void Stat_PortInc(UINT32 ulPort)     { g_aulPortStat[ulPort]++; }   /* ★1 */
static void Port_CountDown(UINT32 ulPort)   { Stat_PortInc(ulPort); }
static void Port_HandleDown(UINT32 ulPort)  { Port_CountDown(ulPort); }

void Port_OnLinkDown_TP(void) { Port_HandleDown(MAX_PORT_NUM); }
void Port_OnLinkDown_TN(void) { Port_HandleDown(MAX_PORT_NUM - 1); }
