static int rec(int *a, int i) { if (i <= 0) return a[0]; return a[i] + rec(a, i - 1); }
int p09(void) { int a[10]; return rec(a, 15); }
