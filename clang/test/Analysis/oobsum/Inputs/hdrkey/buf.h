/* header-key.test 的头文件（SPEC-0059）：同一个 static inline，各 TU 的 BUF_LEN 不同。 */
static inline void Buf_Put(unsigned char *p, unsigned int i) { if (i < BUF_LEN) { p[i] = 0; } }
