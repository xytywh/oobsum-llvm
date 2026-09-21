/* 安全：flag==0 时 i 才被用作下标，而那条路径上 i=3 */
int a[10];
int f01(int flag) { int i = 3; if (flag) i = 100; if (!flag) return a[i]; return 0; }
