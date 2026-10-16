typedef unsigned int UINT32;
typedef unsigned char UINT8;
extern UINT32 g_x;
void Mid(UINT8 *p);
void Top_X(void) { UINT8 a[4]; g_x = 2; Mid(a); }
