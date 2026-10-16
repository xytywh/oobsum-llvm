/* EngineTest 的输入（oobsum/specs/SPEC-0021 §5）：gen_engine_fir.py 把它降级成 engine-fir.json。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Ext_Send(UINT8 *pucBuf, UINT32 ulLen);

static void Leaf(UINT8 *pucBuf, UINT32 ulIdx)
{
    pucBuf[ulIdx] = 0;
}

static void Mid(UINT8 *pucBuf, UINT32 ulIdx)
{
    Leaf(pucBuf, ulIdx);
}

static void Up(UINT8 *pucBuf, UINT32 ulIdx)
{
    Mid(pucBuf, ulIdx);
}

void Top_TP(void)
{
    UINT8 aucBuf[16];
    Up(aucBuf, 16);
}

void Top_TN(void)
{
    UINT8 aucBuf[16];
    Up(aucBuf, 15);
}

void Top_L2(void)
{
    UINT8 aucBuf[16];
    UINT32 i;
    for (i = 0; i <= 16; i++) {
        Up(aucBuf, i);
    }
}

void Entry(UINT8 *pucIn)
{
    UINT8 aucTmp[4];
    UINT32 i;
    for (i = 0; i < 4; i++) {
        aucTmp[i] = pucIn[i];
    }
    Ext_Send(aucTmp, 4);
}

/* 两层封装的分配器（SPEC-0024）：Tbl_Alloc → Wrap_Alloc → VOS_MemAlloc（模型表），对象 8 字节 */
void *VOS_MemAlloc(UINT32 ulPid, UINT32 ulPtNo, UINT32 ulSize);

static void *Wrap_Alloc(UINT32 ulSize)
{
    return VOS_MemAlloc(1, 0, ulSize);
}

static UINT8 *Tbl_Alloc(void)
{
    UINT8 *pucTbl = (UINT8 *)Wrap_Alloc(8);
    if (pucTbl == 0) {
        return 0;
    }
    return pucTbl;
}

void Heap_TP(void)
{
    UINT8 *pucTbl = Tbl_Alloc();
    if (pucTbl == 0) {
        return;
    }
    Leaf(pucTbl, 8);
}
