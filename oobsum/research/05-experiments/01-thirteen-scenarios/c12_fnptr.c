static int get(int *a, int i) { return a[i]; }
int f(void) {
    int a[10];
    int (*fp)(int *, int) = &get;
    return fp(a, 22);
}
