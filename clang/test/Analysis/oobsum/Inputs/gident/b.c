/* ① b.c 自己的 static g_len = 2 不能代进 a.c 的 g_len（原来错误 SAFE） */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void PutA(UINT8 *p);
static UINT32 g_len;
void Top_B(void) { UINT8 a[4]; g_len = 2; PutA(a); }
