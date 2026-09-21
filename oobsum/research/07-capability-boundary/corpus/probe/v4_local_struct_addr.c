struct S { int n; int d[8]; };
int v4(void) { struct S s; int *p = s.d; return p[11]; }
