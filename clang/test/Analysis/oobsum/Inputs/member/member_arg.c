/* member-arg.test 的输入（oobsum/specs/SPEC-0035）：成员数组当实参，外层对象也要查。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct {
    UINT32 ulLen;
    UINT8 aucBody[1024];
} MSG;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);

static void Body_Clear(UINT8 *p, UINT32 n)
{
    (void)memset_s(p, n, 0, n);
}

static void Msg_Clear(MSG *pstMsg)
{
    Body_Clear(pstMsg->aucBody, 512);
}

void Top_TP(void)
{
    UINT8 auc[64];
    Msg_Clear((MSG *)auc);
}

void Top_OK(void)
{
    MSG st;
    Msg_Clear(&st);
}

static void Msg_ClearDirect(MSG *pstMsg)
{
    (void)memset_s(pstMsg->aucBody, 512, 0, 512);
}

void TopDirect_TP(void)
{
    UINT8 auc[64];
    Msg_ClearDirect((MSG *)auc);
}

static void Msg_ClearAll(MSG *pstMsg)
{
    Body_Clear(pstMsg->aucBody, 2048);
}

void TopAll_TP(void)
{
    MSG st;
    Msg_ClearAll(&st);
}

/* 入口把成员数组传下去：成员、外层对象两条 typed_field 账本，只报一条（SPEC-0036） */
void Entry_Msg(MSG *pstMsg, UINT32 n)
{
    Body_Clear(pstMsg->aucBody, n);
}
