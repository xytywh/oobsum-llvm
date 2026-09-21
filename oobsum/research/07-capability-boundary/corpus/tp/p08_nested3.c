static int leaf(int *p, int i) { return p[i]; }
static int mid(int *p, int k) { return leaf(p, k + 5); }
static int top(int *p, int m) { return mid(p, m + 2); }
int p08(void) { int a[10]; return top(a, 20); }
