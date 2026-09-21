int leaf(int *base, int idx) { return base[idx]; }

int mid(int *arr, int k) { return leaf(arr, k + 1); }

int top(int *p, int n) { return mid(p, n * 2); }

int caller() {
    int a[10];
    return top(a, 7);          /* n=7 -> k=14 -> idx=15, a has 10 elems */
}
