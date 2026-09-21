int f20(int *p, int i) { return p[i]; }
int f19(int *p, int i) { return f20(p, i + 1); }
int f18(int *p, int i) { return f19(p, i + 1); }
int f17(int *p, int i) { return f18(p, i + 1); }
int f16(int *p, int i) { return f17(p, i + 1); }
int f15(int *p, int i) { return f16(p, i + 1); }
int f14(int *p, int i) { return f15(p, i + 1); }
int f13(int *p, int i) { return f14(p, i + 1); }
int f12(int *p, int i) { return f13(p, i + 1); }
int f11(int *p, int i) { return f12(p, i + 1); }
int f10(int *p, int i) { return f11(p, i + 1); }
int f9(int *p, int i) { return f10(p, i + 1); }
int f8(int *p, int i) { return f9(p, i + 1); }
int f7(int *p, int i) { return f8(p, i + 1); }
int f6(int *p, int i) { return f7(p, i + 1); }
int f5(int *p, int i) { return f6(p, i + 1); }
int f4(int *p, int i) { return f5(p, i + 1); }
int f3(int *p, int i) { return f4(p, i + 1); }
int f2(int *p, int i) { return f3(p, i + 1); }
int f1(int *p, int i) { return f2(p, i + 1); }
int entry_bad(void)  { int a[10]; return f1(a, 5); }   /* 5 + 19 = 24 -> OOB */
int entry_safe(void) { int a[64]; return f1(a, 5); }   /* 24 < 64 -> safe  */
