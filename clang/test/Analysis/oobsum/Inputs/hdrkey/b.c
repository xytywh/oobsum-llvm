/* header-key.test：BUF_LEN 16 → 安全。 */
#define BUF_LEN 16
#include "buf.h"
void B_TN(unsigned int i) { unsigned char b[16]; Buf_Put(b, i); }
