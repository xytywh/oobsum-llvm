int a[10];
static int rec(int i) { if (i <= 0) return a[0]; return a[i] + rec(i - 1); }
int f08(void) { return rec(9); }     /* 安全：9..0 */
