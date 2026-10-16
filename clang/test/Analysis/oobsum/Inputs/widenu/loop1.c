/* widen-unsigned.test 的输入（SPEC-0050）：从 1 开始的无符号循环。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

static void Fill_Prev(UINT8 *p) { UINT32 u; for (u = 1; u <= 16; u++) { p[u - 1] = 0; } }
static void Fill_Cur(UINT8 *p) { UINT32 u; for (u = 1; u <= 16; u++) { p[u] = 0; } }
static void Fill_Lt(UINT8 *p) { UINT32 u; for (u = 1; u < 17; u++) { p[u] = 0; } }
void Prev_OK(void) { UINT8 a[16]; Fill_Prev(a); }
void Cur_TP(void) { UINT8 a[16]; Fill_Cur(a); }
void Lt_TP(void) { UINT8 a[16]; Fill_Lt(a); }

/* 对照：没有条件收住的循环——照旧记账 */
static void Fill_Forever(UINT8 *p) { UINT32 u; for (u = 1;; u++) { p[u] = 0; } }
void Forever_TP(void) { UINT8 a[16]; Fill_Forever(a); }
