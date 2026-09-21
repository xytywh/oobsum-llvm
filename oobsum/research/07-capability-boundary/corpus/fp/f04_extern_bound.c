extern int get_n(void);
int a[10];
int f04(void) { int n = get_n(); if (n < 0 || n >= 10) return -1; return a[n]; }  /* 安全 */
