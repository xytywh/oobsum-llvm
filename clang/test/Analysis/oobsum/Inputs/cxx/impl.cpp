// C++ 文件里 extern "C" 的函数（SPEC-0121）：键与 C 相同，C 文件调用它时三阶段能接上
typedef unsigned char UINT8;
typedef unsigned int UINT32;

extern "C" void Fill_C(UINT8 *p, UINT32 i) { p[i] = 0; }
extern "C" void Mid_C(UINT8 *p, UINT32 i) { Fill_C(p, i); }
