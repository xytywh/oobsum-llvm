int f(void) {
    int a[10];
    int *p = a;
    int *q = p;                     /* q aliases a */
    return q[17];
}
