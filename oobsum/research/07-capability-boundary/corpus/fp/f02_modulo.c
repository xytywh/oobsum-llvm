int a[10];
int f02(int i) { return a[i % 10]; }          /* 负数 i 时其实不安全，但常见写法 */
