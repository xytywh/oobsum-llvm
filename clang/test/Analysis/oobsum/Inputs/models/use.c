/* model-file.test 的输入（SPEC-0033）：三个函数的模型都只在 extra.yaml 里。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

void Drv_Copy(void *dst, UINT32 n, const void *src);
void *Pool_Get(UINT32 count);
void Stat_Inc(UINT32 *counter);

static void Fill(UINT8 *buf, UINT32 n)
{
    UINT8 src[64] = {0};
    Drv_Copy(buf, n, src);
}

void Copy_TP(void)
{
    UINT8 buf[16];
    Fill(buf, 32);
}

static void Put(UINT32 *p, UINT32 i)
{
    p[i] = 0;
}

void Pool_TP(void)
{
    UINT32 *p = Pool_Get(4);
    if (p == 0) {
        return;
    }
    Put(p, 4);
}

void Stat_OK(UINT32 *counter)
{
    Stat_Inc(counter);
}
