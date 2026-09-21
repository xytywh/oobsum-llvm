int f(int n) {
    int a[10];
    int i = 0, s = 0;
    for (i = 0; i < n; ++i) {
        if (i == 3) goto bump;
        s += a[i];
        continue;
bump:
        i = n + 2;                  /* jumps the index past the loop bound */
        s += a[i];
    }
    return s;
}
