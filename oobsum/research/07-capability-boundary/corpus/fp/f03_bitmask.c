int a[8];
int f03(unsigned i) { return a[i & 7]; }       /* 安全 */
