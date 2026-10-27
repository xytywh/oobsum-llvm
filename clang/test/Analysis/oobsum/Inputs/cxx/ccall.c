/* C 文件调用 impl.cpp 里 extern "C" 的函数（SPEC-0121）：X_TP → Up → Mid_C → Fill_C，三层 → DEFINITE；X_TN 不报 */
typedef unsigned char UINT8;
typedef unsigned int UINT32;

void Mid_C(UINT8 *p, UINT32 i);
static void Up(UINT8 *p, UINT32 i) { Mid_C(p, i); }
void X_TP(void) { UINT8 a[16]; Up(a, 16); }
void X_TN(void) { UINT8 a[16]; Up(a, 15); }
