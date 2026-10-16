/* lit phase-solve.test 的输入（SPEC-0029；与 unittests/core/data/solve_b.c 相同）：Fill 写 ulLen 字节；Send 哪里都没有定义。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Send(UINT8 *pucBuf);

static void Init(UINT8 *pucBuf)
{
    pucBuf[100] = 0;
}

void Fill(UINT8 *pucBuf, UINT32 ulLen)
{
    UINT32 i;
    for (i = 0; i < ulLen; i++) {
        pucBuf[i] = 0;
    }
    Send(pucBuf);
}

void Top_B(void)
{
    UINT8 aucBuf[64];
    Init(aucBuf);
}
