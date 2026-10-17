/* arg-offset-range.test 的输入（SPEC-0095）：指针实参的偏移是区间时按对象起点 + 偏移区间判。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
UINT8 GetIdx(void);
void T3(void) { UINT8 a[300]; UINT8 j = GetIdx(); Put(a + j, 12); }
void T4(UINT32 c) { UINT8 a[16]; UINT32 k = c ? 2 : 6; Put(a + k, 12); }
void T5(UINT32 c) { UINT8 a[16]; UINT8 *p = c ? a + 2 : a + 3; Put(p, 12); }
void T6(UINT32 c) { UINT8 a[16]; UINT8 *p = c ? a + 6 : a + 8; Put(p, 12); }
static void Mid(UINT8 *q, UINT32 c) { UINT8 *p = c ? q + 2 : q + 6; Put(p, 12); }
void T7(UINT32 c) { UINT8 a[16]; Mid(a, c); }
void T8(UINT32 c) { UINT8 a[32]; Mid(a, c); }
