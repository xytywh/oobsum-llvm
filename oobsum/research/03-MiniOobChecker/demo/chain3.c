int leaf(int *p, int i)          { return p[i]; }
int mid(int *q, int k)           { return leaf(q, k + 5); }
int top(int *r, int m)           { return mid(r, m + 2); }
int entry(void) { int a[10]; return top(a, 20); }   /* 20 -> +2 -> +5 = 27 >= 10 */
