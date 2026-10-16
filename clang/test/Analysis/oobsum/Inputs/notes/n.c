/* f33-notes.test 的输入（SPEC-0078）：三层链 + 堆对象。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void *malloc(UINT32 n);
static void Leaf(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Mid(UINT8 *p, UINT32 i) { Leaf(p, i); }
void Top_TP(void)
{
    UINT8 *p = (UINT8 *)malloc(16);
    Mid(p, 16);
}
/* 入口自己的访问：一跳的账本，不加 note */
void Entry(UINT8 *p) { p[3] = 0; }
