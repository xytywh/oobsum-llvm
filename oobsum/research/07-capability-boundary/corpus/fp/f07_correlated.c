/* 安全：n 与 buf 的容量由同一处保证 */
int f07(int *buf, int n, int cap) { if (n > cap) return -1;
  int s = 0; for (int i = 0; i < n; ++i) s += buf[i]; return s; }
int f07_call(void) { int b[10]; return f07(b, 10, 10); }
