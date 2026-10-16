/* congruence.test 的输入（SPEC-0084）：区间带同余（值 ≡ 锚点 mod 步长）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef struct { UINT32 a; UINT32 b; UINT8 c[8]; } ENT_S;
/* 结构体指针按元素走：偏移 ≡ 0 mod 16 */
static void E1(ENT_S *p, UINT32 n) { ENT_S *q; for (q = p; q < p + n; q++) { q->c[7] = 0; } }
void E1_TP(void) { ENT_S a[4]; E1(a, 5); }
void E1_TN(void) { ENT_S a[4]; E1(a, 4); }
/* 整数按 4 走：上界取整成 4·div(n − 1, 4) */
static void I4(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i += 4) { *(UINT32 *)(p + i) = 0; } }
void I4_TP(void) { UINT32 a[4]; I4((UINT8 *)a, 17); }
void I4_TN(void) { UINT32 a[4]; I4((UINT8 *)a, 16); }
/* 结束指针按字节长度算（不一定是 16 的倍数）：取整成 16·div(len − 1, 16)，len = 40 时最后一个元素真的越界 */
static void B1(UINT8 *buf, UINT32 len)
{
    ENT_S *q = (ENT_S *)buf;
    ENT_S *e = (ENT_S *)(buf + len);
    for (; q < e; q++) {
        q->c[7] = 0;
    }
}
void B1_TP(void) { UINT8 a[40]; B1(a, 40); }
void B1_TN(void) { UINT8 a[48]; B1(a, 48); }
/* != 结束、按元素走（SPEC-0082 的阈值 + 同余） */
static void W4(UINT32 *p, UINT32 n) { UINT32 *q; for (q = p; q != p + n; q++) { *q = 0; } }
void W4_TP(void) { UINT32 a[4]; W4(a, 5); }
void W4_TN(void) { UINT32 a[4]; W4(a, 4); }
