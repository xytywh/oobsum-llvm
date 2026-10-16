/* underflow.test 的输入（SPEC-0039）：负偏移（下溢）。 */
typedef unsigned int UINT32;
typedef int INT32;
typedef unsigned char UINT8;

/* 带符号下标减 1：i = 0 时写 p[-1] */
static void Neg_Set(UINT8 *p, INT32 i)
{
    p[i - 1] = 0;
}
void Neg_TP(void)
{
    UINT8 auc[16];
    Neg_Set(auc, 0);
}
void Neg_OK(void)
{
    UINT8 auc[16];
    Neg_Set(auc, 1);
}

/* 指针往回退 4 字节 */
static void Back_Set(UINT8 *p)
{
    UINT8 *q = p - 4;
    *q = 0;
}
void Back_TP(void)
{
    UINT8 auc[16];
    Back_Set(auc);
}
void Back_OK(void)
{
    UINT8 auc[16];
    Back_Set(auc + 8);
}

/* 检查了下界：不生成反向 Req */
static void Neg_Checked(UINT8 *p, INT32 i)
{
    if (i < 1 || i > 16) {
        return;
    }
    p[i - 1] = 0;
}
void Neg_Checked_OK(INT32 i)
{
    UINT8 auc[16];
    Neg_Checked(auc, i);
}

/* 入口函数：传进来的指针前面有多少字节不知道 */
void Back_Entry(UINT8 *p)
{
    Back_Set(p);
}

/* 无符号下标减 1 回绕成大数：是上溢，不是负偏移 */
static void Wrap_Set(UINT8 *p, UINT32 ul)
{
    p[ul - 1] = 0;
}
void Wrap_TP(void)
{
    UINT8 auc[16];
    Wrap_Set(auc, 0);
}
