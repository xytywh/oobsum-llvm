int f(void) {
    int a[10];
    int *p = a + 6;
    return *(p + 7);                /* a[13] */
}
