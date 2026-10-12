/*
 * @id      LV9-03
 * @title   运行期注册的事件回调：注册在模块初始化里，触发在另一条链上
 * @scene   S9.3  A1·M1·P5·V1·R2·X0·K1·G0
 * @year    AI + 程序分析（2027 的指向分析也能做一部分）
 * @story   事件回调表按事件号存函数指针；小区模块注册的回调按 ulLen 位置写结尾。
 * @expect  TP  Evt_FireCellUp_TP  pa=记账  ai+pa=L1
 * @expect  TN  Evt_FireCellUp_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Cell_OnUpEvt(pucData, ulLen)     req R1  W1 @ pucData + ulLen    需 ulLen + 1 ≤ ext(pucData)         ★1
 *   Evt_Register(ulEvt, pfn)         eff g_apfnEvt[ulEvt] := pfn       （写进全局函数指针表）
 *   Evt_Fire(ulEvt, pucData, ulLen)  调用 g_apfnEvt[ulEvt](…) → 目标未知 → 断链
 * 串联
 *   年内：断链，记账"函数指针（全局表）"
 *   AI + 程序分析：AI（或 2027 的指向分析）从所有 Evt_Register 调用点恢复 g_apfnEvt[EVT_CELL_UP] ∈ {Cell_OnUpEvt}
 *     → 作为外部调用边注入 → 程序分析代换 → TP：ulLen 16、缓冲 16 → 17 ≤ 16 → L1；TN：ulLen 15 → 成立
 */
#include "../common.h"

#define EVT_CELL_UP 0
#define EVT_BUTT    4

typedef void (*EVT_FUNC)(UINT8 *pucData, UINT32 ulLen);

static EVT_FUNC g_apfnEvt[EVT_BUTT];

void Evt_Register(UINT32 ulEvt, EVT_FUNC pfn)
{
    if (ulEvt < EVT_BUTT) {
        g_apfnEvt[ulEvt] = pfn;
    }
}

static void Cell_OnUpEvt(UINT8 *pucData, UINT32 ulLen) { pucData[ulLen] = 0; }      /* ★1 */

void Cell_ModInit(void) { Evt_Register(EVT_CELL_UP, Cell_OnUpEvt); }

static void Evt_Fire(UINT32 ulEvt, UINT8 *pucData, UINT32 ulLen)
{
    if (ulEvt < EVT_BUTT && g_apfnEvt[ulEvt] != VOS_NULL_PTR) {
        g_apfnEvt[ulEvt](pucData, ulLen);
    }
}

static void Evt_Notify(UINT32 ulEvt, UINT8 *pucData, UINT32 ulLen) { Evt_Fire(ulEvt, pucData, ulLen); }

void Evt_FireCellUp_TP(void)
{
    UINT8 aucData[16];
    Evt_Notify(EVT_CELL_UP, aucData, sizeof(aucData));
}

void Evt_FireCellUp_TN(void)
{
    UINT8 aucData[16];
    Evt_Notify(EVT_CELL_UP, aucData, sizeof(aucData) - 1);
}
