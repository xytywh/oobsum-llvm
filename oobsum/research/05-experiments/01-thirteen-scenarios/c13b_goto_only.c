int f(int n) {
    int a[10];
    int i = 0;
    if (n > 0) goto bump;
    return a[0];
bump:
    i = 12;
    return a[i];
}
