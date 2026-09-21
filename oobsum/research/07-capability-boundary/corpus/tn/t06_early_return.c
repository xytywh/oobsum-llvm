int t06(int *p, int n, int i) { if (i >= n) return 0; return p[i]; }
int t06_call(void) { int b[10]; return t06(b, 10, 3); }
