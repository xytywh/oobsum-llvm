/* out-new-field.test 的输入（SPEC-0061）：申请结果写进形参所指结构体的指针字段（LV5-05 同形）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
#define NULL ((void *)0)
void *VOS_MemAlloc(UINT32 ulPid, UINT32 ulType, UINT32 ulSize);

typedef struct { UINT32 ulId; UINT8 *pucRes; UINT32 ulLen; } RES_S;

static UINT32 Res_Alloc(RES_S *pst) { pst->pucRes = VOS_MemAlloc(1, 0, 64); if (pst->pucRes == NULL) { return 1; } pst->ulLen = 64; return 0; }
static void Res_Write(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Res_Fill(RES_S *pst, UINT32 i) { Res_Write(pst->pucRes, i); }
void Field_TP(void) { RES_S st; if (Res_Alloc(&st) != 0) { return; } Res_Fill(&st, 64); }
void Field_TN(void) { RES_S st; if (Res_Alloc(&st) != 0) { return; } Res_Fill(&st, 63); }

/* 对照：只有一条路径写字段 → 没有效果、记账 */
static UINT32 Res_Maybe(RES_S *pst, UINT32 on) { if (on) { pst->pucRes = VOS_MemAlloc(1, 0, 64); return 0; } return 1; }
void Maybe_TP(UINT32 on) { RES_S st; if (Res_Maybe(&st, on) != 0) { return; } Res_Fill(&st, 64); }
