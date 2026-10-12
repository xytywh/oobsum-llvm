/*
 * @id      LV2-08
 * @title   协议栈逐层加头：每层偏移都没问题，加起来超了帧长
 * @scene   S2.3  A2·M1·P1·V0·R1·X1·K0·G0
 * @year    承诺
 * @story   PHY / MAC / RLC 各自只看自己的头长；RLC 新增了 2 字节扩展头后，CRC 位置落到了帧外。
 * @expect  TP  Frame_Build_TP  pa=L1
 * @expect  TN  Frame_Build_TN  pa=-
 *
 * 摘要
 *   Rlc_FillCrc(pucRlc)        req R1  W1 @ pucRlc + 50       需 51 ≤ ext(pucRlc)                ★1
 *   Mac_Fill(pucMac)           实参 pucMac + 6 → R1 变成 需 57 ≤ ext(pucMac)
 *                              （本层的 pucMac[0] 需 1 ≤ ext，被覆盖）
 *   Phy_Fill(pucFrame)         实参 pucFrame + 8 → 需 65 ≤ ext(pucFrame)
 * 串联
 *   Frame_Build_TP：aucFrame（64）→ 65 ≤ 64 不成立 → L1，trace 上每一层的偏移都列出来：+8 → +6 → +50
 *   Frame_Build_TN：aucFrame（72）→ 成立
 *   要点：偏移是线性累加的，每过一层只改常数项，条件的形状不变。
 */
#include "../common.h"

#define FRAME_LEN        64
#define FRAME_LEN_V2     72
#define PHY_HDR_LEN      8
#define MAC_HDR_LEN      6
#define RLC_HDR_LEN      2
#define RLC_PAYLOAD_LEN  48

static void Rlc_FillCrc(UINT8 *pucRlc)
{
    pucRlc[RLC_HDR_LEN + RLC_PAYLOAD_LEN] = 0x5A;              /* ★1 */
}

static void Mac_Fill(UINT8 *pucMac)
{
    pucMac[0] = 0x01;
    Rlc_FillCrc(pucMac + MAC_HDR_LEN);
}

static void Phy_Fill(UINT8 *pucFrame)
{
    pucFrame[0] = 0xAA;
    Mac_Fill(pucFrame + PHY_HDR_LEN);
}

void Frame_Build_TP(void)
{
    UINT8 aucFrame[FRAME_LEN];
    Phy_Fill(aucFrame);
}

void Frame_Build_TN(void)
{
    UINT8 aucFrame[FRAME_LEN_V2];
    Phy_Fill(aucFrame);
}
