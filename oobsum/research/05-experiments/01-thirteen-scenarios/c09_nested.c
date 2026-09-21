static int raw_get(int *d, int i) { return d[i]; }
static int vec_get(int *d, int n, int i) { (void)n; return raw_get(d, i); }
int vec_get_checked(int *d, int n, int i) { return vec_get(d, n, i); }
int f(void) { int a[10]; return vec_get_checked(a, 10, 33); }
