/* ② 两个函数内 static 都叫 n，是两个对象（原来错误 SAFE） */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void PutL(UINT8 *p) { static UINT32 n = 100; p[n] = 0; }
void Top_L(void) { static UINT32 n; UINT8 a[4]; n = 2; PutL(a); }
