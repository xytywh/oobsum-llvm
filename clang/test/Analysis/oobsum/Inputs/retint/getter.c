/* ret-int.test 的输入（SPEC-0054）：返回值范围传给调用方。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

static UINT32 Num_Clamp(UINT32 n) { return (n > 8) ? 8 : n; }
static UINT32 Num_Get(UINT32 n) { return Num_Clamp(n); }
static void Fill(UINT8 *p, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { p[i] = 0; } }
void Clamp_OK(UINT32 c) { UINT8 a[8]; Fill(a, Num_Get(c)); }
void Clamp_TP(UINT32 c) { UINT8 a[4]; Fill(a, Num_Get(c)); }

/* 同一个确切式子：调用方代换成确切值 */
static UINT32 Next(UINT32 x) { return x + 1; }
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
void Next_TP(void) { UINT8 a[8]; Put(a, Next(7)); }

/* 对照：没有守卫的返回只有类型范围 */
static UINT32 Raw(UINT32 *p) { return *p; }
void Raw_TP(UINT32 *q) { UINT8 a[8]; Put(a, Raw(q)); }
