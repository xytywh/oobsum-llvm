/* header-key.test：BUF_LEN 32 → 16 字节数组越界（原来可能被 b.c 那份函数体判成 SAFE）。 */
#define BUF_LEN 32
#include "buf.h"
void A_TP(unsigned int i) { unsigned char a[16]; Buf_Put(a, i); }
