#include <stdlib.h>
int p15(void) { int *p = (int*)malloc(4 * sizeof(int)); if (!p) return 0;
  int v = p[9]; free(p); return v; }
