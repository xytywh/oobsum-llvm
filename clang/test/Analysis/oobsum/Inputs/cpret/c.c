/* caller-param-ret.test 的输入（SPEC-0092）：调用方把自己的形参传给被调者，返回值式子照样代换。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
static UINT32 Id(UINT32 x) { return x; }
static void Mid(UINT8 *p, UINT32 n) { Put(p, Id(n)); }
void P1_TP(void) { UINT8 a[4]; Mid(a, 4); }
void P1_TN(void) { UINT8 a[4]; Mid(a, 3); }
static UINT32 Half(UINT32 x) { return x / 2; }
static void Mid2(UINT8 *p, UINT32 n) { Put(p, Half(n)); }
void P2_TP(void) { UINT8 a[4]; Mid2(a, 8); }
void P2_TN(void) { UINT8 a[4]; Mid2(a, 6); }
static UINT32 Idx(UINT32 x) { return x + 1; }
static UINT32 Twice(UINT32 x) { return Idx(Idx(x)); }
void R7(void) { UINT8 a[4]; Put(a, Twice(2)); }
