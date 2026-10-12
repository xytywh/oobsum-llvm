/*
 * @id      LV1-08
 * @title   拷贝模板：源缓冲区逐层下传，最底层按新长度读（读越界）
 * @scene   S1.7  A3·M2·P1·V0·R1·X0·K0·G0
 * @year    承诺
 * @story   配置块从 32 字节扩到 64 字节，最底层的拷贝改成按 CFG_BLOCK_LEN 读，
 *          但老的默认模板还是 32 字节。
 * @expect  TP  Cfg_InitDefault_TP  pa=L1
 * @expect  TN  Cfg_InitDefault_TN  pa=-
 *
 * 摘要
 *   Cfg_CopyBlock(pucDst, pucTmpl)
 *     req R1  W64 @ pucDst + 0      需 64 ≤ ext(pucDst)       （memcpy_s 的 destMax）           ★1
 *     req R2  R64 @ pucTmpl + 0     需 64 ≤ ext(pucTmpl)      （memcpy_s 的 count，读源）         ★1
 *   Cfg_ApplyTmpl / Cfg_Load        req R1、R2 原样上传
 * 串联
 *   Cfg_InitDefault_TP：pucTmpl = g_aucDefaultTmpl（32）→ R2：64 ≤ 32 不成立 → L1
 *                       pucDst = 入口形参 pucCfg → R1 停在入口（StillOpen，不报，记账"入口未证明"）
 *   Cfg_InitDefault_TN：g_aucDefaultTmplV2（64）→ R2 成立；R1 同上
 *   要点：同一个访问点的两条条件可以有不同的结局，各自独立判定。
 */
#include "../common.h"

#define CFG_BLOCK_LEN 64

static const UINT8 g_aucDefaultTmpl[32]   = { 0 };
static const UINT8 g_aucDefaultTmplV2[64] = { 0 };

static void Cfg_CopyBlock(UINT8 *pucDst, const UINT8 *pucTmpl)
{
    (void)memcpy_s(pucDst, CFG_BLOCK_LEN, pucTmpl, CFG_BLOCK_LEN);   /* ★1 */
}

static void Cfg_ApplyTmpl(UINT8 *pucDst, const UINT8 *pucTmpl)
{
    Cfg_CopyBlock(pucDst, pucTmpl);
}

static void Cfg_Load(UINT8 *pucDst, const UINT8 *pucTmpl)
{
    Cfg_ApplyTmpl(pucDst, pucTmpl);
}

void Cfg_InitDefault_TP(UINT8 *pucCfg) { Cfg_Load(pucCfg, g_aucDefaultTmpl); }
void Cfg_InitDefault_TN(UINT8 *pucCfg) { Cfg_Load(pucCfg, g_aucDefaultTmplV2); }
