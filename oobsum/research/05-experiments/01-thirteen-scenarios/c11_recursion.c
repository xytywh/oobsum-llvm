static int rec(int *a, int i) {
    if (i <= 0) return a[i];
    return rec(a, i - 1) + a[i];    /* a[15] on entry */
}
int f(void) { int a[10]; return rec(a, 15); }
