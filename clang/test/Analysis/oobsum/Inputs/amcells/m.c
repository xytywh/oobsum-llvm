/* access-model-cells.test 的输入（SPEC-0089）：访问类模型只失效目的对象的格。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
extern int memcpy_s(void *d, UINT32 dm, const void *s, UINT32 n);
extern int memset_s(void *d, UINT32 dm, int c, UINT32 n);
typedef struct { UINT32 len; UINT8 buf[8]; } MSG_S;
static void UseLen(UINT8 *p, const MSG_S *m) { p[m->len] = 0; }
/* memcpy_s 写的是另一个对象：m.len 应该还在 */
void M1_TP(const UINT8 *src) { UINT8 a[4]; UINT8 tmp[16]; MSG_S m; m.len = 100; (void)memcpy_s(tmp, 16, src, 16); UseLen(a, &m); }
void M1_TN(const UINT8 *src) { UINT8 a[4]; UINT8 tmp[16]; MSG_S m; m.len = 2; (void)memset_s(tmp, 16, 0, 16); UseLen(a, &m); }
/* 写的就是 m：len 不知道 */
void M2(const UINT8 *src) { UINT8 a[4]; MSG_S m; m.len = 2; (void)memcpy_s(&m, sizeof(m), src, sizeof(m)); UseLen(a, &m); }
/* 形参结构体：先经成员数组写，再 memset_s 整个结构体，再读成员数组元素当下标 */
typedef struct { UINT32 n; UINT8 idx[4]; } CFG_S;
static void UseIdx(UINT8 *p, UINT8 i) { p[i] = 0; }
void M3(CFG_S *pc) { UINT8 a[4]; pc->idx[1] = 2; (void)memset_s(pc, sizeof(*pc), 0x7f, sizeof(*pc)); UseIdx(a, pc->idx[1]); }
/* 反过来：经外层写，再 memcpy_s 进成员数组 */
void M4(CFG_S *pc, const UINT8 *src) { UINT8 a[4]; pc->idx[1] = 2; (void)memcpy_s(pc->idx, 4, src, 4); UseIdx(a, pc->idx[1]); }
