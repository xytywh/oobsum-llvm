static int get(int *p, int i) { return p[i]; }
int p13(void) { int a[10]; int (*fp)(int*, int) = &get; return fp(a, 22); }
