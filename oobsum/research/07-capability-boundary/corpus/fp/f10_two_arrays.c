/* 安全：小数组用小下标，大数组用大下标 */
int small[4], big[100];
int f10(int k) { if (k < 0 || k >= 4) return 0; return small[k] + big[k * 10]; }
