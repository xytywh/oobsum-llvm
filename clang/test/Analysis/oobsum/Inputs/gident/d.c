/* ③ d.c 没声明 g_x：SetX 改过之后，PutX 读到的不是 Mid 入口的值（原来没记，错误 SAFE） */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void SetX(UINT32 v);
void PutX(UINT8 *p);
void Mid(UINT8 *p) { SetX(100); PutX(p); }
