int f2(int *p, int i) { return p[i]; }
int f1(int *p, int i) { return f2(p, i); }
int entry(void) { int arr[10]; return f1(arr, 20); }
