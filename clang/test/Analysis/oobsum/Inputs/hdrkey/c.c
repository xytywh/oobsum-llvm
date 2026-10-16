/* header-key.test：与 b.c 的宏相同 → 同一个键（只求解一次，D-08）。 */
#define BUF_LEN 16
#include "buf.h"
void C_TN(unsigned int i) { unsigned char c[16]; Buf_Put(c, i); }
