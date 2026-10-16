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
