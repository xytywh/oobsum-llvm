int leaf_get(int *p, int i);
int mid_a(int *p, int i) { return leaf_get(p, i); }
int mid_b(int *p, int i) { return leaf_get(p, i + 1); }
int mid_indep(int x) { return x * 2; }
