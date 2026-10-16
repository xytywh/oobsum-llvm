/* out-new.test 的输入（SPEC-0053）：出参式分配器把新对象写进 *pp。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void *malloc(unsigned int n);
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);

static UINT32 Buf_Create(UINT8 **pp, UINT32 n)
{
    *pp = (UINT8 *)malloc(n);
    return (*pp == 0) ? 1 : 0;
}
static UINT32 Rpt_Create(UINT8 **pp) { return Buf_Create(pp, 64); }
static void Rpt_Clear(UINT8 *p, UINT32 n) { (void)memset_s(p, n, 0, n); }
void Rpt_TP(void) { UINT8 *p = 0; if (Rpt_Create(&p) != 0) { return; } Rpt_Clear(p, 72); }
void Rpt_OK(void) { UINT8 *p = 0; if (Rpt_Create(&p) != 0) { return; } Rpt_Clear(p, 64); }

/* 对照：只有一条路径写出参——不当成分配器 */
static UINT32 Buf_Maybe(UINT8 **pp, UINT32 n, UINT32 f)
{
    if (f != 0) {
        *pp = (UINT8 *)malloc(n);
    }
    return 0;
}
void Maybe_TP(UINT32 f) { UINT8 *p = 0; (void)Buf_Maybe(&p, 64, f); Rpt_Clear(p, 72); }
