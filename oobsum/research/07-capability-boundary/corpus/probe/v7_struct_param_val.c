struct S { int n; int d[8]; };
static int rd(struct S s, int i) { return s.d[i]; }
int v7(void) { struct S s; return rd(s, 11); }
