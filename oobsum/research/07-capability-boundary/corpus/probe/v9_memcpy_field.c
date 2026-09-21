#include <string.h>
struct S { int n; char d[8]; };
void v9(void) { struct S s; char src[64]; memcpy(s.d, src, sizeof(src)); }
