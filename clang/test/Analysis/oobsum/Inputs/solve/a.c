/* lit phase-solve.test 的输入（SPEC-0029；与 unittests/core/data/solve_a.c 相同）：A 调 B 里定义的 Fill；两个文件各有一个同名 static Init。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Fill(UINT8 *pucBuf, UINT32 ulLen);

static void Init(UINT8 *pucBuf)
{
    pucBuf[0] = 0;
}

void Top_A(void)
{
    UINT8 aucBuf[16];
    Init(aucBuf);
    Fill(aucBuf, 17);
}
