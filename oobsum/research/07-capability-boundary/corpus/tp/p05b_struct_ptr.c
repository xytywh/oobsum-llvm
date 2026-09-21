struct S { int n; int d[8]; };
static int rd(struct S *s, int i) { return s->d[i]; }
int p05b(void) { struct S s; return rd(&s, 11); }
