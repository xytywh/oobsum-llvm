/*
 * @id      LV10-01
 * @title   报文结构体有两组"缓冲区 + 长度"，访问 ext 时用 dataLen 做了校验
 * @scene   S10.1  A1·M8·P3·V3·R2·X0·K1·G4
 * @year    AI + 程序分析
 * @story   驱动交上来的报文描述符；缓冲区都在分析范围之外分配。哪个长度字段属于哪个缓冲区，只有业务知道。
 * @expect  TP  Drv_OnRxPkt_TP  pa=记账  ai+pa=L2
 * @expect  TN  Drv_OnRxPkt_TN  pa=记账  ai+pa=-
 *
 * 摘要
 *   Pkt_SetExt(pstPkt, ulOff, ucVal)
 *     req R1  ulOff < pstPkt->ulDataLen ⇒ W1 @ pstPkt->pucExt + ulOff   需 ulOff + 1 ≤ ext(pstPkt->pucExt)   ★1
 *   Pkt_SetExt_OK：守卫换成 ulOff < pstPkt->ulExtLen
 *   Pkt_MarkExt / Pkt_Proc   原样上传；入口 Drv_OnRxPkt_* 的 pstPkt 来自驱动 → 条件停在入口，记账
 * 串联
 *   年内：TP、TN 都是"入口未证明"，记账
 *   AI + 程序分析：AI 推断字段配对 pucExt ↔ ulExtLen、pucData ↔ ulDataLen（命名 + 其它模块里的申请点），
 *     作为契约 ext(pstPkt->pucExt) = pstPkt->ulExtLen 注入；程序分析在所有访问点检查：
 *       TP：守卫 ulOff < ulDataLen 推不出 ulOff < ulExtLen → L2
 *       TN：守卫 ulOff < ulExtLen ⇒ ulOff + 1 ≤ ulExtLen → 成立
 *   对照：如果 pucExt 在汇合点指向一个已知大小的数组，这就是 Lv3 的"校验界与实际大小不一致"，不需要 AI。
 */
#include "../common.h"

typedef struct {
    UINT8  *pucData;
    UINT32  ulDataLen;
    UINT8  *pucExt;
    UINT32  ulExtLen;
} PKT_S;

static void Pkt_SetExt(PKT_S *pstPkt, UINT32 ulOff, UINT8 ucVal)
{
    if (ulOff >= pstPkt->ulDataLen) {
        return;
    }
    pstPkt->pucExt[ulOff] = ucVal;                            /* ★1 */
}

static void Pkt_SetExt_OK(PKT_S *pstPkt, UINT32 ulOff, UINT8 ucVal)
{
    if (ulOff >= pstPkt->ulExtLen) {
        return;
    }
    pstPkt->pucExt[ulOff] = ucVal;
}

static void Pkt_MarkExt(PKT_S *p, UINT32 o, UINT8 v)    { Pkt_SetExt(p, o, v); }
static void Pkt_MarkExt_OK(PKT_S *p, UINT32 o, UINT8 v) { Pkt_SetExt_OK(p, o, v); }
static void Pkt_Proc(PKT_S *p, UINT32 o, UINT8 v)       { Pkt_MarkExt(p, o, v); }
static void Pkt_Proc_OK(PKT_S *p, UINT32 o, UINT8 v)    { Pkt_MarkExt_OK(p, o, v); }

void Drv_OnRxPkt_TP(PKT_S *pstPkt, UINT32 ulOff) { Pkt_Proc(pstPkt, ulOff, 0x7E); }
void Drv_OnRxPkt_TN(PKT_S *pstPkt, UINT32 ulOff) { Pkt_Proc_OK(pstPkt, ulOff, 0x7E); }
