/* post-decrement.test 的输入（SPEC-0075）：后置自减当条件。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Clr(UINT8 *p, UINT32 n) { while (n--) { p[n] = 0; } }
void Clr_TP(void) { UINT8 a[16]; Clr(a, 17); }
void Clr_TN(void) { UINT8 a[16]; Clr(a, 16); }
static void Last(UINT8 *p, UINT32 len) { if (len-- > 0) { p[len] = 0; } }
void Last_TP(void) { UINT8 a[16]; Last(a, 17); }
void Last_TN(void) { UINT8 a[16]; Last(a, 16); }
