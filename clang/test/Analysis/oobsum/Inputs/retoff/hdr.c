/* ret-new-offset.test 的输入（SPEC-0052）：带管理头的分配器返回 pucRaw + 16。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void *malloc(unsigned int n);

static UINT8 *Buf_Alloc(UINT32 ulLen)
{
    UINT8 *pucRaw = (UINT8 *)malloc(ulLen + 16);
    if (pucRaw == 0) {
        return 0;
    }
    return pucRaw + 16;
}
static void Txt_End(UINT8 *p, UINT32 n) { p[n] = 0; }
void Hdr_TP(void) { UINT8 *p = Buf_Alloc(32); if (p != 0) { Txt_End(p, 32); } }
void Hdr_OK(void) { UINT8 *p = Buf_Alloc(33); if (p != 0) { Txt_End(p, 32); } }

/* 对照：两条返回路径的偏移不同——不当成分配器 */
static UINT8 *Buf_Odd(UINT32 ulLen, UINT32 ulFlag)
{
    UINT8 *pucRaw = (UINT8 *)malloc(ulLen + 16);
    if (ulFlag != 0) {
        return pucRaw + 8;
    }
    return pucRaw + 16;
}
void Odd_TP(UINT32 ulFlag) { UINT8 *p = Buf_Odd(32, ulFlag); Txt_End(p, 32); }
