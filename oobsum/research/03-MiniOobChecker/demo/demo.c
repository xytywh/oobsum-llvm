int read(int *p, int n, int i) { return p[i]; }

int foo(void) {
    int a[10];
    return read(a, 10, 20);      /* 20 >= 10 -> out of bounds, but ONLY visible here */
}
