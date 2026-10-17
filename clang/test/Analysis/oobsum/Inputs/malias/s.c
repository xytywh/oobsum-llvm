/* member-alias-store.test 的输入（SPEC-0090）：形参结构体的成员数组与外层对象是同一块内存。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 n; UINT8 idx[4]; } CFG_S;
static void UseIdx(UINT8 *p, UINT8 i) { p[i] = 0; }
/* 经成员数组写 2，再经字节偏移把同一个字节写成 200 */
void S1(CFG_S *pc) { UINT8 a[4]; pc->idx[1] = 2; ((UINT8 *)pc)[5] = 200; UseIdx(a, pc->idx[1]); }
/* 反过来 */
void S2(CFG_S *pc) { UINT8 a[4]; ((UINT8 *)pc)[5] = 2; pc->idx[1] = 200; UseIdx(a, ((UINT8 *)pc)[5]); }
/* 局部结构体：同一基址，应该没问题 */
void S3(void) { UINT8 a[4]; CFG_S c; c.idx[1] = 2; ((UINT8 *)&c)[5] = 200; UseIdx(a, c.idx[1]); }
/* 对照：写标量字段不碰成员数组的格 */
void S4(CFG_S *pc) { UINT8 a[4]; pc->idx[1] = 2; pc->n = 5; UseIdx(a, pc->idx[1]); }
