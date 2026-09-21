#include <stdlib.h>
int t15(int n) { if (n <= 0 || n > 1000) return 0;
  int *p = (int*)malloc((unsigned)n * sizeof(int)); if (!p) return 0;
  int s = 0; for (int i = 0; i < n; ++i) s += p[i]; free(p); return s; }
