int f(void) {
    int a[10];
    int s = 0;
    for (int i = 0; i <= 10; ++i)   /* off-by-one: i reaches 10 */
        s += a[i];
    return s;
}
