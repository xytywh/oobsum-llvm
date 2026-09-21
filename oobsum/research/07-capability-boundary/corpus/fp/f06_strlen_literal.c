#include <string.h>
int f06(void) { char buf[16]; const char *s = "abc";
  size_t n = strlen(s); buf[n] = 0; return buf[0]; }   /* 安全：n==3 */
