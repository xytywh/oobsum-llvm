/* callee-writes.test 的输入（SPEC-0038）：被调者把字段 / 全局从 4 改成 100，调用方不能再按 4 证明安全。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 ulLen; UINT8 *pucData; } BUF_S;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);

static void Buf_SetLen(BUF_S *p) { p->ulLen = 100; }
static void Buf_Clear(UINT8 *p, UINT32 n) { (void)memset_s(p, n, 0, n); }

void Top_TP(void)
{
    UINT8 auc[16];
    BUF_S st;
    st.ulLen = 4;
    Buf_SetLen(&st);
    Buf_Clear(auc, st.ulLen);
}

extern void Ext_SetLen(BUF_S *p);
void TopExt_TP(void)
{
    UINT8 auc[16];
    BUF_S st;
    st.ulLen = 4;
    Ext_SetLen(&st);
    Buf_Clear(auc, st.ulLen);
}

static UINT32 g_ulLen;
static void G_Set(void) { g_ulLen = 100; }
void TopG_TP(void)
{
    UINT8 auc[16];
    g_ulLen = 4;
    G_Set();
    Buf_Clear(auc, g_ulLen);
}
