/* ③ 外部全局 g_x：SetX 写、PutX 读 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
UINT32 g_x;
void SetX(UINT32 v) { g_x = v; }
void PutX(UINT8 *p) { p[g_x] = 0; }
