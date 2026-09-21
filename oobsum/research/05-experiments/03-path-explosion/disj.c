int f(int *p, int *c) {
  int i = 0;
  if (c[0]) i += 1;
  if (c[1]) i += 2;
  if (c[2]) i += 4;
  if (c[3]) i += 8;
  if (c[4]) i += 16;
  if (c[5]) i += 32;
  if (c[6]) i += 64;
  if (c[7]) i += 128;
  if (c[8]) i += 256;
  if (c[9]) i += 512;
  if (c[10]) i += 1024;
  if (c[11]) i += 2048;
  return p[i];
}
int entry(void) { int a[10]; int c[12]; return f(a, c); }
