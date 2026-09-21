#include <stdlib.h>
struct S { int n; int d[8]; };
int v12(void) { struct S *p = (struct S*)malloc(sizeof(struct S)); if (!p) return 0;
  int v = p->d[11]; free(p); return v; }
