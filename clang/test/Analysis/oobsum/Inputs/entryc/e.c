/* entry-constant.test 的输入（SPEC-0087）：入口函数自己的常数访问按 F-11 / F-12 判级。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void T9(void) { UINT8 a[4]; UINT32 i = 100; a[i] = 0; }
void T12(void) { UINT8 a[4]; UINT32 i; for (i = 0; i < 10; i++) { a[i] = 0; } }
/* 对照：带入口形参的照旧记账 */
void T13(UINT32 n) { UINT8 a[4]; a[n] = 0; }
