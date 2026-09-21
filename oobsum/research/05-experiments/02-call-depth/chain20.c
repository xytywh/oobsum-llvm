int f20(int *p, int i) { return p[i]; }
int f19(int *p, int i) { return f20(p, i); }
int f18(int *p, int i) { return f19(p, i); }
int f17(int *p, int i) { return f18(p, i); }
int f16(int *p, int i) { return f17(p, i); }
int f15(int *p, int i) { return f16(p, i); }
int f14(int *p, int i) { return f15(p, i); }
int f13(int *p, int i) { return f14(p, i); }
int f12(int *p, int i) { return f13(p, i); }
int f11(int *p, int i) { return f12(p, i); }
int f10(int *p, int i) { return f11(p, i); }
int f9(int *p, int i) { return f10(p, i); }
int f8(int *p, int i) { return f9(p, i); }
int f7(int *p, int i) { return f8(p, i); }
int f6(int *p, int i) { return f7(p, i); }
int f5(int *p, int i) { return f6(p, i); }
int f4(int *p, int i) { return f5(p, i); }
int f3(int *p, int i) { return f4(p, i); }
int f2(int *p, int i) { return f3(p, i); }
int f1(int *p, int i) { return f2(p, i); }
int entry(void) { int arr[10]; return f1(arr, 20); }
