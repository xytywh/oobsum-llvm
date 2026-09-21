int mid_a(int *p, int i);
int mid_b(int *p, int i);
int mid_indep(int x);
int top_a(void) { int a[10]; return mid_a(a, 3); }
int top_b(void) { int a[10]; return mid_b(a, 3); }
int top_indep(void) { return mid_indep(21); }
