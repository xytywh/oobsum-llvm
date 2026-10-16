/* recursion.test 的输入（SPEC-0066）：递归调用点记账 recursion。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Rec(UINT8 *p, UINT32 n) { if (n == 0) { return; } p[n] = 0; Rec(p, n - 1); }
void Rec_TP(void) { UINT8 a[16]; Rec(a, 16); }
void Rec_TN(void) { UINT8 a[16]; Rec(a, 15); }
static void B(UINT8 *p, UINT32 n);
static void A(UINT8 *p, UINT32 n) { B(p, n + 1); }
static void B(UINT8 *p, UINT32 n) { if (n < 20) { A(p, n); } p[n] = 0; }
void Mut_TP(void) { UINT8 a[16]; A(a, 0); }
static void Walk(UINT8 *p, UINT32 n, UINT32 depth) { if (depth == 0) { p[n] = 0; return; } Walk(p, n + 4, depth - 1); }
void Walk_TP(void) { UINT8 a[16]; Walk(a, 0, 8); }
/* 对照：不递归的调用不记 */
static void Leaf(UINT8 *p, UINT32 n) { p[n] = 0; }
void Plain_TN(void) { UINT8 a[16]; Leaf(a, 3); }
