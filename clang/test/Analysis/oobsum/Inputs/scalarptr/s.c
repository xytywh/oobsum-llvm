/* scalar-pointee.test 的输入（SPEC-0071）：经指针传进来的整数。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Fill(UINT8 *p, const UINT32 *pLen) { UINT32 i; for (i = 0; i < *pLen; i++) { p[i] = 0; } }
void A_TP(void) { UINT8 a[16]; UINT32 n = 20; Fill(a, &n); }
void A_TN(void) { UINT8 a[16]; UINT32 n = 16; Fill(a, &n); }
/* 调用方把自己的形参原样传下去：上传成调用方的 p1->_0 */
void Mid(UINT8 *p, UINT32 *pulLen) { Fill(p, pulLen); }
void Top_TP(void) { UINT8 a[16]; UINT32 n = 17; Mid(a, &n); }
/* 调用前被 Set 改过（写效应，SPEC-0073） */
static void Set(UINT32 *pn) { *pn = 20; }
void C_TP(void) { UINT8 a[16]; UINT32 n = 16; Set(&n); Fill(a, &n); }
/* 同类型的两个整数指针形参可能是同一个变量：先写后读不当入口值 */
static void Both(UINT8 *p, UINT32 *pulOut, const UINT32 *pulLen) { *pulOut = 100; Fill(p, pulLen); }
void Both_TP(void) { UINT8 a[16]; UINT32 n = 16; Both(a, &n, &n); }
