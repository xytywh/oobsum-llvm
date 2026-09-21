#include <string.h>
void t11(const char *s) { char buf[64]; strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf)-1] = 0; }
