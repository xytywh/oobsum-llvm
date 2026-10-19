/* range-range-narrow.test 的输入（SPEC-0110）：长度先夹到上限再循环。 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;
static void Clamp(UINT8 *pucDst, UINT32 ulLen) { UINT32 n = ulLen > 16 ? 16 : ulLen; UINT32 i; for (i = 0; i < n; i++) { pucDst[i] = 0; } }
void Clamp_TP(void) { UINT8 a[8]; Clamp(a, 100); }
void Clamp_TN(void) { UINT8 a[16]; Clamp(a, 100); }
/* 对照：上界只来自类型（数据）——不用来收窄，照旧记账 */
static void Data(UINT8 *pucDst, const UINT8 *pucLen) { UINT32 n = pucLen[0]; UINT32 i; for (i = 0; i < n; i++) { pucDst[i] = 0; } }
void Data_Use(const UINT8 *pucLen) { UINT8 a[16]; Data(a, pucLen); }
