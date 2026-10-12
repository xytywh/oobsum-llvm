/*
 * @id      LV1-07
 * @title   跨文件：告警 ID 用了 BUTT，经三个文件三层到达标志数组
 * @scene   S1.6  A1·M2·P0·V1·R1·X0·K0·G0（跨 TU）
 * @year    承诺
 * @story   与 LV1-01 同一类错误，但链分布在 alm_top.c / alm_mid.c / alm_low.c 三个文件，
 *          用来验收跨文件执行（多轮扫描 + SummaryDB 工作表）。
 * @expect  TP  Alm_ReportAll_TP  pa=L1
 * @expect  TN  Alm_ReportAll_TN  pa=-
 *
 * 摘要（分在三个文件里生成，经 SummaryDB 拼起来）
 *   alm_low.c  Alm_SetFlag(ulAlmId)    req R1  W1 @ g_aucAlmFlag + ulAlmId   需 ulAlmId ≤ 2     ★1
 *   alm_low.c  Alm_RaiseOne(ulAlmId)   req R1 原样上传
 *   alm_mid.c  Alm_Dispatch(ulAlmId, ulCellId)   req R1 原样上传（第 1 轮时 Alm_RaiseOne 的摘要可能还没生成：
 *              标记 Pending，等第 2 轮）
 * 串联
 *   Alm_ReportAll_TP：ulAlmId = ALM_ID_BUTT = 3 → 3 ≤ 2 不成立 → L1，trace 跨 3 个文件
 *   Alm_ReportAll_TN：ALM_ID_CLK_LOSS = 2 → 成立
 */
#include "lv1_07.h"

void Alm_ReportAll_TP(UINT32 ulCellId) { Alm_Dispatch(ALM_ID_BUTT, ulCellId); }
void Alm_ReportAll_TN(UINT32 ulCellId) { Alm_Dispatch(ALM_ID_CLK_LOSS, ulCellId); }
