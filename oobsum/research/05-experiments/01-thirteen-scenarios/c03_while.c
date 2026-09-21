int f(int n) {
    int a[10];
    int i = 0;
    while (i < n) ++i;
    if (n == 25) return a[i];       /* i == 25 */
    return 0;
}
