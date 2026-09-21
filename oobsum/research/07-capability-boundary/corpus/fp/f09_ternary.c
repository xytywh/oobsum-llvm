int a[10];
int f09(int i) { return a[(i < 0 || i > 9) ? 0 : i]; }   /* 安全 */
