int t09(void) { int a[10]; int *p = a; int *q = p; q[9] = 1; return a[9]; }
